#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <petari/editor/catalog.hpp>
#include <petari/editor/check_providers.hpp>
#include <petari/editor/import_assets.hpp>
#include <set>
#include <utility>

namespace PetariEditor::Imports {
    namespace {
        std::map< std::string, std::shared_ptr< const Asset > > gAssets;
        std::filesystem::path gLibrary;
        std::mutex gRequestMutex;
        std::shared_ptr< const Asset > gRequest;
        bool write(const std::filesystem::path& path, const Bytes& bytes, std::string* error) {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            const auto temporary = path.string() + ".tmp";
            {
                std::ofstream out(temporary, std::ios::binary);
                out.write(reinterpret_cast< const char* >(bytes.data()), bytes.size());
                if (!out) {
                    *error = "Cannot write imported asset to " + path.string();
                    return false;
                }
            }
            std::filesystem::rename(temporary, path, ec);
            if (ec) {
                *error = "Cannot save imported asset: " + ec.message();
                return false;
            }
            return true;
        }
        void addCatalog(const std::string& id, const Asset& asset) {
            for (size_t i = 0; i < asset.parts.size(); ++i) {
                Catalog::ClassInfo c;
                c.name = className(id, int(i));
                c.displayName = asset.name;
                if (asset.parts.size() > 1 && i > 0) {
                    c.displayName += " / " + std::to_string(i + 1);
                }
                c.description = "Imported model, " + std::to_string(asset.parts[i].mesh.triangleCount()) + " triangles";
                c.aliases = {"import", "custom", asset.name};
                c.category = Catalog::Category::Planet;
                c.curated = true;
                c.keepable = true;
                c.hasCollision = !asset.parts[i].collision.indices.empty();
                c.liveRecreate = "safe";
                Catalog::addClass(std::move(c));
            }
        }
        void checks(const ModelView& model, std::vector< Check >& out) {
            std::set< std::string > distinct;
            std::vector< RowId > allRows;
            for (int z = 0; z < model.zoneCount(); ++z) {
                size_t triangles = 0;
                std::vector< RowId > rows;
                for (int t = 0; t < model.tableCount(z); ++t) {
                    model.forEachRow(z, t, [&](RowId row) {
                        if (model.state(row) == RowState::Deleted) {
                            return;
                        }
                        std::string id;
                        int part = 0;
                        if (!parseClassName(model.objectName(row), &id, &part)) {
                            return;
                        }
                        auto asset = findAsset(id);
                        if (!asset || part >= int(asset->parts.size())) {
                            Check c;
                            c.key = "import-missing:" + std::to_string(row.v);
                            c.message = "Imported model asset is missing";
                            c.rows = {row};
                            c.severity = Severity::Error;
                            out.push_back(std::move(c));
                        } else {
                            distinct.insert(className(id, part));
                            allRows.push_back(row);
                            if (model.rowMissions(row) & (1u << (model.scenario() - 1))) {
                                triangles += asset->parts[part].mesh.triangleCount();
                                rows.push_back(row);
                            }
                        }
                    });
                }
                if (triangles > 64000) {
                    Check c;
                    c.key = "import-budget:" + std::string(model.zoneName(z)) + ":" + std::to_string(model.scenario());
                    c.message = "Imported geometry exceeds 64,000 triangles in this zone";
                    c.rows = std::move(rows);
                    out.push_back(std::move(c));
                }
            }
            if (distinct.size() > 64) {
                Check c;
                c.key = "import-model-capacity";
                c.message = "More than 64 distinct imported planet parts in this galaxy";
                c.rows = std::move(allRows);
                out.push_back(std::move(c));
            }
        }
    }  // namespace
    void loadLibrary(const std::string& directory, const std::vector< std::filesystem::path >& roots) {
        gLibrary = std::filesystem::path(directory) / "editor" / "imports";
        gAssets.clear();
        std::vector< std::filesystem::path > folders{gLibrary};
        for (const auto& root : roots) {
            folders.push_back(root / "assets" / "models");
        }
        for (const auto& folder : folders) {
            std::error_code ec;
            for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
                if (it->path().extension() != ".pmodel") {
                    continue;
                }
                const auto size = it->file_size(ec);
                if (ec || size > 256 * 1024 * 1024) {
                    continue;
                }
                std::ifstream in(it->path(), std::ios::binary);
                Bytes bytes(std::istreambuf_iterator< char >(in), {});
                const auto id = contentId(bytes);
                if (id != it->path().stem().string()) {
                    continue;
                }
                std::string error;
                auto asset = deserialize(bytes, &error);
                if (asset) {
                    gAssets[id] = std::make_shared< Asset >(std::move(*asset));
                }
            }
        }
        registerClasses();
    }
    std::shared_ptr< const Asset > findAsset(const std::string& id) {
        const auto found = gAssets.find(id);
        return found == gAssets.end() ? nullptr : found->second;
    }
    std::string storeAsset(const Asset& asset, std::string* error) {
        const auto bytes = serialize(asset);
        auto checked = deserialize(bytes, error);
        if (!checked) {
            return {};
        }
        if (gLibrary.empty()) {
            *error = "Open a galaxy before importing a model.";
            return {};
        }
        const auto id = contentId(bytes);
        if (!write(gLibrary / (id + ".pmodel"), bytes, error)) {
            return {};
        }
        gAssets[id] = std::make_shared< Asset >(std::move(*checked));
        addCatalog(id, *gAssets[id]);
        return id;
    }
    bool saveAssets(const ModelView& model, const std::filesystem::path& root, std::string* error) {
        std::set< std::string > ids;
        for (int z = 0; z < model.zoneCount(); ++z) {
            for (int t = 0; t < model.tableCount(z); ++t) {
                model.forEachRow(z, t, [&](RowId row) {
                    std::string id;
                    if (model.state(row) != RowState::Deleted && parseClassName(model.objectName(row), &id)) {
                        ids.insert(id);
                    }
                });
            }
        }
        for (const auto& id : ids) {
            const auto asset = findAsset(id);
            if (!asset) {
                *error = "Missing imported model " + id;
                return false;
            }
            if (!write(root / "assets" / "models" / (id + ".pmodel"), serialize(*asset), error)) {
                return false;
            }
        }
        return true;
    }
    void registerClasses() {
        for (const auto& [id, asset] : gAssets) {
            addCatalog(id, *asset);
        }
        registerCheckProvider(checks);
    }
    void appendPlacementParts(Op& op, const AddRow& first) {
        const auto name = std::find_if(first.cells.begin(), first.cells.end(), [](const auto& c) { return c.column == columnHash("name"); });
        if (name == first.cells.end()) {
            return;
        }
        std::string id;
        int part = 0;
        if (!parseClassName(name->value.s.c_str(), &id, &part) || part != 0) {
            return;
        }
        const auto asset = findAsset(id);
        if (!asset) {
            return;
        }
        for (size_t i = 1; i < asset->parts.size(); ++i) {
            AddRow add = first;
            add.id = newRowId();
            for (auto& cell : add.cells) {
                if (cell.column == columnHash("name")) {
                    cell.value.s = className(id, i);
                }
            }
            op.adds.push_back(std::move(add));
        }
    }
    void requestPlacement(std::shared_ptr< const Asset > asset) {
        std::lock_guard< std::mutex > lock(gRequestMutex);
        gRequest = std::move(asset);
    }
    std::shared_ptr< const Asset > takePlacement() {
        std::lock_guard< std::mutex > lock(gRequestMutex);
        return std::exchange(gRequest, {});
    }
}  // namespace PetariEditor::Imports
