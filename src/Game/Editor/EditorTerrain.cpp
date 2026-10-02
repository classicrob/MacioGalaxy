#ifdef PETARI_NATIVE
#include "Game/Editor/EditorTerrain.hpp"
#include "Game/Editor/EditorScene.hpp"
#include "Game/Gravity/CubeGravity.hpp"
#include "Game/Gravity/PointGravity.hpp"
#include "Game/LiveActor/AllLiveActorGroup.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include <petari/editor/runtime_hooks.hpp>
#include <petari/host_allocation.hpp>
#include <vector>

namespace EditorTerrain {
    Part::Part(const char* pName) : LiveActor(pName) {
        mBaseMtx.identity();
    }

    void Part::init(const JMapInfoIter& rIter) {
        // Movement and calcAnim keep the collision matrix current (LiveActor::calcAnim); no
        // model yet, so no draw.
        MR::connectToScene(this, MR::MovementType_MapObj, MR::CalcAnimType_MapObj, MR::DrawBufferType_None, MR::DrawType_None);
        MR::makeMtxTR(mBaseMtx.toMtxPtr(), this);
        initHitSensor(1);
        MR::addBodyMessageSensorMapObj(this);
        MR::invalidateClipping(this);
        makeActorAppeared();
    }

    MtxPtr Part::getBaseMtx() const {
        return const_cast< TPos3f& >(mBaseMtx).toMtxPtr();
    }

    void Part::calcAnim() {
        MR::makeMtxTR(mBaseMtx.toMtxPtr(), this);
        LiveActor::calcAnim();
    }

    void Part::setCollision(void* pKcl, const void* pPa) {
        if (mCollisionParts != nullptr) {
            MR::invalidateCollisionParts(this);
        }
        // The new parts belong to the zone the part was placed in (CollisionParts::init reads the
        // current placement zone).
        MR::setCurrentPlacementZoneId(0);
        CollisionParts* pParts = new CollisionParts();
        TPos3f mtx;
        MR::makeMtxTRS(mtx.toMtxPtr(), this);
        pParts->init(mtx, getSensor("body"), pKcl, pPa, 0, false);
        MR::clearCurrentPlacementZoneId();
        mCollisionParts = pParts;
        if (!MR::isDead(this)) {
            MR::validateCollisionParts(this);
        }
    }

    Part* createPart(const char* pName, const TVec3f& rPos, void* pKcl, const void* pPa, const char** ppError) {
        AllLiveActorGroup* pGroup = MR::getAllLiveActorGroup();
        if (pGroup->getObjNum() >= EditorScene::liveActorLimit() - 64) {
            *ppError = "the scene's actor limit is reached";
            return nullptr;
        }
        const s32 executeBefore = EditorScene::executeCount();
        MR::setCurrentPlacementZoneId(0);
        MR::startInitLiveActorSystemInfo();
        Part* pPart = new Part(pName);
        pPart->mPosition = rPos;
        pPart->initWithoutIter();
        MR::initLiveActorSystemInfo(JMapInfoIter());
        pPart->initAfterPlacement();
        MR::clearCurrentPlacementZoneId();
        EditorScene::lateConnect(executeBefore);
        pPart->setCollision(pKcl, pPa);
        return pPart;
    }

    void injectArchive(const char* pPath, const void* pBytes, u32 size) {
        // The editor's archive registry (also reload's edited stages): the next mount of the path
        // (ModelObj, placement) takes these bytes (EditorReload::injectOverriddenArchive), and
        // MR::isFileExist answers for it. Overrides last for the session.
        PetariNative::HostAllocationScope host;
        const u8* pData = static_cast< const u8* >(pBytes);
        PetariEditor::Hooks::setArchiveOverride(pPath, std::vector< u8 >(pData, pData + size));
    }

    LiveActor* createModel(const char* pName, const char* pModelName, MtxPtr pMtx, const char** ppError) {
        AllLiveActorGroup* pGroup = MR::getAllLiveActorGroup();
        if (pGroup->getObjNum() >= EditorScene::liveActorLimit() - 64) {
            *ppError = "the scene's actor limit is reached";
            return nullptr;
        }
        const s32 executeBefore = EditorScene::executeCount();
        MR::setCurrentPlacementZoneId(0);
        MR::startInitLiveActorSystemInfo();
        ModelObj* pModel = new ModelObj(pName, pModelName, pMtx, MR::DrawBufferType_Planet, MR::MovementType_Planet, MR::CalcAnimType_Planet, false);
        pModel->initWithoutIter();
        MR::initLiveActorSystemInfo(JMapInfoIter());
        pModel->initAfterPlacement();
        MR::clearCurrentPlacementZoneId();
        EditorScene::lateConnect(executeBefore);
        MR::invalidateClipping(pModel);
        return pModel;
    }

    PlanetGravity* addCubeGravity(const TVec3f& rCenter, const TVec3f& rHalf, f32 range, s32 priority) {
        // CubeGravityCreator: the cube is the unit cube scaled to the half extents.
        CubeGravity* pGravity = new CubeGravity();
        TPos3f cube;
        cube.identity();
        cube.mMtx[0][0] = rHalf.x;
        cube.mMtx[1][1] = rHalf.y;
        cube.mMtx[2][2] = rHalf.z;
        cube.setTrans(rCenter);
        pGravity->mActiveFaces = 0x3F;
        pGravity->mRange = range;
        pGravity->setPriority(priority);
        pGravity->setCube(cube);
        MR::registerGravity(pGravity);
        return pGravity;
    }

    PlanetGravity* addPointGravity(const TVec3f& rCenter, f32 radius, f32 range, s32 priority) {
        // PointGravityCreator: Distant is the radius, so Range counts from the surface.
        PointGravity* pGravity = new PointGravity();
        pGravity->mDistant = radius;
        pGravity->setOrig(rCenter);
        pGravity->mRange = range;
        pGravity->setPriority(priority);
        pGravity->updateIdentityMtx();
        MR::registerGravity(pGravity);
        return pGravity;
    }
}  // namespace EditorTerrain
#endif
