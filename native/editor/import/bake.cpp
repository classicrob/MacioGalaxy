#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <petari/editor/import_assets.hpp>
#include <set>
#include <sstream>

namespace PetariEditor::Imports {
    namespace {
        namespace F = Formats;
        F::CellValue integer(int v) {
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
        int col(const F::Table& t, const char* name) {
            return t.column(F::jmapHash(name));
        }
        void set(F::Table& t, int r, const char* n, const F::CellValue& v) {
            const int c = col(t, n);
            if (c >= 0) {
                t.set(r, c, v);
            }
        }
        float get(const F::Table& t, int r, const char* n, float fallback) {
            const int c = col(t, n);
            return c >= 0 ? t.get(r, c).f : fallback;
        }
        std::string lower(std::string s) {
            for (char& c : s) {
                c = std::tolower(static_cast< unsigned char >(c));
            }
            return s;
        }
        void gravity(F::Table& t, const F::Table& objects, int row, Vec3 lo, Vec3 hi) {
            const int r = t.addRow(-1);
            for (const auto& c : t.columns()) {
                if (c.type != F::ColumnType::Float && c.type != F::ColumnType::InlineString && c.type != F::ColumnType::StringOffset) {
                    t.set(r, t.column(c.hash), integer(-1));
                }
            }
            const float d = 3.14159265359f / 180;
            const float rx = get(objects, row, "dir_x", 0), ry = get(objects, row, "dir_y", 0), rz = get(objects, row, "dir_z", 0);
            const float sx = std::sin(rx * d), sy = std::sin(ry * d), sz = std::sin(rz * d), cx = std::cos(rx * d), cy = std::cos(ry * d),
                        cz = std::cos(rz * d);
            // Cube creator takes the bottom centre, in the placement's local frame.
            const Vec3 p{(lo.x + hi.x) / 2, lo.y, (lo.z + hi.z) / 2};
            set(t, r, "name", text("GlobalCubeGravity"));
            set(t, r, "pos_x", real(get(objects, row, "pos_x", 0) + cz * cy * p.x + (cz * sy * sx - sz * cx) * p.y + (cz * sy * cx + sz * sx) * p.z));
            set(t, r, "pos_y", real(get(objects, row, "pos_y", 0) + sz * cy * p.x + (sz * sy * sx + cz * cx) * p.y + (sz * sy * cx - cz * sx) * p.z));
            set(t, r, "pos_z", real(get(objects, row, "pos_z", 0) - sy * p.x + cy * sx * p.y + cy * cx * p.z));
            set(t, r, "dir_x", real(rx));
            set(t, r, "dir_y", real(ry));
            set(t, r, "dir_z", real(rz));
            set(t, r, "scale_x", real(std::max(1.f, hi.x - lo.x) / 1000));
            set(t, r, "scale_y", real(std::max(1.f, hi.y - lo.y) / 1000));
            set(t, r, "scale_z", real(std::max(1.f, hi.z - lo.z) / 1000));
            set(t, r, "Obj_arg0", integer(0));
            set(t, r, "Obj_arg1", integer(1));
            set(t, r, "Obj_arg2", integer(0));
            set(t, r, "Range", real(3000));
            set(t, r, "Distant", real(0));
            set(t, r, "Priority", integer(1000));
            set(t, r, "Inverse", integer(0));
            set(t, r, "Power", text("Normal"));
            set(t, r, "Gravity_type", text("Normal"));
        }
        Bytes readFile(const std::filesystem::path& path) {
            std::error_code ec;
            const auto size = std::filesystem::file_size(path, ec);
            if (ec || size > 256 * 1024 * 1024) {
                return {};
            }
            std::ifstream in(path, std::ios::binary);
            return Bytes(std::istreambuf_iterator< char >(in), {});
        }
    }  // namespace
    Terrain::BakeOutput bake(const Terrain::BakeInput& input, const AssetReader& assets) {
        Terrain::BakeOutput out;
        struct Flags {
            bool middle = false, low = false;
        };
        std::map< std::string, Flags > planets;
        for (const auto& [path, bytes] : input.stageArchives) {
            auto archive = F::Archive::parse(bytes);
            if (!archive) {
                out.error = path + " is not an archive";
                return out;
            }
            std::map< std::string, F::Table > tables;
            for (const auto& file : archive->files()) {
                if (file.size() < 8 || file.compare(file.size() - 8, 8, "/objinfo")) {
                    continue;
                }
                auto objects = F::Table::parse(*archive->file(file));
                const int names = objects ? col(*objects, "name") : -1;
                if (names < 0) {
                    continue;
                }
                bool changed = false;
                for (int row = 0; row < objects->rowCount(); ++row) {
                    std::string id;
                    int index = 0;
                    const auto className = objects->get(row, names).s;
                    if (!parseClassName(className.c_str(), &id, &index)) {
                        continue;
                    }
                    auto asset = assets(id);
                    if (!asset) {
                        out.error = "Missing imported asset " + id;
                        return out;
                    }
                    const Vec3 scale{get(*objects, row, "scale_x", 1), get(*objects, row, "scale_y", 1), get(*objects, row, "scale_z", 1)};
                    std::ostringstream key;
                    key << className << std::hexfloat << scale.x << '/' << scale.y << '/' << scale.z;
                    const std::string keyText = key.str();
                    const std::string planet = "EditorImport" + contentId(Bytes(keyText.begin(), keyText.end())).substr(0, 16);
                    const auto built = buildPart(*asset, index, scale, input.readDisc);
                    if (!built.error.empty()) {
                        out.error = asset->name + ": " + built.error;
                        return out;
                    }
                    const auto base = lower(planet);
                    std::vector< std::pair< std::string, Bytes > > files{{base + ".bmd", built.model}};
                    if (!built.kcl.empty()) {
                        files.push_back({base + ".kcl", built.kcl});
                        files.push_back({base + ".pa", built.pa});
                    }
                    out.files["/ObjectData/" + planet + ".arc"] = F::Archive::create(base, files).build();
                    planets[planet] = {!built.middle.empty(), !built.low.empty()};
                    for (const auto& lod : {std::make_pair(std::string("Middle"), &built.middle), std::make_pair(std::string("Low"), &built.low)}) {
                        if (!lod.second->empty()) {
                            out.files["/ObjectData/" + planet + lod.first + ".arc"] =
                                F::Archive::create(base + lower(lod.first), {{base + lower(lod.first) + ".bmd", *lod.second}}).build();
                        }
                    }
                    if (asset->gravity && !asset->parts[index].collision.indices.empty()) {
                        const auto gravityPath = file.substr(0, file.size() - 8) + "/planetobjinfo";
                        if (!tables.count(gravityPath)) {
                            const auto* existing = archive->file(gravityPath);
                            const auto* schema = existing ? existing : archive->file("jmp/placement/common/planetobjinfo");
                            auto table = schema ? F::Table::parse(*schema) : std::nullopt;
                            if (!table) {
                                out.error = "No PlanetObjInfo schema for imported gravity.";
                                return out;
                            }
                            if (!existing) {
                                while (table->rowCount()) {
                                    table->removeRow(table->rowCount() - 1);
                                }
                            }
                            tables.emplace(gravityPath, std::move(*table));
                        }
                        gravity(tables.at(gravityPath), *objects, row, built.minimum, built.maximum);
                    }
                    objects->set(row, names, text(planet));
                    for (const char* field : {"scale_x", "scale_y", "scale_z"}) {
                        set(*objects, row, field, real(1));
                    }
                    changed = true;
                    ++out.brushes;
                    out.log.push_back(asset->name + " part " + std::to_string(index + 1) + " -> " + planet);
                }
                if (changed) {
                    tables.emplace(file, std::move(*objects));
                }
            }
            if (!tables.empty()) {
                std::vector< std::pair< std::string, Bytes > > files;
                for (const auto& file : archive->files()) {
                    files.push_back({file, tables.count(file) ? tables.at(file).build() : *archive->file(file)});
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
        const auto raw = input.readDisc("/ObjectData/PlanetMapDataTable.arc");
        auto archive = raw ? F::Archive::parse(*raw) : std::nullopt;
        const auto* bytes = archive ? archive->file("planetmapdatatable.bcsv") : nullptr;
        auto table = bytes ? F::Table::parse(*bytes) : std::nullopt;
        if (!table || !table->rowCount()) {
            out.error = "PlanetMapDataTable is missing.";
            return out;
        }
        const int nameColumn = col(*table, "PlanetName");
        if (nameColumn < 0) {
            out.error = "PlanetMapDataTable has no PlanetName column.";
            return out;
        }
        for (int r = table->rowCount() - 1; r >= 0; --r) {
            if (planets.count(table->get(r, nameColumn).s)) {
                table->removeRow(r);
            }
        }
        for (const auto& [name, flags] : planets) {
            const int r = table->addRow(table->rowCount() ? 0 : -1);
            for (const char* f : {"LowFlag", "MiddleFlag", "BloomFlag", "WaterFlag", "IndirectFlag"}) {
                set(*table, r, f, integer(0));
            }
            set(*table, r, "LowFlag", integer(flags.low));
            set(*table, r, "MiddleFlag", integer(flags.middle));
            for (int i = 0; i < 8; ++i) {
                set(*table, r, ("ForceLowScenarioName" + std::to_string(i)).c_str(), text(""));
            }
            set(*table, r, "PlanetName", text(name));
        }
        archive->replace("planetmapdatatable.bcsv", table->build());
        out.files["/ObjectData/PlanetMapDataTable.arc"] = archive->build();
        return out;
    }
    bool bakePatchAssets(const std::filesystem::path& root, const Patch::DiscReader& disc, std::vector< Patch::Built >* files,
                         std::vector< std::string >* log, std::string* error) {
        Terrain::BakeInput input;
        for (const auto& f : *files) {
            if (f.path.rfind("StageData/", 0) == 0) {
                input.stageArchives["/" + f.path] = f.bytes;
            }
        }
        std::error_code ec;
        for (std::filesystem::directory_iterator it(root / "files" / "StageData", ec), end; !ec && it != end; it.increment(ec)) {
            const std::string path = "/StageData/" + it->path().filename().string();
            if (it->path().extension() == ".arc" && !input.stageArchives.count(path)) {
                input.stageArchives[path] = readFile(it->path());
            }
        }
        input.readDisc = [&](const std::string& path) -> std::optional< Bytes > {
            for (const auto& f : *files) {
                if ("/" + f.path == path) {
                    return f.bytes;
                }
            }
            return disc(path.substr(1));
        };
        std::map< std::string, std::shared_ptr< const Asset > > cache;
        const auto result = bake(input, [&](const std::string& id) -> std::shared_ptr< const Asset > {
            if (cache.count(id)) {
                return cache[id];
            }
            std::string why;
            auto bytes = readFile(root / "assets" / "models" / (id + ".pmodel"));
            // Row patches are merged before builders run. Resolve assets from every contributing
            // mod, so a first mod's bake can also see a later mod's imported rows.
            if (bytes.empty()) {
                for (const auto& file : *files) {
                    for (const auto& mod : file.mods) {
                        if (std::filesystem::path(mod).filename() != mod) {
                            continue;
                        }
                        bytes = readFile(root.parent_path() / mod / "assets" / "models" / (id + ".pmodel"));
                        if (!bytes.empty()) {
                            break;
                        }
                    }
                    if (!bytes.empty()) {
                        break;
                    }
                }
            }
            if (contentId(bytes) != id) {
                return {};
            }
            auto asset = deserialize(bytes, &why);
            if (!asset) {
                return {};
            }
            return cache[id] = std::make_shared< Asset >(std::move(*asset));
        });
        if (!result.error.empty()) {
            *error = "Model import: " + result.error;
            return false;
        }
        if (log) {
            log->insert(log->end(), result.log.begin(), result.log.end());
        }
        for (const auto& [path, bytes] : result.files) {
            const auto relative = path.substr(1);
            auto it = std::find_if(files->begin(), files->end(), [&](const auto& f) { return f.path == relative; });
            if (it == files->end()) {
                files->push_back({relative, bytes, {root.filename().string()}});
            } else {
                it->bytes = bytes;
                if (std::find(it->mods.begin(), it->mods.end(), root.filename().string()) == it->mods.end()) {
                    it->mods.push_back(root.filename().string());
                }
            }
        }
        return true;
    }
    void registerPatchBuilder() {
        static const bool once = (Patch::registerAssetBuilder("model-import-1", &bakePatchAssets), true);
        (void)once;
    }
}  // namespace PetariEditor::Imports
