#ifdef PETARI_NATIVE
#include "Game/Editor/EditorScene.hpp"
#include "Game/LiveActor/AllLiveActorGroup.hpp"
#include "Game/LiveActor/ClippingActorHolder.hpp"
#include "Game/LiveActor/ClippingDirector.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ViewGroupCtrl.hpp"
#include "Game/NameObj/ModelChangableObjFactory.hpp"
#include "Game/NameObj/NameObjExecuteHolder.hpp"
#include "Game/NameObj/NameObjFactory.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/JMapUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Gravity/PlanetGravityManager.hpp"
#include "Game/Scene/StageDataHolder.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include <JSystem/JKernel/JKRArchive.hpp>
#include <JSystem/JKernel/JKRSolidHeap.hpp>
#include <cmath>

namespace {
    // Kept free for the game's own runtime needs (effects, generated items reuse pre-made actors,
    // but some classes register helpers on first use).
    const s32 kActorMargin = 64;
    const s32 kExecuteMargin = 64;

    NameObjExecuteHolder* executeHolder() {
        return MR::getSceneObj< NameObjExecuteHolder >(SceneObj_NameObjExecuteHolder);
    }

    // ClippingActorHolder::initViewGroupTable runs once at the end of load; a later actor's
    // ClippingActorInfo has no view-group entry until the lists are walked again.
    void refreshViewGroups() {
        ClippingActorHolder* pHolder = MR::getClippingDirector()->mActorHolder;
        pHolder->mViewGroupCtrl->initViewGroup(pHolder->_10);
        pHolder->mViewGroupCtrl->initViewGroup(pHolder->_18);
        pHolder->mViewGroupCtrl->initViewGroup(pHolder->_1C);
        pHolder->mViewGroupCtrl->initViewGroup(pHolder->_14);
    }
}  // namespace

namespace EditorScene {
    s32 liveActorCount() {
        return MR::getAllLiveActorGroup()->getObjNum();
    }

    s32 liveActorLimit() {
        return 2560;  // AllLiveActorGroup (AllLiveActorGroup.cpp)
    }

    bool createFromRow(const JMapInfoIter& rIter, Created* pOut, const char** ppError) {
        *pOut = Created();
        const char* pName = "";
        if (!MR::getObjectName(&pName, rIter) || pName[0] == '\0') {
            *ppError = "the row has no object name";
            return false;
        }
        s32 shapeId = -1;
        MR::getJMapInfoShapeIdWithInit(rIter, &shapeId);
        CreatorFuncPtr creator = shapeId != -1 ? MR::getModelChangableObjCreator(pName) : NameObjFactory::getCreator(pName);
        if (creator == nullptr) {
            *ppError = "no such object class";
            return false;
        }
        AllLiveActorGroup* pGroup = MR::getAllLiveActorGroup();
        NameObjExecuteHolder* pExec = executeHolder();
        if (pGroup->getObjNum() >= liveActorLimit() - kActorMargin) {
            *ppError = "the scene's actor limit is reached";
            return false;
        }
        if (pExec->nativeExecuteCount() >= pExec->nativeExecuteCapacity() - kExecuteMargin) {
            *ppError = "the scene's object limit is reached";
            return false;
        }

        // The archives placement would have mounted at load (synchronous; 1-2 ms each from disk).
        if (shapeId != -1) {
            MR::requestMountModelChangableObjArchives(pName, shapeId);
        } else {
            NameObjFactory::requestMountObjectArchives(pName, rIter);
        }
        MR::receiveAllRequestedFile();

        const s32 actorsBefore = pGroup->getObjNum();
        const s32 executeBefore = pExec->nativeExecuteCount();
        MR::setCurrentPlacementZoneId(MR::getPlacedZoneId(rIter));  // collision parts take their zone from it
        MR::startInitLiveActorSystemInfo();
        NameObj* pObj = creator(MR::getJapaneseObjectName(pName));
        pObj->init(rIter);
        MR::initLiveActorSystemInfo(rIter);
        pObj->initAfterPlacement();
        MR::clearCurrentPlacementZoneId();
        lateConnect(executeBefore);

        pOut->mObj = pObj;
        pOut->mActorCount = pGroup->getObjNum() - actorsBefore;
        pOut->mFirstActor = pOut->mActorCount > 0 ? pGroup->getActor(actorsBefore) : nullptr;
        pOut->mFirstIndex = actorsBefore;
        return true;
    }

    s32 executeCount() {
        return executeHolder()->nativeExecuteCount();
    }

    void lateConnect(s32 executeBefore) {
        executeHolder()->nativeLateConnect(executeBefore);
        refreshViewGroups();
    }

    LiveActor* actorAt(s32 index) {
        AllLiveActorGroup* pGroup = MR::getAllLiveActorGroup();
        return index >= 0 && index < pGroup->getObjNum() ? pGroup->getActor(index) : nullptr;
    }

    s32 gravityCount() {
        if (!MR::isExistSceneObj(SceneObj_PlanetGravityManager)) {
            return 0;
        }
        return MR::getSceneObj< PlanetGravityManager >(SceneObj_PlanetGravityManager)->nativeGravityCount();
    }

    const char* stageName() {
        return MR::getCurrentStageName();
    }

    s32 scenario() {
        return MR::getCurrentScenarioNo();
    }

    const void* tableData(s32 zoneId, const char* pPath) {
        if (!MR::isExistSceneObj(SceneObj_StageDataHolder)) {
            return nullptr;
        }
        StageDataHolder* pRoot = MR::getStageDataHolder();
        if (zoneId != 0 && !pRoot->isPlacedZone(zoneId)) {
            return nullptr;
        }
        StageDataHolder* pZone = pRoot->getStageDataHolderFromZoneId(zoneId);
        if (pZone == nullptr || pZone->mArchive == nullptr) {
            return nullptr;
        }
        char path[160];
        snprintf(path, sizeof(path), "/%s", pPath);
        return pZone->mArchive->getResource(path);
    }

    void applyTransform(LiveActor* pActor, s32 zoneId, const f32* pPos, const f32* pDirDegrees, const f32* pScale) {
        TVec3f pos(pPos[0], pPos[1], pPos[2]);
        TVec3f rotate(pDirDegrees[0], pDirDegrees[1], pDirDegrees[2]);
        if (MR::isPlacementLocalStage()) {
            const TPos3f* pZone = MR::getZonePlacementMtx(zoneId);
            pZone->mult(pos, pos);
            // As MR::getJMapInfoRotate: zone rotation times the row's, back to Euler degrees.
            TMtx34f rotateMtx;
            MR::makeMtxRotate(rotateMtx.toMtxPtr(), rotate);
            rotateMtx.concat(*pZone, rotateMtx);
            if (-0.001f <= rotateMtx.mMtx[2][0] - 1.0f) {
                rotate.x = std::atan2(-rotateMtx.mMtx[0][1], rotateMtx.mMtx[1][1]);
                rotate.y = -1.5707964f;
                rotate.z = 0.0f;
            } else if (rotateMtx.mMtx[2][0] + 1.0f <= 0.001f) {
                rotate.x = std::atan2(rotateMtx.mMtx[0][1], rotateMtx.mMtx[1][1]);
                rotate.y = 1.5707964f;
                rotate.z = 0.0f;
            } else {
                rotate.x = std::atan2(rotateMtx.mMtx[2][1], rotateMtx.mMtx[2][2]);
                rotate.z = std::atan2(rotateMtx.mMtx[1][0], rotateMtx.mMtx[0][0]);
                rotate.y = std::asin(-rotateMtx.mMtx[2][0]);
            }
            rotate = rotate * 57.29578f;
        }
        pActor->mPosition = pos;
        pActor->mRotation = rotate;
        pActor->mScale.set(pScale[0], pScale[1], pScale[2]);
        MR::calcAnimDirect(pActor);
    }

    void sceneHeap(u32* pFree, u32* pTotal) {
        JKRSolidHeap* pHeap = MR::getSceneHeapNapa();
        *pFree = pHeap != nullptr ? static_cast< u32 >(pHeap->getFreeSize()) : 0;
        *pTotal = pHeap != nullptr ? static_cast< u32 >(reinterpret_cast< uintptr_t >(pHeap->getEndAddr()) - reinterpret_cast< uintptr_t >(pHeap->getStartAddr())) : 0;
    }

    void retire(LiveActor* pActor) {
        if (pActor == nullptr) {
            return;
        }
        if (!MR::isDead(pActor)) {
            pActor->makeActorDead();  // also takes it out of clipping (the dead list)
        }
        // Not MR::invalidateClipping: on a clipped actor (a view group hiding it, say) it calls
        // endClipped(), which connects the dead actor to the scene again and draws it.
        MR::hideModel(pActor);
    }
}  // namespace EditorScene
#endif
