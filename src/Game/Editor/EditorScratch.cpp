// The play-test progress snapshot (Game/Editor/EditorScratch.hpp).
#include "Game/Editor/EditorScratch.hpp"

#ifdef PETARI_NATIVE
#include "Game/System/GameDataFunction.hpp"
#include "Game/System/GameDataGalaxyStorage.hpp"
#include "Game/System/GameDataHolder.hpp"
#include "Game/System/SaveDataHandler.hpp"
#include "Game/System/UserFile.hpp"
#include <petari/host_allocation.hpp>
#include <petari/platform/mod_galaxy.hpp>

#include <cstdio>
#include <string>
#include <vector>

namespace {
    struct ModGalaxyRecord {
        std::string name;
        u8 owned = 0, visited = 0;
        u16 maxCoins[8] = {};
    };

    struct Snapshot {
        bool taken = false;
        std::string name;
        std::vector< u8 > gameData;
        std::vector< ModGalaxyRecord > modGalaxies;
    };

    Snapshot& snapshotData() {
        static Snapshot sSnapshot;
        return sSnapshot;
    }
}  // namespace

namespace EditorScratch {
    void snapshot() {
        PetariNative::HostAllocationScope host;
        UserFile* pFile = GameDataFunction::nativeCurrentUserFile();
        Snapshot& s = snapshotData();
        s = Snapshot();
        if (pFile == nullptr || pFile->mGameDataHolder == nullptr) {
            return;
        }
        s.name = pFile->getGameDataName();
        s.gameData.assign(SaveDataHandler::getEnoughtTempBufferSize(), 0);
        pFile->makeGameDataBinary(s.gameData.data(), static_cast< u32 >(s.gameData.size()));
        // Mod galaxies' records aren't in the binary (TECH.md 5.7).
        GameDataAllGalaxyStorage* pAll = pFile->mGameDataHolder->mAllGalaxyStorage;
        for (s32 i = 0; i < pAll->getGalaxyNum(); i++) {
            const GameDataSomeGalaxyStorage* pGalaxy = pAll->getGalaxyStorage(i);
            if (PetariNative::Platform::ModGalaxy::find(pGalaxy->mGalaxyName) == nullptr) continue;
            ModGalaxyRecord record;
            record.name = pGalaxy->mGalaxyName;
            record.owned = pGalaxy->mPowerStarOwnedFlags;
            record.visited = pGalaxy->mAlreadyVisitedFlags;
            for (s32 c = 0; c < 8; c++) record.maxCoins[c] = pGalaxy->mMaxCoinNum[c];
            s.modGalaxies.push_back(record);
        }
        s.taken = true;
        std::fprintf(stderr, "[editor] play-test isolation: progress of %s kept (%zu mod galaxies)\n", s.name.c_str(), s.modGalaxies.size());
    }

    void restore() {
        PetariNative::HostAllocationScope host;
        Snapshot& s = snapshotData();
        UserFile* pFile = GameDataFunction::nativeCurrentUserFile();
        if (!s.taken || pFile == nullptr || pFile->mGameDataHolder == nullptr) {
            s = Snapshot();
            return;
        }
        pFile->loadFromGameDataBinary(s.name.c_str(), s.gameData.data(), static_cast< u32 >(s.gameData.size()));
        GameDataAllGalaxyStorage* pAll = pFile->mGameDataHolder->mAllGalaxyStorage;
        for (const ModGalaxyRecord& record : s.modGalaxies) {
            const s32 index = pAll->findIndex(record.name.c_str());
            if (index < 0) continue;
            GameDataSomeGalaxyStorage* pGalaxy = pAll->getGalaxyStorage(index);
            pGalaxy->mPowerStarOwnedFlags = record.owned;
            pGalaxy->mAlreadyVisitedFlags = record.visited;
            for (s32 c = 0; c < 8; c++) pGalaxy->mMaxCoinNum[c] = record.maxCoins[c];
        }
        std::fprintf(stderr, "[editor] play-test isolation: progress of %s restored\n", s.name.c_str());
        s = Snapshot();
    }
}  // namespace EditorScratch
#endif
