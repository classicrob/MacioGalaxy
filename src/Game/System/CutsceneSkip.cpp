#ifdef PETARI_NATIVE

#include "Game/System/CutsceneSkip.hpp"
#include "Game/Demo/DemoDirector.hpp"
#include "Game/Demo/DemoFunction.hpp"
#include "Game/Scene/GameScene.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/GameSceneLayoutHolder.hpp"
#include "Game/Screen/MoviePlayingSequence.hpp"
#include "Game/Screen/YesNoController.hpp"
#include "Game/Screen/YesNoLayout.hpp"
#include "Game/System/GameSequenceFunction.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/System/WPad.hpp"
#include "Game/System/WPadButton.hpp"
#include "Game/System/WPadHolder.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include <petari/mods.hpp>
#include <petari/skip.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
    using PetariNative::Skip::Context;

    // A frame's extra scene updates stop at this many or after this much time,
    // so the window keeps drawing while a long demo runs to its end.
    constexpr int cFastForwardMaxSteps = 15;
    constexpr double cFastForwardBudgetMs = 10.0;

    struct Shown {
        bool game = false;  // the Game scene, initialised
        bool movie = false;
        bool demo = false;
        bool talk = false;
        bool stageClear = false;
        bool choice = false;  // a yes/no, the save prompt or a menu (galaxy map, pause) waits for the player
        bool gameplay = false;
    };

    Shown gShown;
    const void* gLastScene = nullptr;
    char gLastStage[64] = {};
    bool gMuting = false;
    bool gWaitingForPlayer = false;  // noteWaitingForPlayer since the last noteFrame
    std::chrono::steady_clock::time_point gFastForwardStart;
    int gFastForwardSteps = 0;
    unsigned long gFastForwardTotal = 0;  // extra updates in this skip, for the log
    unsigned long gMutedSE = 0;           // sound effect starts refused by the mute in this skip

    bool isChoiceShown() {
        if (GameSequenceFunction::isActiveSaveDataHandleSequence()) {
            return true;
        }

        if (!MR::isExistSceneObj(SceneObj_GameSceneLayoutHolder)) {
            return false;
        }

        YesNoLayout* pYesNo = MR::getGameSceneLayoutHolder()->mYesNoLayout;
        return pYesNo != nullptr && pYesNo->mController != nullptr && pYesNo->mController->_C;
    }

    Shown observe(GameSystemSceneController* pController) {
        Shown shown;
        shown.game = pController->mScene != nullptr && pController->isSceneInitializeState(SceneInitializeState_End) &&
                     MR::isEqualSceneName("Game");

        if (!shown.game) {
            return shown;
        }

        GameScene* pScene = static_cast< GameScene* >(pController->mScene);
        shown.movie = MR::isMoviePlayingOnSequence() || MR::isActiveMoviePlayer();
        shown.demo = MR::isExistSceneObj(SceneObj_DemoDirector) && MR::isDemoActive();
        shown.talk = MR::isSystemTalking();
        shown.stageClear = pScene->isExecStageClearDemo();
        shown.choice = isChoiceShown() || pScene->isNativeMenuOpen();
        const bool opening = pScene->isExecScenarioOpeningCamera() || pScene->isExecScenarioStarter();
        const bool wipe = MR::isExistSceneObj(SceneObj_SceneWipeHolder) && MR::isWipeActive();
        shown.gameplay = pScene->isNativeSceneAction() && !shown.demo && !shown.talk && !shown.movie && !opening && !wipe;
        return shown;
    }
}  // namespace

namespace CutsceneSkip {
    void noteFrame() {
        GameSystemSceneController* pController = SingletonHolder< GameSystem >::get()->mSceneController;

        // Mod off: the game state is not read at all (unless PETARI_SKIP_OBSERVE_WHEN_OFF, for the
        // skip probe's mod-off check: the context is then published, and nothing else changes).
        static const bool sObserveWhenOff = [] {
            const char* value = std::getenv("PETARI_SKIP_OBSERVE_WHEN_OFF");
            return value != nullptr && value[0] != '\0' && value[0] != '0';
        }();
        if (pController == nullptr || (!PetariNative::Mods::enabled(PetariNative::Mods::Mod::SkipCutscenes) && !sObserveWhenOff)) {
            gShown = Shown();
            gMuting = false;
            gWaitingForPlayer = false;
            petari_skip_set_context(0);
            return;
        }

        unsigned context = 0;
        const char* pStage = pController->mCurrSceneControlInfo.mStage;
        if (pController->mScene != gLastScene || std::strncmp(pStage != nullptr ? pStage : "", gLastStage, sizeof(gLastStage) - 1) != 0) {
            if (gLastScene != nullptr) {
                context |= PetariNative::Skip::kNewStage;
            }

            gLastScene = pController->mScene;
            std::snprintf(gLastStage, sizeof(gLastStage), "%s", pStage != nullptr ? pStage : "");
        }

        gShown = observe(pController);
        gShown.choice = gShown.choice || (gShown.game && gWaitingForPlayer);
        gWaitingForPlayer = false;
        GameScene* pScene = gShown.game ? static_cast< GameScene* >(pController->mScene) : nullptr;
        const bool opening = pScene != nullptr && (pScene->isExecScenarioOpeningCamera() || pScene->isExecScenarioStarter());

        if (gShown.game && !gShown.choice && (gShown.movie || gShown.demo || gShown.talk || gShown.stageClear || opening)) {
            context |= PetariNative::Skip::kSkippable;
        }

        if (gShown.gameplay) {
            context |= PetariNative::Skip::kGameplay;
        }

        if (gShown.choice) {
            context |= PetariNative::Skip::kChoice;
        }

        petari_skip_set_context(context);

        if (!petari_skip_active() && (gFastForwardTotal != 0 || gMutedSE != 0)) {
            std::fprintf(stderr, "[skip] fast-forward ran %lu extra scene updates; %lu sound effect starts muted\n", gFastForwardTotal,
                         gMutedSE);
            gFastForwardTotal = 0;
            gMutedSE = 0;
        }

        gMuting = petari_skip_active() && gShown.game && !gShown.movie && (gShown.demo || gShown.talk || gShown.stageClear);
    }

    bool isActive() {
        return petari_skip_active();
    }

    bool continueFastForward(bool firstStep) {
        if (firstStep) {
            gFastForwardStart = std::chrono::steady_clock::now();
            gFastForwardSteps = 0;
        }

        if (!petari_skip_active() || !gShown.game || gShown.movie || gShown.choice) {
            return false;
        }

        if (!gShown.demo && !gShown.talk && !gShown.stageClear) {
            return false;
        }

        // A requested scene change happens at the next frame's GameSystem::update.
        if (SingletonHolder< GameSystem >::get()->mSceneController->isExistRequest()) {
            return false;
        }

        if (gFastForwardSteps >= cFastForwardMaxSteps) {
            return false;
        }

        const double elapsedMs = std::chrono::duration< double, std::milli >(std::chrono::steady_clock::now() - gFastForwardStart).count();

        if (!firstStep && elapsedMs >= cFastForwardBudgetMs) {
            return false;
        }

        ++gFastForwardSteps;
        ++gFastForwardTotal;
        return true;
    }

    void beginFastForwardStep() {
        for (s32 chan = 0; chan < WPAD_MAX_CONTROLLERS; chan++) {
            WPad* pPad = MR::getWPad(chan);

            if (pPad != nullptr && pPad->mButton != nullptr) {
                pPad->mButton->clearEdges();
            }
        }
    }

    bool isMutingSE() {
        return gMuting;
    }

    void noteMutedSE() {
        ++gMutedSE;
    }

    void noteWaitingForPlayer() {
        gWaitingForPlayer = true;
    }

    const char* describe() {
        static char sText[160];
        GameSystemSceneController* pController = SingletonHolder< GameSystem >::get()->mSceneController;

        if (pController == nullptr || !gShown.game) {
            std::snprintf(sText, sizeof(sText), "scene %s", pController != nullptr ? pController->mCurrSceneControlInfo.mScene : "-");
            return sText;
        }

        GameScene* pScene = static_cast< GameScene* >(pController->mScene);
        int length = std::snprintf(sText, sizeof(sText), "%s:", pController->mCurrSceneControlInfo.mStage);
        auto add = [&length](const char* pFormat, auto... args) {
            if (length >= 0 && length < static_cast< int >(sizeof(sText))) {
                length += std::snprintf(sText + length, sizeof(sText) - length, pFormat, args...);
            }
        };

        if (gShown.movie) {
            add(" movie %d/%u", MR::isActiveMoviePlayer() ? MR::getMovieCurrentFrame() : -1, MR::isActiveMoviePlayer() ? MR::getMovieTotalFrame() : 0u);
        }
        if (gShown.demo) {
            const char* pName = DemoFunction::getDemoDirector()->getCurrentDemoName();
            add(" demo(%s)", pName != nullptr ? pName : "?");
        }
        if (gShown.talk) {
            add(" talk");
        }
        if (gShown.stageClear) {
            add(" stage-clear");
        }
        if (pScene->isExecScenarioOpeningCamera()) {
            add(" galaxy-entry-camera");
        }
        if (pScene->isExecScenarioStarter()) {
            add(" spin-driver-launch");
        }
        if (pScene->isNativeMenuOpen()) {
            add(" menu");
        }
        if (gShown.choice) {
            add(" choice");
        }
        if (gShown.gameplay) {
            add(" gameplay");
        }
        return sText;
    }
}  // namespace CutsceneSkip

#endif
