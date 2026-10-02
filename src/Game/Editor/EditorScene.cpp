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
        pExec->nativeLateConnect(executeBefore);
        refreshViewGroups();

        pOut->mObj = pObj;
        pOut->mActorCount = pGroup->getObjNum() - actorsBefore;
        pOut->mFirstActor = pOut->mActorCount > 0 ? pGroup->getActor(actorsBefore) : nullptr;
        return true;
    }

    void retire(LiveActor* pActor) {
        if (pActor == nullptr) {
            return;
        }
        if (!MR::isDead(pActor)) {
            pActor->makeActorDead();
        }
        MR::invalidateClipping(pActor);
        MR::hideModel(pActor);
    }
}  // namespace EditorScene
#endif
