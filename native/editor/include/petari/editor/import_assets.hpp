#pragma once
#include <memory>
#include <petari/editor/edit_api.hpp>
#include <petari/editor/import.hpp>
#include <petari/editor/terrain/bake.hpp>

namespace PetariEditor::Imports {
    using Reader = std::function< std::optional< Bytes >(const std::string&) >;
    struct BuiltPart {
        Bytes model, middle, low, kcl, pa;
        Vec3 minimum, maximum;
        std::string error;
    };
    // The row's scale is baked into geometry. The part matrix then carries translation/rotation.
    BuiltPart buildPart(const Asset& asset, int part, Vec3 scale, const Reader& readDisc);
    using AssetReader = std::function< std::shared_ptr< const Asset >(const std::string&) >;
    Terrain::BakeOutput bake(const Terrain::BakeInput& input, const AssetReader& assets);
    bool bakePatchAssets(const std::filesystem::path& root, const Patch::DiscReader& readDisc, std::vector< Patch::Built >* files,
                         std::vector< std::string >* log, std::string* error);
    void registerPatchBuilder();

    // Neutral, content-addressed assets. Call catalog/storage mutations at the editor seam.
    void loadLibrary(const std::string& userDirectory, const std::vector< std::filesystem::path >& modRoots);
    std::shared_ptr< const Asset > findAsset(const std::string& id);
    std::string storeAsset(const Asset& asset, std::string* error);
    bool saveAssets(const ModelView& model, const std::filesystem::path& root, std::string* error);
    void registerClasses();
    // Placing the first Browser card places every material/collision split in one op.
    void appendPlacementParts(Op& op, const AddRow& first);
    // UI hands an immutable asset to the game thread. Canceling/undoing placement retains the library part.
    void requestPlacement(std::shared_ptr< const Asset > asset);
    std::shared_ptr< const Asset > takePlacement();
}  // namespace PetariEditor::Imports
