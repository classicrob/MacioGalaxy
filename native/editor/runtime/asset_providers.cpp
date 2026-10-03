#include <algorithm>
#include <cstring>
#include <petari/editor/asset_providers.hpp>
#include <petari/editor/import_assets.hpp>
#include <petari/editor/terrain/brush_catalog.hpp>

namespace PetariEditor::Assets {
    namespace {
        std::vector< Provider >& providers() {
            static std::vector< Provider > entries;
            return entries;
        }
        void brushLoad(const std::string&, const std::vector< std::filesystem::path >&) {
            Terrain::registerBrushClass();
            Terrain::registerBrushChecks();
        }
        Terrain::BakeOutput importBake(const Terrain::BakeInput& input) {
            return Imports::bake(input, Imports::findAsset);
        }
        bool brushFile(const std::string& name) {
            return name.rfind("EditorBrush", 0) == 0;
        }
        bool importFile(const std::string& name) {
            return name.rfind("EditorImport", 0) == 0;
        }
        void defaults() {
            registerProvider({"terrain", brushLoad, nullptr, Terrain::bakeBrushes, brushFile});
            registerProvider({"imports", Imports::loadLibrary, Imports::saveAssets, importBake, importFile});
        }
    }  // namespace
    void registerProvider(Provider provider) {
        if (!provider.name) {
            return;
        }
        const auto it =
            std::find_if(providers().begin(), providers().end(), [&](const Provider& p) { return std::strcmp(p.name, provider.name) == 0; });
        if (it == providers().end()) {
            providers().push_back(provider);
        }
    }
    void load(const std::string& user, const std::vector< std::filesystem::path >& roots) {
        defaults();
        for (const auto& p : providers()) {
            if (p.load) {
                p.load(user, roots);
            }
        }
    }
    bool save(const ModelView& model, const std::filesystem::path& root, std::string* error) {
        defaults();
        for (const auto& p : providers()) {
            if (p.save && !p.save(model, root, error)) {
                return false;
            }
        }
        return true;
    }
    Terrain::BakeOutput bake(const Terrain::BakeInput& input) {
        defaults();
        Terrain::BakeOutput out;
        for (const auto& p : providers()) {
            if (!p.bake) {
                continue;
            }
            auto next = input;
            for (const auto& [path, bytes] : out.files) {
                if (path.rfind("/StageData/", 0) == 0) {
                    next.stageArchives[path] = bytes;
                }
            }
            next.readDisc = [&](const std::string& path) -> std::optional< Formats::Bytes > {
                const auto it = out.files.find(path);
                return it != out.files.end() ? std::optional< Formats::Bytes >(it->second) : input.readDisc(path);
            };
            auto result = p.bake(next);
            if (!result.error.empty()) {
                out.error = std::string(p.name) + ": " + result.error;
                return out;
            }
            for (auto& [path, bytes] : result.files) {
                out.files[path] = std::move(bytes);
            }
            out.brushes += result.brushes;
            out.log.insert(out.log.end(), result.log.begin(), result.log.end());
        }
        return out;
    }
    bool ownsFile(const std::string& name) {
        defaults();
        return std::any_of(providers().begin(), providers().end(), [&](const Provider& p) { return p.ownsFile && p.ownsFile(name); });
    }
}  // namespace PetariEditor::Assets
