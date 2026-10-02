// Writes terrain brushes as rows and bakes them into a Full-archive mod (spikes S6-S8; M5), the
// way the editor's save does (petari/editor/terrain/bake.hpp): each EditorBrush row becomes a
// generated planet (ObjectData/<Planet>.arc with its model, .kcl and .pa), registered in
// PlanetMapDataTable, with its fitted gravity row. Everything then goes through the game's normal
// archive path, as a player's saved mod would.
//
// The spike scene (the same places as src/Game/Editor/EditorTerrainTest.cpp):
//   a checker box, 3000 x 300 x 3000, centred at (-20000, 0, 30000): cube gravity
//   a grass sphere, radius 1000, centred at (-20000, 0, 40000): point gravity
//
// usage: petari_terrain_bake --disc <disc files/> --out <mod>/files [--stage EggStarGalaxy]
//        petari_terrain_bake --disc <disc files/> --out <mod>/files --galaxy ModTestGalaxy
// --galaxy (spike S8, TECH.md 5.7) writes a new galaxy instead of editing a stage: its stage and
// Scenario archives (the tables of a small disc galaxy, TransformationExGalaxy, emptied: schemas
// only) holding a checker box brush at the origin, a start above it and Power Star 1 where Mario
// lands; and <mod>/galaxy.json for the mod galaxy registry.
// The disc is only read. Writes plain (uncompressed) RARCs, which the game and mod loader accept.
#include <petari/editor/formats/formats.hpp>
#include <petari/editor/terrain/bake.hpp>
#include <petari/editor/terrain/brush.hpp>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>

namespace F = PetariEditor::Formats;
namespace T = PetariEditor::Terrain;
namespace fs = std::filesystem;

namespace {
    bool readFile(const fs::path& path, F::Bytes* pOut) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return false;
        }
        pOut->assign(std::istreambuf_iterator< char >(file), std::istreambuf_iterator< char >());
        return true;
    }

    bool writeFile(const fs::path& path, const F::Bytes& bytes) {
        fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast< const char* >(bytes.data()), static_cast< std::streamsize >(bytes.size()));
        return static_cast< bool >(file);
    }

    std::string lower(std::string text) {
        for (char& c : text) {
            c = static_cast< char >(std::tolower(static_cast< unsigned char >(c)));
        }
        return text;
    }

    [[noreturn]] void die(const std::string& message) {
        std::fprintf(stderr, "terrain_bake: %s\n", message.c_str());
        std::exit(1);
    }

    std::optional< F::Archive > readArchive(const fs::path& path) {
        F::Bytes raw;
        if (!readFile(path, &raw)) {
            return std::nullopt;
        }
        return F::Archive::parse(raw);
    }

    F::CellValue integer(std::int32_t v) {
        F::CellValue c;
        c.kind = F::CellValue::Kind::Int;
        c.i = v;
        return c;
    }
    F::CellValue real(float v) {
        F::CellValue c;
        c.kind = F::CellValue::Kind::Float;
        c.f = v;
        return c;
    }
    F::CellValue text(const std::string& v) {
        F::CellValue c;
        c.kind = F::CellValue::Kind::String;
        c.s = v;
        return c;
    }

    // Sets a cell if the table has the column (placement tables differ in columns).
    void set(F::Table& table, int row, const char* pColumn, const F::CellValue& value) {
        const int column = table.column(F::jmapHash(pColumn));
        if (column >= 0) {
            table.set(row, column, value);
        }
    }

    // A new row with every link, switch and arg -1, no rotation, scale 1.
    int addPlacementRow(F::Table& table, const std::string& name, std::int32_t id, const T::Vec3& pos, const T::Vec3& scale) {
        const int row = table.addRow(-1);
        for (const F::TableColumn& column : table.columns()) {
            if (column.type != F::ColumnType::Float && column.type != F::ColumnType::InlineString && column.type != F::ColumnType::StringOffset) {
                table.set(row, table.column(column.hash), integer(-1));
            }
        }
        set(table, row, "name", text(name));
        set(table, row, "l_id", integer(id));
        set(table, row, "pos_x", real(pos.x));
        set(table, row, "pos_y", real(pos.y));
        set(table, row, "pos_z", real(pos.z));
        set(table, row, "dir_x", real(0));
        set(table, row, "dir_y", real(0));
        set(table, row, "dir_z", real(0));
        set(table, row, "scale_x", real(scale.x));
        set(table, row, "scale_y", real(scale.y));
        set(table, row, "scale_z", real(scale.z));
        return row;
    }

    std::int32_t nextId(const F::Table& table) {
        std::int32_t id = 0;
        const int column = table.column(F::jmapHash("l_id"));
        for (int r = 0; column >= 0 && r < table.rowCount(); r++) {
            id = std::max(id, table.get(r, column).i + 1);
        }
        return std::max(id, 9000);
    }

    // A brush row: bottom centre `base`, size / 1000 as the scale, shape and look in Obj_arg0-3.
    void addBrushRow(F::Table& table, std::int32_t id, const T::Vec3& base, const T::Vec3& scale, T::BrushShape shape, int material) {
        const int row = addPlacementRow(table, T::kBrushClass, id, base, scale);
        set(table, row, "Obj_arg0", integer(static_cast< int >(shape)));
        set(table, row, "Obj_arg1", integer(material));
        set(table, row, "Obj_arg2", integer(0));
        set(table, row, "Obj_arg3", integer(1));
    }

    // Bakes the brushes of `archives` and writes every file of the result below `out`.
    void bakeAndWrite(const fs::path& disc, const fs::path& out, std::map< std::string, F::Bytes > archives) {
        T::BakeInput input;
        input.stageArchives = std::move(archives);
        input.readDisc = [&disc](const std::string& path) -> std::optional< F::Bytes > {
            F::Bytes bytes;
            if (!readFile(disc / path.substr(1), &bytes)) {
                return std::nullopt;
            }
            return bytes;
        };
        const T::BakeOutput baked = T::bakeBrushes(input);
        if (!baked.error.empty()) {
            die(baked.error);
        }
        for (const std::string& line : baked.log) {
            std::printf("%s\n", line.c_str());
        }
        for (const auto& [path, bytes] : baked.files) {
            if (!writeFile(out / path.substr(1), bytes)) {
                die("cannot write " + path);
            }
        }
        std::printf("baked %d brushes into %zu files\n", baked.brushes, baked.files.size());
    }

    void writeSkeletonGalaxy(const fs::path& disc, const fs::path& out, const std::string& name) {
        const char* pTemplate = "TransformationExGalaxy";
        auto stageTemplate = readArchive(disc / "StageData" / (std::string(pTemplate) + ".arc"));
        auto scenarioTemplate = readArchive(disc / "StageData" / pTemplate / (std::string(pTemplate) + "Scenario.arc"));
        if (!stageTemplate || !scenarioTemplate) {
            die("the template galaxy is not on the disc");
        }
        // The stage: every table emptied (the template's schemas only); the intro camera and the
        // rails' point tables dropped.
        std::vector< std::pair< std::string, F::Bytes > > files;
        std::map< std::string, F::Table > tables;
        for (const std::string& path : stageTemplate->files()) {
            if (path.size() > 5 && path.compare(path.size() - 5, 5, ".canm") == 0) {
                continue;
            }
            if (path.find("commonpathpointinfo") != std::string::npos) {
                continue;
            }
            auto table = F::Table::parse(*stageTemplate->file(path));
            if (!table || path == "jmp/list/stageinfo") {
                files.push_back({path, *stageTemplate->file(path)});
                continue;
            }
            while (table->rowCount() > 0) {
                table->removeRow(table->rowCount() - 1);
            }
            tables.emplace(path, *table);
        }
        F::Table& objects = tables.at("jmp/placement/common/objinfo");
        F::Table& start = tables.at("jmp/start/common/startinfo");
        addBrushRow(objects, 0, {0, -150, 0}, {3, 0.3f, 3}, T::BrushShape::Box, 0);  // top face at y = 150
        // Power Star 1 just above the top face, where Mario lands from the start.
        const int star = addPlacementRow(objects, "PowerStar", 1, {0, 260, 0}, {1, 1, 1});
        set(objects, star, "Obj_arg0", integer(1));
        const int mario = addPlacementRow(start, "Mario", 0, {0, 600, 0}, {1, 1, 1});
        set(start, mario, "MarioNo", integer(0));
        set(start, mario, "Camera_id", integer(-1));
        for (auto& [path, table] : tables) {
            files.push_back({path, table.build()});
        }
        bakeAndWrite(disc, out, {{"/StageData/" + name + ".arc", F::Archive::create("stage", files).build()}});

        // The Scenario archive: one mission (Power Star 1) with only the Common layer, one zone.
        auto scenarios = F::Table::parse(*scenarioTemplate->file("scenariodata.bcsv"));
        auto zones = F::Table::parse(*scenarioTemplate->file("zonelist.bcsv"));
        while (scenarios->rowCount() > 1) {
            scenarios->removeRow(scenarios->rowCount() - 1);
        }
        while (zones->rowCount() > 1) {
            zones->removeRow(zones->rowCount() - 1);
        }
        F::CellValue none = integer(0);
        scenarios->addColumn(F::jmapHash(name.c_str()), F::ColumnType::Long, none);
        set(*scenarios, 0, "ScenarioNo", integer(1));
        set(*scenarios, 0, "PowerStarId", integer(1));
        set(*scenarios, 0, name.c_str(), integer(0));
        set(*zones, 0, "ZoneName", text(name));
        const std::string lowerName = lower(name);
        writeFile(out / "StageData" / name / (name + "Scenario.arc"),
                  F::Archive::create(lowerName + "scenario", {{"scenariodata.bcsv", scenarios->build()}, {"zonelist.bcsv", zones->build()}}).build());

        // The registry entry, beside files/.
        const std::string json = "{\n  \"format\": 1,\n  \"galaxies\": [ {\n    \"name\": \"" + name +
                                 "\",\n    \"title\": { \"en\": \"Blockout Test Galaxy\", \"fr\": \"Galaxie d'essai\", \"es\": \"Galaxia de prueba\" },\n"
                                 "    \"shortTitle\": { \"en\": \"Blockout Test\" },\n"
                                 "    \"missions\": [ { \"en\": \"The Checkered Box\" } ],\n"
                                 "    \"music\": \"EggStarGalaxy\"\n  } ]\n}\n";
        writeFile(out.parent_path() / "galaxy.json", F::Bytes(json.begin(), json.end()));
        std::printf("galaxy %s: %zu stage files, 1 mission, galaxy.json\n", name.c_str(), files.size());
    }
}  // namespace

int main(int argc, char** argv) {
    fs::path disc, out;
    std::string stage = "EggStarGalaxy";
    std::string galaxy;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (std::strcmp(argv[i], "--disc") == 0) {
            disc = argv[i + 1];
        } else if (std::strcmp(argv[i], "--out") == 0) {
            out = argv[i + 1];
        } else if (std::strcmp(argv[i], "--stage") == 0) {
            stage = argv[i + 1];
        } else if (std::strcmp(argv[i], "--galaxy") == 0) {
            galaxy = argv[i + 1];
        }
    }
    if (disc.empty() || out.empty()) {
        std::fprintf(stderr, "usage: %s --disc <disc files/> --out <mod>/files [--stage EggStarGalaxy]\n", argv[0]);
        return 2;
    }

    if (!galaxy.empty()) {
        writeSkeletonGalaxy(disc, out, galaxy);
        return 0;
    }

    // The two brushes, placed in the stage's own (galaxy) zone on the Common layer, then baked.
    auto stageArchive = readArchive(disc / "StageData" / (stage + ".arc"));
    if (!stageArchive) {
        die("stage archive not found");
    }
    auto objects = F::Table::parse(*stageArchive->file("jmp/placement/common/objinfo"));
    if (!objects) {
        die("the stage has no Common ObjInfo");
    }
    std::int32_t id = nextId(*objects);
    addBrushRow(*objects, id++, {-20000, -150, 30000}, {3, 0.3f, 3}, T::BrushShape::Box, 0);   // checker (Stone)
    addBrushRow(*objects, id++, {-20000, -1000, 40000}, {2, 2, 2}, T::BrushShape::Sphere, 1);  // grass (Lawn)
    stageArchive->replace("jmp/placement/common/objinfo", objects->build());
    bakeAndWrite(disc, out, {{"/StageData/" + stage + ".arc", stageArchive->build()}});
    return 0;
}
