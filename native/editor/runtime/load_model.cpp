// Loading a galaxy's EditModel from the mounted disc (load_model.hpp). Lifted from session.cpp so that
// headless tools load exactly as the in-game editor does.
#include "load_model.hpp"

#include <petari/editor/catalog.hpp>
#include <petari/platform/dvd.hpp>
#include <petari/platform/mod_folder.hpp>
#include <revolution/dvd.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace PetariEditor::Runtime {

std::optional< Formats::Bytes > readMountedDisc(const std::string& path) {
    const s32 entry = DVDConvertPathToEntrynum(path.c_str());
    if (entry < 0) {
        return std::nullopt;
    }
    std::ifstream in(PetariNative::Platform::DVD::hostPathForEntry(entry), std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return Formats::Bytes(std::istreambuf_iterator< char >(in), {});
}

std::optional< Formats::Bytes > readUnmoddedDisc(const std::string& path) {
    const std::filesystem::path files = PetariNative::Platform::DVD::mountInfo().filesDirectory;
    std::ifstream in(files / path.substr(1), std::ios::binary);
    if (files.empty() || !in) {
        return std::nullopt;
    }
    return Formats::Bytes(std::istreambuf_iterator< char >(in), {});
}

void loadCatalog(std::string* pCatalogError) {
    std::string catalogError;
    if (!Catalog::loadWithUser(PetariNative::Platform::ModFolder::userDirectory().string(), &catalogError) && pCatalogError != nullptr) {
        *pCatalogError = catalogError;
    }
    // Planet classes: the names PlanetMapDataTable lists on this disc.
    if (std::optional< Formats::Bytes > raw = readMountedDisc("/ObjectData/PlanetMapDataTable.arc")) {
        if (std::optional< Formats::Archive > archive = Formats::Archive::parse(*raw)) {
            for (const std::string& file : archive->files()) {
                std::optional< Formats::Table > table = Formats::Table::parse(*archive->file(file));
                const int column = table ? table->column(Formats::jmapHash("PlanetName")) : -1;
                std::vector< std::string > names;
                for (int r = 0; column >= 0 && r < table->rowCount(); r++) {
                    names.push_back(table->get(r, column).s);
                }
                Catalog::addPlanets(names);
            }
        }
    }
}

std::unique_ptr< EditModel > loadEditModel(const char* pStage, int scenario, std::string* pError, std::string* pCatalogError) {
    loadCatalog(pCatalogError);
    return EditModel::load(pStage, scenario, readMountedDisc, pError, readUnmoddedDisc);
}

}  // namespace PetariEditor::Runtime
