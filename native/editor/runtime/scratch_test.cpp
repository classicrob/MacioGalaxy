// Live check of play-test progress isolation (native/tools/editor_sync_smoke.py): with
// PETARI_EDITOR=1 and PETARI_EDITOR_SCRATCH_TEST=1, once the stage smoke's checks are done it opens
// an edit session (ensureModel: progress isolated) with 12 lives and plays it hard: 100 Star Bits,
// 3 1-Ups, 2 lives lost, 50 coins, a Power Star recorded, and a real save write sent to the NAND
// thread; then turns the editor off (the session ends). The real save file and progress.json must
// be byte-identical to before, the progress in memory back where the session found it (lives
// too, which the save data's own read resets to 4), and isolation released.
// Logs "[editor-scratch] PASS" or "[editor-scratch] FAIL: <reason>".
#include "runtime_core.hpp"

#include <petari/editor/runtime.hpp>
#include <petari/editor/runtime_hooks.hpp>
#include <petari/host_allocation.hpp>
#include <petari/smoke_phase.hpp>

#include "Game/System/GameDataFunction.hpp"
#include "Game/System/NANDManager.hpp"
#include "Game/Util/SingletonHolder.hpp"

#include "Game/System/GameDataTemporaryInGalaxy.hpp"
#include "Game/System/GameDataHolder.hpp"
#include "Game/System/SpinDriverPathStorage.hpp"
#include "Game/System/UserFile.hpp"
#include "Game/Util/SceneUtil.hpp"
#include <petari/platform/mod_folder.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
#include <cstdlib>
#include <string>

namespace PetariEditor::Runtime {
namespace {
    std::vector< char > readAll(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        return std::vector< char >(std::istreambuf_iterator< char >(in), {});
    }

    struct State {
        int frame = -1;
        bool done = false;
        s32 before = 0;
        s32 livesBefore = 0;
        bool supplyBefore = false;
        NANDRequestInfo duringSession;  // a save prepared in the session, executed (say) after it
        NANDRequestInfo realSave;       // a write of the real save file, sent to the NAND thread in the session
        std::string savePath, progressPath;
        std::vector< char > saveBytes, progressBytes;
        bool starBefore = false;
        s32 coinsBefore = 0;
        SpinDriverPathStorageScenario* spin = nullptr;
        std::vector< SpinDriverPathStorageOne > spinBefore;
    };
    State gScratch;

    void finish(const std::string& result) {
        std::fprintf(stderr, "[editor-scratch] %s\n", result.c_str());
        gScratch.done = true;
    }
}  // namespace

// Before the editor-off check in tick: it must run on the frame the editor is turned off too.
void scratchTestTick() {
    static const bool sOn = [] {
        const char* p = std::getenv("PETARI_EDITOR_SCRATCH_TEST");
        return p != nullptr && p[0] == '1';
    }();
    State& s = gScratch;
    if (!sOn || s.done || !PetariNative::SmokePhase::stageChecksDone.load()) return;
    const int f = ++s.frame;
    if (f == 0) {
        GameDataFunction::addPlayerLeft(12 - GameDataFunction::getPlayerLeft());  // a non-default count
        // The real records on disk, as the session finds them.
        namespace fs = std::filesystem;
        const fs::path user = PetariNative::Platform::ModFolder::userDirectory();
        std::error_code ec;
        for (fs::recursive_directory_iterator it(user / "NAND", ec), end; !ec && it != end; it.increment(ec)) {
            if (it->path().filename() == "GameData.bin") s.savePath = it->path().string();
        }
        s.progressPath = (user / "progress.json").string();
        if (s.savePath.empty()) return finish("FAIL: no save file in the user folder");
        s.saveBytes = readAll(s.savePath);
        s.progressBytes = readAll(s.progressPath);
        s.starBefore = GameDataFunction::hasPowerStar(MR::getCurrentStageName(), 1);
        s.coinsBefore = GameDataFunction::nativeTemporaryInGalaxy()->mCoinNum;
        s.before = GameDataFunction::getStarPieceNum();
        s.livesBefore = GameDataFunction::getPlayerLeft();
        s.supplyBefore = GameDataFunction::isPlayerLeftSupply();
        // Deliberately unsorted registrations: SPN1 would omit the zero and quantize the fraction.
        auto* storage = GameDataFunction::nativeCurrentUserFile()->mGameDataHolder->mSpinDriverPathStorage;
        for (auto* galaxy : storage->mGalaxyStorage) {
            for (int i = 0; i < galaxy->mNumScenarios && !s.spin; ++i) {
                auto& scenario = galaxy->mScenarioStorage[i];
                if (scenario.mOneStorage.size() + 3 <= scenario.mOneStorage.capacity()) s.spin = &scenario;
            }
            if (s.spin) break;
        }
        if (!s.spin) return finish("FAIL: no room for the SPN1 fixture");
        SpinDriverPathStorageOne zero, fraction;
        zero.registerToUse(62, 62);
        fraction.registerToUse(61, 61);
        fraction.mDrawRange = .12345f;
        s.spin->mOneStorage.push_back(zero);
        s.spin->mOneStorage.push_back(fraction);
        s.spinBefore.assign(s.spin->mOneStorage.begin(), s.spin->mOneStorage.end());
        if (!ensureModel() || !progressIsolated()) return finish("FAIL: no session, or progress not isolated");
        SpinDriverPathStorageOne added;
        added.registerToUse(60, 60);
        s.spin->mOneStorage.push_back(added);
        for (auto& entry : s.spin->mOneStorage) entry.mDrawRange = .875f;
        GameDataFunction::addStarPiece(100);
        GameDataFunction::addPlayerLeft(3);   // 1-Ups
        GameDataFunction::addPlayerLeft(-2);  // lives lost
        GameDataFunction::nativeTemporaryInGalaxy()->mCoinNum += 50;
        GameDataFunction::setGameFlagPowerStarSuccess(MR::getCurrentStageName(), 1, true);
        // A real save write (garbage over GameData.bin's NAND path), through the NAND thread.
        {
            static u8 sGarbage[64];
            const std::string user = PetariNative::Platform::ModFolder::userDirectory().string();
            const std::string nandPath = s.savePath.substr(s.savePath.find("/NAND") + 5);
            s.realSave.setWriteSeq(nandPath.c_str(), sGarbage, sizeof(sGarbage), 0, 0);
            SingletonHolder< NANDManager >::get()->addRequest(&s.realSave);
        }
        static const u8 kBytes[4] = {};
        s.duringSession.setWriteSeq("/editor-scratch-test.bin", kBytes, sizeof(kBytes), 0, 0);
        if (!s.duringSession.mNativeDropWrite) return finish("FAIL: a write prepared in the session isn't marked to drop");
        std::fprintf(stderr, "[editor-scratch] in the session: Star Bits %d -> %d\n", s.before, GameDataFunction::getStarPieceNum());
        if (GameDataFunction::getStarPieceNum() == s.before) return finish("FAIL: the Star Bits didn't change in the session");
        Hooks::setEnabled(false);  // the editor turned off: the session ends on the next tick
        return;
    }
    if (f == 3) {
        const auto& entries = s.spin->mOneStorage;
        if (entries.size() != int(s.spinBefore.size()) + 1) return finish("FAIL: SPN1 registration count changed");
        for (size_t i = 0; i < s.spinBefore.size(); ++i) {
            const auto& before = s.spinBefore[i];
            const auto& after = entries[i];
            if (before.mZoneId != after.mZoneId || before.mSpinDriverId != after.mSpinDriverId || before.mDrawRange != after.mDrawRange)
                return finish("FAIL: SPN1 slot identity or exact progress changed");
        }
        const auto& added = entries[s.spinBefore.size()];
        if (added.mZoneId != 60 || added.mSpinDriverId != 60 || added.mDrawRange != 0)
            return finish("FAIL: SPN1 new registration was lost or kept play-test progress");
        std::fprintf(stderr, "[editor-scratch] SPN1: zero/fractional progress and live indices restored; new slot retained at zero\n");
        const s32 after = GameDataFunction::getStarPieceNum();
        std::fprintf(stderr, "[editor-scratch] after the session: Star Bits %d (found %d), lives %d (found %d), isolated %d\n", after, s.before,
                     GameDataFunction::getPlayerLeft(), s.livesBefore, progressIsolated());
        if (after != s.before) return finish("FAIL: the play-test's Star Bits reached the save file's progress");
        if (GameDataFunction::getPlayerLeft() != s.livesBefore) return finish("FAIL: the lives weren't restored");
        if (GameDataFunction::isPlayerLeftSupply() != s.supplyBefore) return finish("FAIL: the 1-Up supply changed (the extra-life letter)");
        // The NAND thread decides by the request, not the moment it runs: still dropped after End,
        // and a write prepared now goes through.
        NANDRequestInfo afterSession;
        static const u8 kAfter[4] = {};
        afterSession.setWriteSeq("/editor-scratch-test.bin", kAfter, sizeof(kAfter), 0, 0);
        if (!s.duringSession.mNativeDropWrite || afterSession.mNativeDropWrite) return finish("FAIL: the drop decision didn't follow the request");
        if (GameDataFunction::nativeTemporaryInGalaxy()->mCoinNum != s.coinsBefore) return finish("FAIL: the coins weren't restored");
        if (GameDataFunction::hasPowerStar(MR::getCurrentStageName(), 1) != s.starBefore) return finish("FAIL: the Power Star stayed recorded");
        const bool saveSame = readAll(s.savePath) == s.saveBytes, progressSame = readAll(s.progressPath) == s.progressBytes;
        std::fprintf(stderr, "[editor-scratch] on disk: save %s, progress.json %s\n", saveSame ? "identical" : "CHANGED", progressSame ? "identical" : "CHANGED");
        if (!saveSame || !progressSame) return finish("FAIL: the real records changed on disk");
        if (progressIsolated()) return finish("FAIL: isolation still on after the session");
        return finish("PASS");
    }
}

}  // namespace PetariEditor::Runtime
