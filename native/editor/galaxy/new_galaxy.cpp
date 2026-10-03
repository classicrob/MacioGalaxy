// A new galaxy's files from a template (new_galaxy.hpp).
#include <petari/editor/galaxy/new_galaxy.hpp>
#include <petari/editor/terrain/brush.hpp>
#include <petari/editor/terrain/kcl_reader.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace PetariEditor::Galaxy {

namespace {
    namespace F = Formats;
    namespace T = Terrain;

    // The emptied stage every non-copy template starts from: a small disc galaxy's own archive,
    // its tables kept as schemas (TECH.md 5.7, spike S8).
    const char* const kTemplateGalaxy = "TransformationExGalaxy";
    const char* const kDefaultSkyFrom = "EggStarGalaxy";
    // The objects the game makes as skies (NameObjFactory: Sky, ProjectionMapSky).
    const char* const kSkies[] = {"AuroraSky",    "ChildRoomSky",     "CloudSky",   "DesertSky",          "FamicomMarioSky", "GalaxySky", "GreenPlanetOrbitSky",
                                  "HalfGalaxySky", "KoopaVS1Sky",     "KoopaVS2Sky", "MilkyWaySky",       "PhantomSky",      "RockPlanetOrbitSky",
                                  "StarrySky",    "SummerSky",        "VRDarkSpace", "VROrbit",           "VRSandwichSun"};

    bool isSky(const std::string& name) {
        return std::find(std::begin(kSkies), std::end(kSkies), name) != std::end(kSkies);
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
    F::CellValue text(const std::string& s) {
        F::CellValue c;
        c.kind = F::CellValue::Kind::String;
        c.s = s;
        return c;
    }
    void set(F::Table& table, int row, const char* pColumn, const F::CellValue& value) {
        const int column = table.column(F::jmapHash(pColumn));
        if (column >= 0) {
            table.set(row, column, value);
        }
    }
    float getF(const F::Table& table, int row, const char* pColumn) {
        const int column = table.column(F::jmapHash(pColumn));
        return column >= 0 ? table.get(row, column).f : 0.0f;
    }
    std::string getS(const F::Table& table, int row, const char* pColumn) {
        const int column = table.column(F::jmapHash(pColumn));
        return column >= 0 ? table.get(row, column).s : std::string();
    }
    std::string lower(std::string s) {
        for (char& c : s) {
            c = static_cast< char >(std::tolower(static_cast< unsigned char >(c)));
        }
        return s;
    }
    std::string jsonString(const std::string& s) {
        std::string out = "\"";
        for (char c : s) {
            if (c == '"' || c == '\\') {
                out += '\\';
                out += c;
            } else if (static_cast< unsigned char >(c) < 0x20) {
                out += ' ';
            } else {
                out += c;
            }
        }
        return out + "\"";
    }

    // A placement row: every int column -1 (the game's "unset"), then the name, id and transform.
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

    struct Stage {
        std::vector< std::pair< std::string, F::Bytes > > files;  // kept as they are
        std::map< std::string, F::Table > tables;                  // emptied, to fill
        F::Table& table(const std::string& path, const std::string& schemaOf) {
            auto it = tables.find(path);
            if (it == tables.end()) {
                it = tables.emplace(path, tables.at(schemaOf).schemaOnly()).first;
            }
            return it->second;
        }
    };

    std::optional< Stage > emptyStage(const DiscReader& readDisc, std::string* pError) {
        const auto raw = readDisc(std::string("StageData/") + kTemplateGalaxy + ".arc");
        const auto archive = raw ? F::Archive::parse(*raw) : std::nullopt;
        if (!archive) {
            *pError = std::string("the template galaxy (") + kTemplateGalaxy + ") is not on the disc";
            return std::nullopt;
        }
        Stage stage;
        for (const std::string& path : archive->files()) {
            // Its intro camera and its rails' points go: they belong to its own layout.
            if ((path.size() > 5 && path.compare(path.size() - 5, 5, ".canm") == 0) || path.find("commonpathpointinfo") != std::string::npos) {
                continue;
            }
            auto table = F::Table::parse(*archive->file(path));
            if (!table || path == "jmp/list/stageinfo") {
                stage.files.push_back({path, *archive->file(path)});
                continue;
            }
            while (table->rowCount() > 0) {
                table->removeRow(table->rowCount() - 1);
            }
            stage.tables.emplace(path, *table);
        }
        for (const char* pNeeded : {"jmp/placement/common/objinfo", "jmp/placement/common/planetobjinfo", "jmp/start/common/startinfo"}) {
            if (!stage.tables.count(pNeeded)) {
                *pError = std::string("the template galaxy has no ") + pNeeded;
                return std::nullopt;
            }
        }
        return stage;
    }

    // Mission k's objects: the Common layer for one mission, else layer k-1 (LayerA = mission 1).
    std::string layerOf(int mission, int missions) {
        if (missions <= 1) {
            return "common";
        }
        return std::string("layer") + static_cast< char >('a' + mission - 1);
    }

    // Where the disc places `planet`, and the gravity row placed with it (the nearest gravity
    // within 100 units in the same archive), for OnePlanet.
    struct PlanetPlacement {
        T::Vec3 dir;
        std::optional< std::pair< F::Table, int > > gravity;  // the source table and row
        T::Vec3 gravityOffset;
        std::string from;  // the archive it came from (log)
    };

    std::optional< PlanetPlacement > findPlanet(const std::string& planet, const DiscReader& readDisc, const DiscLister& listDisc) {
        for (const std::string& name : listDisc("StageData")) {
            if (name.size() < 5 || name.compare(name.size() - 4, 4, ".arc") != 0) {
                continue;
            }
            const auto raw = readDisc("StageData/" + name);
            const auto archive = raw ? F::Archive::parse(*raw) : std::nullopt;
            if (!archive) {
                continue;
            }
            for (const std::string& path : archive->files()) {
                if (path.size() < 8 || path.compare(path.size() - 8, 8, "/objinfo") != 0 || path.find("placement") == std::string::npos) {
                    continue;
                }
                const auto objects = F::Table::parse(*archive->file(path));
                if (!objects) {
                    continue;
                }
                for (int r = 0; r < objects->rowCount(); r++) {
                    if (getS(*objects, r, "name") != planet) {
                        continue;
                    }
                    PlanetPlacement found;
                    found.from = name;
                    found.dir = {getF(*objects, r, "dir_x"), getF(*objects, r, "dir_y"), getF(*objects, r, "dir_z")};
                    const T::Vec3 at{getF(*objects, r, "pos_x"), getF(*objects, r, "pos_y"), getF(*objects, r, "pos_z")};
                    float best = 100.0f;
                    for (const std::string& gravityPath : archive->files()) {
                        if (gravityPath.find("planetobjinfo") == std::string::npos) {
                            continue;
                        }
                        const auto gravities = F::Table::parse(*archive->file(gravityPath));
                        for (int g = 0; gravities && g < gravities->rowCount(); g++) {
                            if (getS(*gravities, g, "name").find("Gravity") == std::string::npos) {
                                continue;
                            }
                            const T::Vec3 p{getF(*gravities, g, "pos_x"), getF(*gravities, g, "pos_y"), getF(*gravities, g, "pos_z")};
                            const float d = std::sqrt((p.x - at.x) * (p.x - at.x) + (p.y - at.y) * (p.y - at.y) + (p.z - at.z) * (p.z - at.z));
                            if (d <= best) {
                                best = d;
                                found.gravity = std::make_pair(*gravities, g);
                                found.gravityOffset = {p.x - at.x, p.y - at.y, p.z - at.z};
                            }
                        }
                    }
                    return found;
                }
            }
        }
        return std::nullopt;
    }

    // The planet's collision bounds (its .kcl), for where Mario starts above it.
    std::optional< std::pair< T::Vec3, T::Vec3 > > planetBounds(const std::string& planet, const DiscReader& readDisc) {
        const auto raw = readDisc("ObjectData/" + planet + ".arc");
        const auto archive = raw ? F::Archive::parse(*raw) : std::nullopt;
        const F::Bytes* pKcl = archive ? archive->file(lower(planet) + ".kcl") : nullptr;
        const auto mesh = pKcl ? T::readKcl(*pKcl) : std::nullopt;
        if (!mesh || mesh->positions.empty()) {
            return std::nullopt;
        }
        T::Vec3 lo, hi;
        mesh->bounds(&lo, &hi);
        return std::make_pair(lo, hi);
    }

    // Copies `source` row `row` into `table` column by column (the columns both have).
    int copyRow(F::Table& table, const F::Table& source, int row) {
        const int out = table.addRow(-1);
        for (const F::TableColumn& column : table.columns()) {
            const int from = source.column(column.hash);
            if (from >= 0) {
                table.set(out, table.column(column.hash), source.get(row, from));
            } else if (column.type != F::ColumnType::Float && column.type != F::ColumnType::InlineString &&
                       column.type != F::ColumnType::StringOffset) {
                table.set(out, table.column(column.hash), integer(-1));
            }
        }
        return out;
    }

    // A sky's row as a disc galaxy places it (`from` first, else any): `sky`, or with `sky` empty
    // the first sky in `from`. nullopt when none is found.
    std::optional< std::pair< F::Table, int > > findSky(const std::string& sky, const std::string& from, const DiscReader& readDisc,
                                                        const DiscLister& listDisc) {
        std::vector< std::string > archives{from + ".arc"};
        if (!sky.empty()) {
            for (const std::string& name : listDisc("StageData")) {
                if (name.size() > 4 && name.compare(name.size() - 4, 4, ".arc") == 0 && name != archives[0]) {
                    archives.push_back(name);
                }
            }
        }
        for (const std::string& name : archives) {
            const auto raw = readDisc("StageData/" + name);
            const auto archive = raw ? F::Archive::parse(*raw) : std::nullopt;
            for (const std::string& path : archive ? archive->files() : std::vector< std::string >{}) {
                if (path.size() < 8 || path.compare(path.size() - 8, 8, "/objinfo") != 0 || path.find("placement") == std::string::npos) {
                    continue;
                }
                const auto objects = F::Table::parse(*archive->file(path));
                for (int r = 0; objects && r < objects->rowCount(); r++) {
                    const std::string object = getS(*objects, r, "name");
                    if (sky.empty() ? isSky(object) : object == sky) {
                        return std::make_pair(*objects, r);
                    }
                }
            }
        }
        return std::nullopt;
    }

    // Places `sky` (as the disc places it, at the origin) in `objects`; its name, or "" for none.
    std::string placeSky(F::Table& objects, const std::string& sky, const std::string& from, const DiscReader& readDisc, const DiscLister& listDisc) {
        const auto found = findSky(sky, from, readDisc, listDisc);
        int row = -1;
        if (found) {
            row = copyRow(objects, found->first, found->second);
        } else if (!sky.empty()) {
            row = addPlacementRow(objects, sky, 0, {0, 0, 0}, {1, 1, 1});
        } else {
            return "";
        }
        set(objects, row, "pos_x", real(0));
        set(objects, row, "pos_y", real(0));
        set(objects, row, "pos_z", real(0));
        set(objects, row, "l_id", integer(1000));
        return getS(objects, row, "name");
    }
}  // namespace

std::string internalNameFor(const std::string& name) {
    std::string out;
    bool startWord = true;
    for (char c : name) {
        const unsigned char u = static_cast< unsigned char >(c);
        if (std::isalnum(u) && u < 0x80) {
            out += startWord ? static_cast< char >(std::toupper(u)) : c;
            startWord = false;
        } else {
            startWord = true;
        }
    }
    if (out.empty() || std::isdigit(static_cast< unsigned char >(out[0]))) {
        out = "Mod" + out;
    }
    if (out.size() < 6 || out.compare(out.size() - 6, 6, "Galaxy") != 0) {
        out += "Galaxy";
    }
    return out;
}

std::string checkInternalName(const std::string& internalName, const DiscLister& listDisc, const std::vector< std::string >& taken) {
    if (internalName.empty() || internalName.size() > 40) {
        return "the internal name must be 1 to 40 characters";
    }
    for (char c : internalName) {
        if (!std::isalnum(static_cast< unsigned char >(c)) || static_cast< unsigned char >(c) >= 0x80) {
            return "the internal name uses letters and digits only";
        }
    }
    const std::string key = lower(internalName);
    for (const std::string& name : listDisc("StageData")) {
        std::string base = name.size() > 4 && name.compare(name.size() - 4, 4, ".arc") == 0 ? name.substr(0, name.size() - 4) : name;
        if (lower(base) == key) {
            return internalName + " is already a galaxy or zone of the game";
        }
    }
    for (const std::string& name : taken) {
        if (lower(name) == key) {
            return internalName + " is already a mod galaxy";
        }
    }
    return "";
}

std::vector< std::string > discGalaxies(const DiscLister& listDisc) {
    std::vector< std::string > out;
    const std::vector< std::string > names = listDisc("StageData");
    for (const std::string& name : names) {
        if (name.find('.') == std::string::npos) {
            for (const std::string& inner : listDisc("StageData/" + name)) {
                if (lower(inner) == lower(name + "Scenario.arc")) {
                    out.push_back(name);
                }
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector< std::string > skyNames(const DiscLister& listDisc) {
    std::vector< std::string > out;
    for (const std::string& name : listDisc("ObjectData")) {
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".arc") == 0 && isSky(name.substr(0, name.size() - 4))) {
            out.push_back(name.substr(0, name.size() - 4));
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector< std::string > planetNames(const DiscReader& readDisc) {
    std::vector< std::string > out;
    const auto raw = readDisc("ObjectData/PlanetMapDataTable.arc");
    const auto archive = raw ? F::Archive::parse(*raw) : std::nullopt;
    const F::Bytes* pTable = archive ? archive->file("planetmapdatatable.bcsv") : nullptr;
    const auto table = pTable ? F::Table::parse(*pTable) : std::nullopt;
    for (int r = 0; table && r < table->rowCount(); r++) {
        const std::string name = getS(*table, r, "PlanetName");
        if (!name.empty() && name.rfind("EditorBrush", 0) != 0) {
            out.push_back(name);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

NewGalaxyFiles buildNewGalaxy(const NewGalaxySpec& spec, const DiscReader& readDisc, const DiscLister& listDisc) {
    NewGalaxyFiles out;
    const std::string G = spec.internalName.empty() ? internalNameFor(spec.name) : spec.internalName;
    out.internalName = G;
    if (const std::string why = checkInternalName(G, listDisc, {}); !why.empty()) {
        out.error = why;
        return out;
    }
    std::vector< std::string > missions = spec.missions;
    if (missions.empty()) {
        missions.push_back("Mission 1");
    }
    if (missions.size() > 8) {
        out.error = "a galaxy holds at most 8 missions";
        return out;
    }
    const int missionCount = static_cast< int >(missions.size());
    const std::string scenarioPath = "StageData/" + G + "/" + G + "Scenario.arc";

    if (spec.from == Template::Copy) {
        // The source's own archive under the new name; its zones keep their names (shared, as on the disc).
        const auto stageRaw = readDisc("StageData/" + spec.copyOf + ".arc");
        const auto scenarioRaw = readDisc("StageData/" + spec.copyOf + "/" + spec.copyOf + "Scenario.arc");
        auto stage = stageRaw ? F::Archive::parse(*stageRaw) : std::nullopt;
        auto scenario = scenarioRaw ? F::Archive::parse(*scenarioRaw) : std::nullopt;
        if (!stage || !scenario || !scenario->file("scenariodata.bcsv") || !scenario->file("zonelist.bcsv")) {
            out.error = spec.copyOf + " is not a galaxy on the disc";
            return out;
        }
        auto zones = F::Table::parse(*scenario->file("zonelist.bcsv"));
        auto scenarios = F::Table::parse(*scenario->file("scenariodata.bcsv"));
        if (!zones || !scenarios) {
            out.error = spec.copyOf + "'s scenario tables don't parse";
            return out;
        }
        for (int r = 0; r < zones->rowCount(); r++) {
            if (getS(*zones, r, "ZoneName") == spec.copyOf) {
                set(*zones, r, "ZoneName", text(G));
            }
        }
        // ScenarioData has a layer-mask column per zone, named after it.
        const int sourceColumn = scenarios->column(F::jmapHash(spec.copyOf.c_str()));
        const int column = scenarios->addColumn(F::jmapHash(G.c_str()), F::ColumnType::Long, integer(0));
        for (int r = 0; sourceColumn >= 0 && r < scenarios->rowCount(); r++) {
            scenarios->set(r, column, scenarios->get(r, sourceColumn));
        }
        scenario->replace("zonelist.bcsv", zones->build());
        scenario->replace("scenariodata.bcsv", scenarios->build());
        while (static_cast< int >(missions.size()) < scenarios->rowCount()) {
            missions.push_back("Mission " + std::to_string(missions.size() + 1));
        }
        missions.resize(static_cast< std::size_t >(scenarios->rowCount()));
        std::vector< std::pair< std::string, F::Bytes > > scenarioFiles;
        for (const std::string& path : scenario->files()) {
            scenarioFiles.push_back({path, *scenario->file(path)});
        }
        if (!spec.sky.empty()) {
            // The chosen sky in place of the source's own (a sky placed in a shared zone stays).
            std::optional< F::Table > common;
            for (const std::string& path : stage->files()) {
                if (path.size() < 8 || path.compare(path.size() - 8, 8, "/objinfo") != 0 || path.find("placement") == std::string::npos) {
                    continue;
                }
                auto objects = F::Table::parse(*stage->file(path));
                for (int r = objects ? objects->rowCount() - 1 : -1; r >= 0; r--) {
                    if (isSky(getS(*objects, r, "name"))) {
                        out.log.push_back("removed its sky " + getS(*objects, r, "name"));
                        objects->removeRow(r);
                    }
                }
                if (path == "jmp/placement/common/objinfo") {
                    common = std::move(objects);
                } else if (objects) {
                    stage->replace(path, objects->build());
                }
            }
            if (!common) {
                out.error = spec.copyOf + " has no common objects table for the sky";
                return out;
            }
            out.log.push_back("sky: " + placeSky(*common, spec.sky, kDefaultSkyFrom, readDisc, listDisc));
            stage->replace("jmp/placement/common/objinfo", common->build());
        }
        out.files["StageData/" + G + ".arc"] = stage->build();
        out.files[scenarioPath] = F::Archive::create(lower(G) + "scenario", scenarioFiles).build();
        out.log.push_back("copied " + spec.copyOf + " (" + std::to_string(scenarios->rowCount()) + " missions; its zones are shared)");
    } else {
        auto stage = emptyStage(readDisc, &out.error);
        if (!stage) {
            return out;
        }
        F::Table& objects = stage->tables.at("jmp/placement/common/objinfo");
        F::Table& start = stage->tables.at("jmp/start/common/startinfo");
        float ground = 0.0f;  // where stars stand
        if (spec.from == Template::OnePlanet) {
            const auto placement = findPlanet(spec.planet, readDisc, listDisc);
            const auto bounds = planetBounds(spec.planet, readDisc);
            if (!placement || !bounds) {
                out.error = spec.planet + " is not a planet the disc places (no placement or collision found)";
                return out;
            }
            const int row = addPlacementRow(objects, spec.planet, 0, {0, 0, 0}, {1, 1, 1});
            set(objects, row, "dir_x", real(placement->dir.x));
            set(objects, row, "dir_y", real(placement->dir.y));
            set(objects, row, "dir_z", real(placement->dir.z));
            if (placement->gravity) {
                F::Table& gravities = stage->tables.at("jmp/placement/common/planetobjinfo");
                const int g = copyRow(gravities, placement->gravity->first, placement->gravity->second);
                set(gravities, g, "pos_x", real(placement->gravityOffset.x));
                set(gravities, g, "pos_y", real(placement->gravityOffset.y));
                set(gravities, g, "pos_z", real(placement->gravityOffset.z));
                set(gravities, g, "l_id", integer(0));
                out.log.push_back(spec.planet + ": placed as in " + placement->from + ", with its " + getS(gravities, g, "name"));
            } else {
                out.log.push_back(spec.planet + ": placed as in " + placement->from + "; no gravity found beside it (add one)");
            }
            ground = bounds->second.y;  // the planet's top, unrotated: right for the upright planets
        } else if (spec.from == Template::Brushes) {
            const int row = addPlacementRow(objects, T::kBrushClass, 0, {0, -300, 0}, {3, 0.3f, 3});  // top face at y = 0
            set(objects, row, "Obj_arg0", integer(static_cast< int >(T::BrushShape::Box)));
            set(objects, row, "Obj_arg1", integer(0));
            set(objects, row, "Obj_arg2", integer(0));
            set(objects, row, "Obj_arg3", integer(1));
        }
        const std::string sky = placeSky(objects, spec.sky, kDefaultSkyFrom, readDisc, listDisc);
        out.log.push_back(sky.empty() ? std::string("no sky found on the disc") : "sky: " + sky);
        // Mario starts above the ground; Empty space has none (the Checks say so).
        const int mario = addPlacementRow(start, "Mario", 0, {0, ground + 600.0f, 0}, {1, 1, 1});
        set(start, mario, "MarioNo", integer(0));
        set(start, mario, "Camera_id", integer(-1));
        // A Power Star per mission (on its layer), 900 units from the start and spread round it (not
        // where the first steps go), except in empty space.
        if (spec.from != Template::Empty) {
            for (int m = 1; m <= missionCount; m++) {
                F::Table& layer = stage->table("jmp/placement/" + layerOf(m, missionCount) + "/objinfo", "jmp/placement/common/objinfo");
                const float angle = 3.14159265f * (1.0f + 0.4f * static_cast< float >(m - 1));
                const T::Vec3 at{900.0f * std::sin(angle), ground + 150.0f, 900.0f * std::cos(angle)};
                const int star = addPlacementRow(layer, "PowerStar", 100 + m, at, {1, 1, 1});
                set(layer, star, "Obj_arg0", integer(m));
            }
        }
        std::vector< std::pair< std::string, F::Bytes > > files = stage->files;
        for (auto& [path, table] : stage->tables) {
            files.push_back({path, table.build()});
        }
        out.files["StageData/" + G + ".arc"] = F::Archive::create("stage", files).build();

        // Scenarios: one row per mission, its star and its layer; one zone (the galaxy's own).
        const auto templateScenario = readDisc(std::string("StageData/") + kTemplateGalaxy + "/" + kTemplateGalaxy + "Scenario.arc");
        const auto scenario = templateScenario ? F::Archive::parse(*templateScenario) : std::nullopt;
        auto scenarios = scenario && scenario->file("scenariodata.bcsv") ? F::Table::parse(*scenario->file("scenariodata.bcsv")) : std::nullopt;
        auto zones = scenario && scenario->file("zonelist.bcsv") ? F::Table::parse(*scenario->file("zonelist.bcsv")) : std::nullopt;
        if (!scenarios || !zones || scenarios->rowCount() == 0 || zones->rowCount() == 0) {
            out.error = "the template galaxy's scenario tables are missing";
            return out;
        }
        while (scenarios->rowCount() > 1) {
            scenarios->removeRow(scenarios->rowCount() - 1);
        }
        while (zones->rowCount() > 1) {
            zones->removeRow(zones->rowCount() - 1);
        }
        const int zoneColumn = scenarios->addColumn(F::jmapHash(G.c_str()), F::ColumnType::Long, integer(0));
        for (int m = 1; m <= missionCount; m++) {
            const int row = m == 1 ? 0 : scenarios->addRow(0);
            set(*scenarios, row, "ScenarioNo", integer(m));
            set(*scenarios, row, "PowerStarId", integer(m));
            scenarios->set(row, zoneColumn, integer(missionCount <= 1 ? 0 : 1 << (m - 1)));
        }
        set(*zones, 0, "ZoneName", text(G));
        out.files[scenarioPath] =
            F::Archive::create(lower(G) + "scenario", {{"scenariodata.bcsv", scenarios->build()}, {"zonelist.bcsv", zones->build()}}).build();
    }

    // The registry entry (TECH.md 5.7): names in English (the other languages fall back to it).
    const std::string music = !spec.musicFrom.empty() ? spec.musicFrom : spec.from == Template::Copy ? spec.copyOf : "EggStarGalaxy";
    out.galaxyJson = galaxyJson(G, spec.name.empty() ? G : spec.name, missions, music);
    out.log.push_back(G + ": " + std::to_string(missions.size()) + (missions.size() == 1 ? " mission" : " missions") + ", music from " + music);
    return out;
}

std::string galaxyJson(const std::string& internalName, const std::string& name, const std::vector< std::string >& missions, const std::string& music) {
    std::string json = "{\n  \"format\": 1,\n  \"galaxies\": [ {\n    \"name\": " + jsonString(internalName) + ",\n    \"title\": { \"en\": " +
                       jsonString(name) + " },\n    \"missions\": [";
    for (std::size_t m = 0; m < missions.size(); m++) {
        json += std::string(m == 0 ? " " : ", ") + "{ \"en\": " + jsonString(missions[m]) + " }";
    }
    return json + " ],\n    \"music\": " + jsonString(music) + "\n  } ]\n}\n";
}

Formats::Bytes resizeScenario(const Formats::Bytes& scenarioArc, const std::string& internalName, int count, std::string* pError) {
    if (count < 1 || count > 8) {
        *pError = "a galaxy holds 1 to 8 missions";
        return {};
    }
    auto archive = F::Archive::parse(scenarioArc);
    auto scenarios = archive && archive->file("scenariodata.bcsv") ? F::Table::parse(*archive->file("scenariodata.bcsv")) : std::nullopt;
    if (!scenarios || scenarios->rowCount() == 0) {
        *pError = internalName + "'s scenario table doesn't parse";
        return {};
    }
    while (scenarios->rowCount() > count) {
        scenarios->removeRow(scenarios->rowCount() - 1);
    }
    const int zoneColumn = scenarios->addColumn(F::jmapHash(internalName.c_str()), F::ColumnType::Long, integer(0));
    while (scenarios->rowCount() < count) {
        const int m = scenarios->rowCount() + 1;
        const int row = scenarios->addRow(0);
        set(*scenarios, row, "ScenarioNo", integer(m));
        set(*scenarios, row, "PowerStarId", integer(m));
        scenarios->set(row, zoneColumn, integer(1 << (m - 1)));
    }
    archive->replace("scenariodata.bcsv", scenarios->build());
    return archive->build();
}

}  // namespace PetariEditor::Galaxy
