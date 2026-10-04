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

namespace {
    // Normal play: the Game scene in its action nerve with Mario present and in
    // control, no demo, talk or movie, and the pointer on the screen.
    bool isSteerable() {
        GameSystemSceneController* pController = SingletonHolder< GameSystem >::get()->mSceneController;

        if (pController == nullptr || pController->mScene == nullptr || !pController->isSceneInitializeState(SceneInitializeState_End) ||
            !MR::isEqualSceneName("Game")) {
            return false;
        }

        if (!static_cast< GameScene* >(pController->mScene)->isNativeSceneAction()) {
            return false;
        }

        if (!MR::isExistMario() || MR::isPlayerDead() || MR::isOffPlayerControl()) {
            return false;
        }

        if ((MR::isExistSceneObj(SceneObj_DemoDirector) && MR::isDemoActive()) || MR::isSystemTalking() || MR::isMoviePlayingOnSequence()) {
            return false;
        }

        return MR::isCorePadPointInScreen(WPAD_CHAN0);
    }
}  // namespace

namespace PointMove {
    void update() {
        if (!PetariNative::Mods::enabled(PetariNative::Mods::Mod::PointToMove) || !isSteerable()) {
            petari_point_move_publish(false, 0.0f, 0.0f);
            return;
        }

        TVec2f mario;
        float x = 0.0f;
        float y = 0.0f;

        const float width = static_cast< float >(MR::getScreenWidth());
        const float height = static_cast< float >(MR::getScreenHeight());
        const bool seen = MR::calcScreenPosition(&mario, *MR::getPlayerCenterPos());
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
