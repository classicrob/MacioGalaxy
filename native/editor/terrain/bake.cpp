// Baking terrain brushes. See petari/editor/terrain/bake.hpp.
#include <petari/editor/terrain/bake.hpp>
#include <petari/editor/terrain/brush.hpp>
#include <petari/editor/terrain/kcl_writer.hpp>
#include <petari/editor/terrain/model_writer.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace PetariEditor::Terrain {

namespace {
    namespace F = Formats;

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

    int column(const F::Table& table, const char* pName) {
        return table.column(F::jmapHash(pName));
    }
    float getFloat(const F::Table& table, int row, const char* pName, float fallback) {
        const int c = column(table, pName);
        return c >= 0 ? table.get(row, c).f : fallback;
    }
    std::int32_t getInt(const F::Table& table, int row, const char* pName) {
        const int c = column(table, pName);
        return c >= 0 ? table.get(row, c).i : -1;
    }
    void set(F::Table& table, int row, const char* pName, const F::CellValue& value) {
        const int c = column(table, pName);
        if (c >= 0) {
            table.set(row, c, value);
        }
    }

    std::string lower(std::string s) {
        for (char& c : s) {
            c = static_cast< char >(std::tolower(static_cast< unsigned char >(c)));
        }
        return s;
    }

    std::uint32_t fnv(const std::string& s, std::uint32_t h = 2166136261u) {
        for (unsigned char c : s) {
            h = (h ^ c) * 16777619u;
        }
        return h;
    }

    // The game's euler rotation (MR::makeMtxTR, degrees): column 1 is the brush's up axis.
    void upAxis(float rx, float ry, float rz, float* pOut) {
        const float d = 3.14159265358979f / 180.0f;
        const float sx = std::sin(rx * d), sy = std::sin(ry * d), sz = std::sin(rz * d);
        const float cx = std::cos(rx * d), cy = std::cos(ry * d), cz = std::cos(rz * d);
        pOut[0] = cz * sy * sx - sz * cx;
        pOut[1] = sz * sy * sx + cz * cx;
        pOut[2] = cy * sx;
    }

    // A new row with every int column -1, no rotation, scale 1.
    int addGravityRow(F::Table& table, const std::string& name, float x, float y, float z, float rx, float ry, float rz,
                      float sx, float sy, float sz, float range, float distant) {
        const int row = table.addRow(-1);
        for (const F::TableColumn& c : table.columns()) {
            if (c.type != F::ColumnType::Float && c.type != F::ColumnType::InlineString && c.type != F::ColumnType::StringOffset) {
                table.set(row, table.column(c.hash), integer(-1));
            }
        }
        set(table, row, "name", text(name));
        set(table, row, "pos_x", real(x));
        set(table, row, "pos_y", real(y));
        set(table, row, "pos_z", real(z));
        set(table, row, "dir_x", real(rx));
        set(table, row, "dir_y", real(ry));
        set(table, row, "dir_z", real(rz));
        set(table, row, "scale_x", real(sx));
        set(table, row, "scale_y", real(sy));
        set(table, row, "scale_z", real(sz));
        set(table, row, "Range", real(range));
        set(table, row, "Distant", real(distant));
        set(table, row, "Priority", integer(1000));
        set(table, row, "Inverse", integer(0));
        set(table, row, "Power", text("Normal"));
        set(table, row, "Gravity_type", text("Normal"));
        return row;
    }

    struct Donors {
        const BakeInput* input;
        std::map< int, MaterialClone > clones;
        const MaterialClone* get(int index, std::string* pError) {
            auto it = clones.find(index);
            if (it != clones.end()) {
                return &it->second;
            }
            const MaterialPreset& preset = materialPalette()[static_cast< std::size_t >(index)];
            const auto raw = input->readDisc("/ObjectData/" + preset.donorArchive + ".arc");
            const auto archive = raw ? F::Archive::parse(*raw) : std::nullopt;
            const F::Bytes* pModel = archive ? archive->file(preset.donorModel) : nullptr;
            if (pModel == nullptr) {
                *pError = "the material " + preset.label + " needs " + preset.donorArchive + " from the disc";
                return nullptr;
            }
            auto clone = cloneMaterial(*pModel, preset.material, pError);
            return clone ? &clones.emplace(index, std::move(*clone)).first->second : nullptr;
        }
    };
}  // namespace

BakeOutput bakeBrushes(const BakeInput& input) {
    BakeOutput out;
    Donors donors{&input, {}};
    std::vector< std::string > planets;
    std::map< std::string, int > nameUses;

    for (const auto& [path, raw] : input.stageArchives) {
        auto archive = F::Archive::parse(raw);
        if (!archive) {
            out.error = path + " is not an archive";
            return out;
        }
        // Tables changed in this archive, and gravity rows to add per layer.
        std::map< std::string, F::Table > tables;
        int brushesHere = 0;
        for (const std::string& file : archive->files()) {
            if (file.size() < 8 || file.compare(file.size() - 8, 8, "/objinfo") != 0) {
                continue;
            }
            auto objects = F::Table::parse(*archive->file(file));
            const int nameColumn = objects ? column(*objects, "name") : -1;
            if (nameColumn < 0) {
                continue;
            }
            bool changed = false;
            for (int row = 0; row < objects->rowCount(); row++) {
                if (objects->get(row, nameColumn).s != kBrushClass) {
                    continue;
                }
                const float px = getFloat(*objects, row, "pos_x", 0), py = getFloat(*objects, row, "pos_y", 0), pz = getFloat(*objects, row, "pos_z", 0);
                const float rx = getFloat(*objects, row, "dir_x", 0), ry = getFloat(*objects, row, "dir_y", 0), rz = getFloat(*objects, row, "dir_z", 0);
                const Vec3 scale{getFloat(*objects, row, "scale_x", 1), getFloat(*objects, row, "scale_y", 1), getFloat(*objects, row, "scale_z", 1)};
                const std::int32_t args[4] = {getInt(*objects, row, "Obj_arg0"), getInt(*objects, row, "Obj_arg1"), getInt(*objects, row, "Obj_arg2"),
                                              getInt(*objects, row, "Obj_arg3")};
                const Brush brush = brushFromRow(args[0], args[1], args[2], args[3], scale);

                // A stable, unique name from where and what the brush is.
                char key[256];
                std::snprintf(key, sizeof(key), "%s|%s|%.1f|%.1f|%.1f|%.2f|%.2f|%.2f|%.3f|%.3f|%.3f|%d|%d|%d|%d", path.c_str(), file.c_str(), px, py,
                              pz, rx, ry, rz, scale.x, scale.y, scale.z, args[0], args[1], args[2], args[3]);
                char hash[16];
                std::snprintf(hash, sizeof(hash), "%08X", fnv(key));
                std::string planet = std::string("EditorBrush") + hash;
                if (const int uses = nameUses[planet]++) {
                    planet += "_" + std::to_string(uses);  // two identical brushes in one place
                }

                // The planet archive: model, collision, attributes.
                const Mesh mesh = brushMesh(brush);
                const KclResult kcl = buildKcl(mesh);
                if (!kcl.error.empty()) {
                    out.error = planet + ": " + kcl.error;
                    return out;
                }
                std::string error;
                const MaterialClone* pMaterial = donors.get(brush.material, &error);
                if (pMaterial == nullptr) {
                    out.error = planet + ": " + error;
                    return out;
                }
                const ModelResult model = buildModel(mesh, *pMaterial);
                if (!model.error.empty()) {
                    out.error = planet + ": " + model.error;
                    return out;
                }
                const std::string base = lower(planet);
                out.files["/ObjectData/" + planet + ".arc"] =
                    F::Archive::create(base, {{base + ".bmd", model.model}, {base + ".kcl", kcl.kcl}, {base + ".pa", kcl.pa}}).build();
                planets.push_back(planet);

                // The row becomes the planet, at its size already.
                objects->set(row, nameColumn, text(planet));
                set(*objects, row, "scale_x", real(1));
                set(*objects, row, "scale_y", real(1));
                set(*objects, row, "scale_z", real(1));
                for (const char* pArg : {"Obj_arg0", "Obj_arg1", "Obj_arg2", "Obj_arg3"}) {
                    set(*objects, row, pArg, integer(-1));
                }
                changed = true;

                // Its gravity, in the same layer.
                const GravityFit fit = fitGravity(brush);
                if (fit.kind != GravityFit::Kind::None) {
                    const std::string layer = file.substr(0, file.size() - 8);  // ".../placement/<layer>"
                    const std::string gravityPath = layer + "/planetobjinfo";
                    auto it = tables.find(gravityPath);
                    if (it == tables.end()) {
                        const F::Bytes* pExisting = archive->file(gravityPath);
                        const F::Bytes* pSchema = pExisting ? pExisting : archive->file("jmp/placement/common/planetobjinfo");
                        auto table = pSchema ? F::Table::parse(*pSchema) : std::nullopt;
                        if (!table) {
                            out.error = path + " has no PlanetObjInfo to copy for the brush's gravity";
                            return out;
                        }
                        if (!pExisting) {
                            while (table->rowCount() > 0) {
                                table->removeRow(table->rowCount() - 1);
                            }
                        }
                        it = tables.emplace(gravityPath, std::move(*table)).first;
                    }
                    float up[3];
                    upAxis(rx, ry, rz, up);
                    switch (fit.kind) {
                    case GravityFit::Kind::Cube: {
                        // GlobalCubeGravity: the row's position is the cube's bottom centre (the
                        // brush's), its scale the size / 1000; Obj_arg0-2 select faces per axis.
                        const int g = addGravityRow(it->second, "GlobalCubeGravity", px, py, pz, rx, ry, rz, fit.half.x / 500.0f,
                                                    fit.half.y / 500.0f, fit.half.z / 500.0f, fit.range, 0.0f);
                        const bool all = fit.activeFaces == 0x3F;
                        set(it->second, g, "Obj_arg0", integer(all ? 3 : 0));
                        set(it->second, g, "Obj_arg1", integer(all ? 3 : 1));  // bit 1: +y only
                        set(it->second, g, "Obj_arg2", integer(all ? 3 : 0));
                        break;
                    }
                    case GravityFit::Kind::Point:
                        // GlobalPointGravity at the sphere's centre; Distant is its radius.
                        addGravityRow(it->second, "GlobalPointGravity", px + up[0] * fit.radius, py + up[1] * fit.radius,
                                      pz + up[2] * fit.radius, 0, 0, 0, fit.radius / 500.0f, fit.radius / 500.0f, fit.radius / 500.0f,
                                      fit.range, fit.radius);
                        break;
                    case GravityFit::Kind::Segment: {
                        // GlobalSegmentGravity: from the row's position along its up axis for
                        // 1000 * scale_y; Obj_arg0 3 = both ends valid, Obj_arg1 -1 = all round.
                        const float height = fit.b.y - fit.a.y;
                        const int g = addGravityRow(it->second, "GlobalSegmentGravity", px, py, pz, rx, ry, rz, 1.0f, height / 1000.0f, 1.0f,
                                                    fit.range, fit.radius);
                        set(it->second, g, "Obj_arg0", integer(3));
                        break;
                    }
                    default:
                        break;
                    }
                }
                out.log.push_back(planet + ": " + brushShapeNames()[static_cast< std::size_t >(brush.shape)] + " " +
                                  materialPalette()[static_cast< std::size_t >(brush.material)].label + ", " + std::to_string(kcl.stats.triangles) +
                                  " triangles, in " + path + " " + file);
                brushesHere++;
                out.brushes++;
            }
            if (changed) {
                tables.emplace(file, std::move(*objects));
            }
        }
        if (brushesHere == 0) {
            continue;
        }
        // Rewrite the archive: changed tables replaced, new layer tables added (a rebuild, since
        // the archive format keeps its folders and files as they were).
        bool added = false;
        for (const auto& [file, table] : tables) {
            if (!archive->has(file)) {
                added = true;
            }
        }
        if (!added) {
            for (auto& [file, table] : tables) {
                archive->replace(file, table.build());
            }
            out.files[path] = archive->build();
        } else {
            std::vector< std::pair< std::string, F::Bytes > > files;
            for (const std::string& file : archive->files()) {
                auto it = tables.find(file);
                files.push_back({file, it != tables.end() ? it->second.build() : *archive->file(file)});
            }
            for (const auto& [file, table] : tables) {
                if (!archive->has(file)) {
                    files.push_back({file, table.build()});
                }
            }
            out.files[path] = F::Archive::create("stage", files).build();
        }
    }
    if (planets.empty()) {
        return out;
    }

    // Register the planets.
    const auto rawTable = input.readDisc("/ObjectData/PlanetMapDataTable.arc");
    auto planetArchive = rawTable ? F::Archive::parse(*rawTable) : std::nullopt;
    const F::Bytes* pPlanetTable = planetArchive ? planetArchive->file("planetmapdatatable.bcsv") : nullptr;
    auto planetTable = pPlanetTable ? F::Table::parse(*pPlanetTable) : std::nullopt;
    if (!planetTable || planetTable->rowCount() == 0) {
        out.error = "PlanetMapDataTable.arc is missing from the disc";
        out.files.clear();
        return out;
    }
    // Planets of an earlier bake of this mod (read back through its files) are replaced.
    const int nameColumn = column(*planetTable, "PlanetName");
    for (int row = planetTable->rowCount() - 1; row >= 0 && nameColumn >= 0; row--) {
        if (planetTable->get(row, nameColumn).s.rfind("EditorBrush", 0) == 0) {
            planetTable->removeRow(row);
        }
    }
    for (const std::string& planet : planets) {
        const int row = planetTable->addRow(0);
        for (const char* pFlag : {"LowFlag", "MiddleFlag", "BloomFlag", "WaterFlag", "IndirectFlag"}) {
            set(*planetTable, row, pFlag, integer(0));
        }
        for (int i = 0; i < 8; i++) {
            set(*planetTable, row, ("ForceLowScenarioName" + std::to_string(i)).c_str(), text(""));
        }
        set(*planetTable, row, "PlanetName", text(planet));
    }
    planetArchive->replace("planetmapdatatable.bcsv", planetTable->build());
    out.files["/ObjectData/PlanetMapDataTable.arc"] = planetArchive->build();
    return out;
}

bool bakePatchAssets(const std::filesystem::path& modRoot, const Patch::DiscReader& readDisc, std::vector< Patch::Built >* pInOut,
                     std::vector< std::string >* pLog, std::string* pError) {
    BakeInput input;
    for (const Patch::Built& built : *pInOut) {
        if (built.path.rfind("StageData/", 0) == 0) {
            input.stageArchives["/" + built.path] = built.bytes;
        }
    }
    // Built files first: a table an earlier call extended (another mod's brushes) is kept.
    input.readDisc = [&](const std::string& path) -> std::optional< Formats::Bytes > {
        const std::string relative = path.substr(1);
        for (const Patch::Built& built : *pInOut) {
            if (built.path == relative) {
                return built.bytes;
            }
        }
        return readDisc(relative);
    };
    const BakeOutput baked = bakeBrushes(input);
    if (!baked.error.empty()) {
        *pError = "terrain: " + baked.error;
        return false;
    }
    const std::string mod = modRoot.filename().string();
    for (const std::string& line : baked.log) {
        if (pLog != nullptr) {
            pLog->push_back("terrain: " + line);
        }
    }
    for (const auto& [path, bytes] : baked.files) {
        const std::string relative = path.substr(1);
        auto it = std::find_if(pInOut->begin(), pInOut->end(), [&](const Patch::Built& b) { return b.path == relative; });
        if (it == pInOut->end()) {
            pInOut->push_back(Patch::Built{relative, bytes, {mod}});
            continue;
        }
        it->bytes = bytes;
        if (std::find(it->mods.begin(), it->mods.end(), mod) == it->mods.end()) {
            it->mods.push_back(mod);
        }
    }
    return true;
}

void registerPatchBuilder() {
    static bool sRegistered = false;
    if (!sRegistered) {
        sRegistered = true;
        // Change the version when the baked files change, so cached builds are made again.
        Patch::registerAssetBuilder("terrain-bake-4", &bakePatchAssets);
    }
}

}  // namespace PetariEditor::Terrain
