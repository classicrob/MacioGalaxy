#ifdef PETARI_NATIVE
// Live check of a new galaxy (spike S8, docs/dev/level-editor/TECH.md 5.7;
// native/tools/editor_galaxy_smoke.py), on the stage smoke with PETARI_STAGE=<the mod galaxy>
// and PETARI_STAGE_FOLLOW=1. The galaxy (petari_terrain_bake --galaxy) puts Power Star 1 where
// Mario lands from the start.
//   PETARI_EDITOR_GALAXY_TEST=collect: logs the galaxy's names (from the mod galaxy registry),
//     waits for the star to be owned after the star sequence, follows the game wherever it goes
//     (the smoke taps A), then saves with the game's own save sequence and reports.
//   PETARI_EDITOR_GALAXY_TEST=verify: a later launch on the same user folder; the star must
//     still be owned when the galaxy is entered.
// Logs "[editor-galaxy] PASS ..." or "[editor-galaxy] FAIL: ...", and sets SmokePhase::followDone.
#include "Game/Editor/EditorGalaxyTest.hpp"
#include "Game/Editor/EditorScene.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/System/GameSequenceFunction.hpp"
#include "Game/Util/MessageUtil.hpp"
#include <petari/smoke_phase.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
    // The message text (host UTF-16, whatever wchar_t is) as ASCII for the log.
    std::string ascii(const void* pText) {
        std::string out;
        const u16* p = static_cast< const u16* >(pText);
        for (int i = 0; p != nullptr && p[i] != 0 && i < 80; i++) {
            out.push_back(p[i] < 0x80 ? static_cast< char >(p[i]) : '?');
        }
        return p == nullptr ? "(none)" : out;
    }

    void done(bool pass, const char* pText) {
        std::fprintf(stderr, "[editor-galaxy] %s%s\n", pass ? "PASS: " : "FAIL: ", pText);
        PetariNative::SmokePhase::followDone.store(true);
    }
}  // namespace

namespace EditorGalaxyTest {
    void update(bool actionNerve) {
        static const char* sMode = std::getenv("PETARI_EDITOR_GALAXY_TEST");
        static const char* sGalaxy = std::getenv("PETARI_STAGE");
        static std::string sLastStage;
        static bool sEntered = false, sOwned = false, sSaving = false, sDone = false;
        static s32 sSaveFrames = 0;
        static s32 sFrames = 0;
        if (sMode == nullptr || sGalaxy == nullptr || sDone) {
            return;
        }
        const bool verify = std::strcmp(sMode, "verify") == 0;
        const char* pStage = EditorScene::stageName();
        const std::string stage = pStage != nullptr ? pStage : "";
        sFrames++;
        if (stage != sLastStage) {
            std::fprintf(stderr, "[editor-galaxy] frame %d: stage %s scenario %d\n", sFrames, stage.empty() ? "(none)" : stage.c_str(),
                         EditorScene::scenario());
            sLastStage = stage;
        }
        if (!sEntered && stage == sGalaxy && actionNerve) {
            sEntered = true;
            const bool owned = GameDataFunction::hasPowerStar(sGalaxy, 1);
            std::fprintf(stderr, "[editor-galaxy] entered %s: title \"%s\", mission 1 \"%s\"; Power Star 1 %s\n", sGalaxy,
                         ascii(MR::getGalaxyNameOnCurrentLanguage(sGalaxy)).c_str(),
                         ascii(MR::getScenarioNameOnCurrentLanguage(sGalaxy, 1)).c_str(), owned ? "owned" : "not owned");
            if (verify) {
                sDone = true;
                done(owned, owned ? "the star collected and saved in the earlier run is owned after a restart"
                                  : "the star is not owned after a restart (the save lost it)");
                return;
            }
        }
        if (verify) {
            return;
        }
        if (!sOwned && sEntered && GameDataFunction::hasPowerStar(sGalaxy, 1)) {
            sOwned = true;
            std::fprintf(stderr, "[editor-galaxy] frame %d: Power Star 1 of %s is owned (stage now %s)\n", sFrames, sGalaxy, stage.c_str());
        }
        // Save once the game is back in play somewhere (the star sequence moves on from the galaxy).
        if (sOwned && !sSaving && actionNerve && stage != sGalaxy && !GameSequenceFunction::isActiveSaveDataHandleSequence()) {
            sSaving = true;
            std::fprintf(stderr, "[editor-galaxy] frame %d: saving in %s\n", sFrames, stage.c_str());
            GameSequenceFunction::startGameDataSaveSequence(false, false);
            return;
        }
        // The sequence becomes active on its next update: give it a few frames first.
        if (sSaving && ++sSaveFrames > 10 && !GameSequenceFunction::isActiveSaveDataHandleSequence()) {
            sDone = true;
            const bool success = GameSequenceFunction::isSuccessSaveDataHandleSequence();
            done(success, success ? "collected Power Star 1 in the mod galaxy, left it and saved" : "the save sequence failed");
        }
    }
}  // namespace EditorGalaxyTest
#endif
