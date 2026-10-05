#ifdef PETARI_NATIVE

#include "Game/System/PointMove.hpp"
#include "Game/Scene/GameScene.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/GamePadUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/MoviePlayingSequence.hpp"
#include <petari/mods.hpp>
#include <petari/point_move.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
    // Normal play: the Game scene in its action nerve on a stage where the pause
    // menu is allowed (not the title/file select), with Mario present and in
    // control, no demo, talk or movie. (Not the pointer: a click into the window
    // can come before a game frame has seen the pointer arrive; a pointer off the
    // screen only zeroes the stick.)
    bool isSteerable() {
        GameSystemSceneController* pController = SingletonHolder< GameSystem >::get()->mSceneController;

        if (pController == nullptr || pController->mScene == nullptr || !pController->isSceneInitializeState(SceneInitializeState_End) ||
            !MR::isEqualSceneName("Game")) {
            return false;
        }

        if (!static_cast< GameScene* >(pController->mScene)->isNativeSceneAction() || MR::isStageDisablePauseMenu()) {
            return false;
        }

        if (!MR::isExistMario() || MR::isPlayerDead() || MR::isOffPlayerControl()) {
            return false;
        }

        return !((MR::isExistSceneObj(SceneObj_DemoDirector) && MR::isDemoActive()) || MR::isSystemTalking() || MR::isMoviePlayingOnSequence());
    }
}  // namespace

namespace {
    // PETARI_POINT_TRACE=1: Mario's world position when a steering hold starts and ends, the
    // game frames it lasted and the distance he moved (the input side traces the rest).
    void traceHold() {
        static const bool sOn = [] {
            const char* v = std::getenv("PETARI_POINT_TRACE");
            return v != nullptr && v[0] != '\0' && v[0] != '0';
        }();
        static bool sWasClaimed = false;
        static unsigned sFrames = 0;
        static TVec3f sStart(0.0f, 0.0f, 0.0f);
        if (!sOn) {
            return;
        }
        const bool claimed = petari_point_move_claimed();
        const bool mario = isSteerable();
        if (claimed && !sWasClaimed && mario) {
            sStart = *MR::getPlayerPos();
            sFrames = 0;
            std::fprintf(stderr, "[point] hold start: Mario at (%.1f, %.1f, %.1f)\n", sStart.x, sStart.y, sStart.z);
        }
        if (claimed) {
            ++sFrames;
        }
        if (!claimed && sWasClaimed) {
            if (mario) {
                const TVec3f& end = *MR::getPlayerPos();
                const float dx = end.x - sStart.x, dy = end.y - sStart.y, dz = end.z - sStart.z;
                std::fprintf(stderr, "[point] hold end after %u game frames: Mario at (%.1f, %.1f, %.1f), moved %.1f units\n", sFrames, end.x,
                             end.y, end.z, std::sqrt(dx * dx + dy * dy + dz * dz));
            } else {
                std::fprintf(stderr, "[point] hold end after %u game frames (Mario not steerable now)\n", sFrames);
            }
        }
        sWasClaimed = claimed;
    }
}  // namespace

namespace PointMove {
    void update() {
        traceHold();
        if (!PetariNative::Mods::enabled(PetariNative::Mods::Mod::PointToMove) || !isSteerable()) {
            petari_point_move_publish(false, 0.0f, 0.0f);
            return;
        }

        TVec2f mario;
        float x = 0.0f;
        float y = 0.0f;

        const float width = static_cast< float >(MR::getScreenWidth());
        const float height = static_cast< float >(MR::getScreenHeight());
        // The pointer off the screen (not yet seen in the window): steerable, no direction.
        const bool pointer = MR::isCorePadPointInScreen(WPAD_CHAN0);
        const bool seen = MR::calcScreenPosition(&mario, *MR::getPlayerCenterPos()) && pointer;
        const TVec2f* pPointer = MR::getStarPointerScreenPosition(WPAD_CHAN0);

        if (seen) {
            PetariNative::PointMove::stickFor(pPointer->x - mario.x, pPointer->y - mario.y, height, &x, &y);
        }

        petari_point_move_publish(true, x, y);

        // For the overlay's steering cue: both points in KPAD space (-1..1 across the image, y down).
        if (seen) {
            petari_point_move_publish_points(mario.x / width * 2.0f - 1.0f, mario.y / height * 2.0f - 1.0f, pPointer->x / width * 2.0f - 1.0f,
                                             pPointer->y / height * 2.0f - 1.0f);
        }
    }
}  // namespace PointMove

#endif
