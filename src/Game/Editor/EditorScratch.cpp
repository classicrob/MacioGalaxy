// The play-test progress snapshot (Game/Editor/EditorScratch.hpp).
#include "Game/Editor/EditorScratch.hpp"

#ifdef PETARI_NATIVE
#include "Game/System/GameDataFunction.hpp"
#include "Game/System/GameDataGalaxyStorage.hpp"
#include "Game/System/GameDataHolder.hpp"
#include "Game/System/GameDataPlayerStatus.hpp"
#include "Game/System/GameDataTemporaryInGalaxy.hpp"
#include "Game/System/SaveDataHandler.hpp"
#include "Game/System/SpinDriverPathStorage.hpp"
#include "Game/System/UserFile.hpp"
#include <petari/host_allocation.hpp>
#include <petari/platform/mod_galaxy.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {
    struct SpinScenario {
        SpinDriverPathStorageScenario* storage = nullptr;
        std::vector< SpinDriverPathStorageOne > entries;
    };

    std::vector< SpinScenario > captureSpin(SpinDriverPathStorage* storage) {
        std::vector< SpinScenario > result;
        for (auto* galaxy : storage->mGalaxyStorage) {
            for (s32 i = 0; i < galaxy->mNumScenarios; ++i) {
                auto* scenario = &galaxy->mScenarioStorage[i];
                result.push_back({scenario, {scenario->mOneStorage.begin(), scenario->mOneStorage.end()}});
            }
        }
        return result;
    }

    void applySpin(const std::vector< SpinScenario >& records) {
        for (const auto& record : records) {
            // The same UserFile owns these fixed-capacity arrays throughout the session.
            auto& entries = record.storage->mOneStorage;
            entries.clear();
            for (const auto& entry : record.entries) entries.push_back(entry);
        }
    }

    void restoreSpinProgress(std::vector< SpinScenario >& current, const std::vector< SpinScenario >& baseline) {
        for (auto& scenario : current) {
            const auto saved = std::find_if(baseline.begin(), baseline.end(), [&](const auto& s) { return s.storage == scenario.storage; });
            for (auto& entry : scenario.entries) {
                // Edits may register new launch stars; retain their indices, with no earned progress.
                entry.mDrawRange = 0.0f;
                if (saved == baseline.end()) continue;
                const auto old = std::find_if(saved->entries.begin(), saved->entries.end(), [&](const auto& s) {
                    return s.mZoneId == entry.mZoneId && s.mSpinDriverId == entry.mSpinDriverId;
                });
                if (old != saved->entries.end()) entry.mDrawRange = old->mDrawRange;
            }
        }
    }

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
        std::vector< SpinScenario > spin;
        // GameDataPlayerStatus entire: its deserialize sets 4 lives and reads the saved count into the
        // (unsaved) 1-Up supply, so the save data alone can't bring it back.
        std::unique_ptr< GameDataPlayerStatus > status;
        const UserFile* pFile = nullptr;  // restored only into the same save file
        // This galaxy's run so far (committed to the save file when the galaxy is left).
        bool haveRun = false;
        int starPieces = 0, last1upStarPieces = 0, coins = 0;
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
        s.status = std::make_unique< GameDataPlayerStatus >(*pFile->mGameDataHolder->mPlayerStatus);
        s.spin = captureSpin(pFile->mGameDataHolder->mSpinDriverPathStorage);
        s.pFile = pFile;
        if (GameDataTemporaryInGalaxy* pRun = GameDataFunction::nativeTemporaryInGalaxy()) {
            s.haveRun = true;
            s.starPieces = pRun->mStarPieceNum;
            s.last1upStarPieces = pRun->mLast1upStarPieceNum;
            s.coins = pRun->mCoinNum;
        }
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
        if (pFile != s.pFile || s.name != pFile->getGameDataName()) {
            std::fprintf(stderr, "[editor] play-test isolation: WARNING: another save file is in use now; its progress is left as it is\n");
            s = Snapshot();
            return;
        }
        if (GameDataTemporaryInGalaxy* pRun = GameDataFunction::nativeTemporaryInGalaxy(); pRun != nullptr && s.haveRun) {
            pRun->mStarPieceNum = s.starPieces;
            pRun->mLast1upStarPieceNum = s.last1upStarPieces;
            pRun->mCoinNum = s.coins;
        }
        auto currentSpin = captureSpin(pFile->mGameDataHolder->mSpinDriverPathStorage);
        restoreSpinProgress(currentSpin, s.spin);
        pFile->loadFromGameDataBinary(s.name.c_str(), s.gameData.data(), static_cast< u32 >(s.gameData.size()));
        // SPN1 omits zero ranges, quantizes others, and sorts registrations by zone. Restore the
        // exact baseline for the binary audit; then retain live actor indices from the current run.
        applySpin(s.spin);
        *pFile->mGameDataHolder->mPlayerStatus = *s.status;
        GameDataAllGalaxyStorage* pAll = pFile->mGameDataHolder->mAllGalaxyStorage;
        for (const ModGalaxyRecord& record : s.modGalaxies) {
            const s32 index = pAll->findIndex(record.name.c_str());
            if (index < 0) continue;
            GameDataSomeGalaxyStorage* pGalaxy = pAll->getGalaxyStorage(index);
            pGalaxy->mPowerStarOwnedFlags = record.owned;
            pGalaxy->mAlreadyVisitedFlags = record.visited;
            for (s32 c = 0; c < 8; c++) pGalaxy->mMaxCoinNum[c] = record.maxCoins[c];
        }
        // What the snapshot holds must come back exactly: anything a deserialize resets would show here.
        std::vector< u8 > now(s.gameData.size(), 0);
        pFile->makeGameDataBinary(now.data(), static_cast< u32 >(now.size()));
        applySpin(currentSpin);
        size_t first = now.size(), last = 0, count = 0;
        for (size_t i = 0; i < now.size(); i++) {
            if (now[i] != s.gameData[i]) {
                first = std::min(first, i);
                last = i;
                count++;
            }
        }
        if (const char* pDump = std::getenv("PETARI_EDITOR_SCRATCH_DUMP"); pDump != nullptr && count != 0) {
            // A diagnostic: both binaries, to see which save data chunk didn't come back.
            for (int k = 0; k < 2; k++) {
                const std::string path = std::string(pDump) + (k == 0 ? "/snapshot.bin" : "/restored.bin");
                if (FILE* pFile = std::fopen(path.c_str(), "wb")) {
                    const std::vector< u8 >& data = k == 0 ? s.gameData : now;
                    std::fwrite(data.data(), 1, data.size(), pFile);
                    std::fclose(pFile);
                }
            }
        }
        if (count == 0) {
            std::fprintf(stderr, "[editor] play-test isolation: progress of %s restored\n", s.name.c_str());
        } else {
            std::fprintf(stderr, "[editor] play-test isolation: progress of %s restored (WARNING: the save data differs from the snapshot: %zu bytes in 0x%zx-0x%zx)\n",
                         s.name.c_str(), count, first, last);
        }
        s = Snapshot();
    }
}  // namespace EditorScratch
#endif
