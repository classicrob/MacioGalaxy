// Bakes terrain brushes into a Full-archive mod (spikes S6 and S7, docs/dev/level-editor/TECH.md
// 6.3): each brush becomes ObjectData/<Name>.arc with <name>.bmd (the mesh, its material cloned
// from a model on the disc), <name>.kcl and <name>.pa (its collision), registered as a planet in
// PlanetMapDataTable and placed in a stage with a fitted gravity row. Everything goes through
// the game's normal archive path, as a player's baked mod would.
//
// The spike scene (the same places as src/Game/Editor/EditorTerrainTest.cpp):
//   EditorBlockoutBox     3000 x 300 x 3000 box centred at (-20000, 0, 30000), BathtubPlanet's
//                         checker material, top face Sound_code Stone; GlobalCubeGravity
//   EditorBlockoutSphere  radius-1000 sphere centred at (-20000, 0, 40000), BeeWallClimbPlanetA's
//                         Soil06_v, Sound_code Lawn; GlobalPointGravity
//
// usage: petari_terrain_bake --disc <disc files/> --out <mod>/files [--stage EggStarGalaxy]
// The disc is only read. Writes plain (uncompressed) RARCs, which the game and mod loader accept.
#include <petari/editor/formats/formats.hpp>
#include <petari/editor/terrain/kcl_writer.hpp>
#include <petari/editor/terrain/model_writer.hpp>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
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

    struct Brush {
        const char* name;
        T::Mesh mesh;
        T::Vec3 position;
        const char* donorArchive;
        const char* donorModel;
        const char* donorMaterial;
    };

    F::Bytes bakeArchive(const fs::path& disc, const Brush& brush) {
        const auto donor = readArchive(disc / "ObjectData" / (std::string(brush.donorArchive) + ".arc"));
        const F::Bytes* pDonorModel = donor ? donor->file(brush.donorModel) : nullptr;
        if (pDonorModel == nullptr) {
            die(std::string("donor model not found: ") + brush.donorArchive + "/" + brush.donorModel);
        }
        std::string error;
        const auto material = T::cloneMaterial(*pDonorModel, brush.donorMaterial, &error);
        if (!material) {
            die(error);
        }
        const T::ModelResult model = T::buildModel(brush.mesh, *material);
        if (!model.error.empty()) {
            die(model.error);
        }
        const T::KclResult kcl = T::buildKcl(brush.mesh);
        if (!kcl.error.empty()) {
            die(kcl.error);
        }
        const std::string base = lower(brush.name);
        std::printf("%s: %d triangles; model %zu bytes (material %s from %s), kcl %zu bytes, pa %zu bytes\n", brush.name, model.triangles,
                    model.model.size(), brush.donorMaterial, brush.donorArchive, kcl.kcl.size(), kcl.pa.size());
        return F::Archive::create(base, {{base + ".bmd", model.model}, {base + ".kcl", kcl.kcl}, {base + ".pa", kcl.pa}}).build();
    }
}  // namespace

int main(int argc, char** argv) {
    fs::path disc, out;
    std::string stage = "EggStarGalaxy";
    for (int i = 1; i + 1 < argc; i += 2) {
        if (std::strcmp(argv[i], "--disc") == 0) {
            disc = argv[i + 1];
        } else if (std::strcmp(argv[i], "--out") == 0) {
            out = argv[i + 1];
        } else if (std::strcmp(argv[i], "--stage") == 0) {
            stage = argv[i + 1];
        }
    }
    if (disc.empty() || out.empty()) {
        std::fprintf(stderr, "usage: %s --disc <disc files/> --out <mod>/files [--stage EggStarGalaxy]\n", argv[0]);
        return 2;
    }

    // The brushes, in their parts' local space (the placement row puts them in the stage).
    T::Mesh box = T::makeBox({0, 0, 0}, {1500, 150, 1500});
    T::Surface stone;
    stone.sound = T::soundCode("Stone");
    box.surfaces.push_back(stone);
    for (std::size_t t = 0; t < box.triangleCount(); t++) {
        if (box.normals[box.indices[t * 3]].y > 0.5f) {
            box.triangleSurface[t] = 1;
        }
    }
    T::Surface lawn;
    lawn.sound = T::soundCode("Lawn");
    const Brush brushes[] = {
        {"EditorBlockoutBox", box, {-20000, 0, 30000}, "BathtubPlanet", "bathtubplanet.bdl", "CheckMat_v"},
        {"EditorBlockoutSphere", T::makeSphere({0, 0, 0}, 1000.0f, 24, 32, lawn), {-20000, 0, 40000}, "BeeWallClimbPlanetA",
         "beewallclimbplaneta.bdl", "Soil06_v"},
    };
    for (const Brush& brush : brushes) {
        if (!writeFile(out / "ObjectData" / (std::string(brush.name) + ".arc"), bakeArchive(disc, brush))) {
            die("cannot write the planet archive");
        }
    }

    // Register the planets: rows in PlanetMapDataTable (no Low/Middle/Bloom/Water/Indirect).
    auto planets = readArchive(disc / "ObjectData" / "PlanetMapDataTable.arc");
    const F::Bytes* pPlanetTable = planets ? planets->file("planetmapdatatable.bcsv") : nullptr;
    if (pPlanetTable == nullptr) {
        die("PlanetMapDataTable.arc not found");
    }
    auto planetTable = F::Table::parse(*pPlanetTable);
    for (const Brush& brush : brushes) {
        const int row = planetTable->addRow(0);
        for (const char* pFlag : {"LowFlag", "MiddleFlag", "BloomFlag", "WaterFlag", "IndirectFlag"}) {
            set(*planetTable, row, pFlag, integer(0));
        }
        for (int i = 0; i < 8; i++) {
            set(*planetTable, row, ("ForceLowScenarioName" + std::to_string(i)).c_str(), text(""));
        }
        set(*planetTable, row, "PlanetName", text(brush.name));
    }
    planets->replace("planetmapdatatable.bcsv", planetTable->build());
    writeFile(out / "ObjectData" / "PlanetMapDataTable.arc", planets->build());

    // Place them, with their gravity, in the stage's own (galaxy) zone, on the Common layer.
    auto stageArchive = readArchive(disc / "StageData" / (stage + ".arc"));
    if (!stageArchive) {
        die("stage archive not found");
    }
    auto objects = F::Table::parse(*stageArchive->file("jmp/placement/common/objinfo"));
    auto gravity = F::Table::parse(*stageArchive->file("jmp/placement/common/planetobjinfo"));
    if (!objects || !gravity) {
        die("the stage has no Common ObjInfo / PlanetObjInfo");
    }
    std::int32_t id = std::max(nextId(*objects), nextId(*gravity));
    for (const Brush& brush : brushes) {
        addPlacementRow(*objects, brush.name, id++, brush.position, {1, 1, 1});
    }
    // GlobalCubeGravity: the row's position is the cube's bottom centre, its scale the size / 1000
    // (CubeGravityCreator); Obj_arg0-2 = 3: both faces of each axis attract.
    const int cube = addPlacementRow(*gravity, "GlobalCubeGravity", id++, {-20000, -150, 30000}, {3, 0.3f, 3});
    // GlobalPointGravity: Distant is the radius, so Range counts from the surface.
    const int point = addPlacementRow(*gravity, "GlobalPointGravity", id++, {-20000, 0, 40000}, {2, 2, 2});
    for (int row : {cube, point}) {
        set(*gravity, row, "Range", real(2000));
        set(*gravity, row, "Distant", real(row == point ? 1000 : 0));
        set(*gravity, row, "Priority", integer(1000));
        set(*gravity, row, "Inverse", integer(0));
        set(*gravity, row, "Power", text("Normal"));
        set(*gravity, row, "Gravity_type", text("Normal"));
    }
    for (const char* pArg : {"Obj_arg0", "Obj_arg1", "Obj_arg2"}) {
        set(*gravity, cube, pArg, integer(3));
    }
    stageArchive->replace("jmp/placement/common/objinfo", objects->build());
    stageArchive->replace("jmp/placement/common/planetobjinfo", gravity->build());
    writeFile(out / "StageData" / (stage + ".arc"), stageArchive->build());
    std::printf("placed %zu planets and 2 gravity fields in %s; PlanetMapDataTable has %d rows\n", std::size(brushes), stage.c_str(),
                planetTable->rowCount());
    return 0;
}
