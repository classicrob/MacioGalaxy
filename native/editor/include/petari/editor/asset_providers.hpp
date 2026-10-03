#pragma once
#include <petari/editor/edit_api.hpp>
#include <petari/editor/terrain/bake.hpp>

namespace PetariEditor::Assets {
    // Pure generated-asset services shared by headless and in-game editing.
    // Providers run in order; later bakers read files produced by earlier ones.
    struct Provider {
        const char* name = nullptr;
        void (*load)(const std::string& userDirectory, const std::vector< std::filesystem::path >& modRoots) = nullptr;
        bool (*save)(const ModelView&, const std::filesystem::path&, std::string* error) = nullptr;
        Terrain::BakeOutput (*bake)(const Terrain::BakeInput&) = nullptr;
        bool (*ownsFile)(const std::string& basename) = nullptr;
    };
    void registerProvider(Provider provider);
    void load(const std::string& userDirectory, const std::vector< std::filesystem::path >& roots);
    bool save(const ModelView& model, const std::filesystem::path& root, std::string* error);
    Terrain::BakeOutput bake(const Terrain::BakeInput& input);
    bool ownsFile(const std::string& basename);
}  // namespace PetariEditor::Assets
