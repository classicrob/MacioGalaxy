#ifdef PETARI_NATIVE
#include "Game/Editor/EditorTerrain.hpp"
#include "Game/Editor/EditorScene.hpp"
#include "Game/Gravity/CubeGravity.hpp"
#include "Game/Gravity/PointGravity.hpp"
#include "Game/Gravity/SegmentGravity.hpp"
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
#include "Game/System/ArchiveHolder.hpp"
#include "Game/System/FileLoader.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include <petari/editor/runtime_hooks.hpp>
#include <petari/host_allocation.hpp>
#include <vector>

namespace EditorTerrain {
    Part::Part(const char* pName)
        : LiveActor(pName), mBuiltScale(1.0f, 1.0f, 1.0f), mModel(nullptr), mGravity(nullptr), mGravityKind(GravityShape::Kind::None) {
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
        // TR, then the stretch of a scale preview (the geometry is built at mBuiltScale). Not
        // LiveActor::calcAnim: its collision matrix multiplies by mScale.x again.
        TPos3f mtx;
        MR::makeMtxTR(mtx.toMtxPtr(), this);
        const TVec3f stretch(mScale.x / mBuiltScale.x, mScale.y / mBuiltScale.y, mScale.z / mBuiltScale.z);
        for (int row = 0; row < 3; row++) {
            mtx.mMtx[row][0] *= stretch.x;
            mtx.mMtx[row][1] *= stretch.y;
            mtx.mMtx[row][2] *= stretch.z;
        }
        bool moved = false;
        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 4; col++) {
                moved = moved || mtx.mMtx[row][col] != mBaseMtx.mMtx[row][col];
            }
        }
        if (!moved) {
            return;
        }
        mBaseMtx = mtx;
        // A brush does not move in play: reset the collision's matrices outright (frozen Edit has
        // no movement step to carry them), and move its gravity with it.
        if (mCollisionParts != nullptr) {
            mCollisionParts->resetAllMtx(mBaseMtx);
        }
        if (mGravity != nullptr) {
            mGravity->updateMtx(mBaseMtx);
        }
    }

    void Part::setBuiltScale(const TVec3f& rScale) {
        mBuiltScale.set(rScale.x != 0.0f ? rScale.x : 1.0f, rScale.y != 0.0f ? rScale.y : 1.0f, rScale.z != 0.0f ? rScale.z : 1.0f);
        mBaseMtx.mMtx[0][0] += 1.0f;  // force the next calcAnim to rebuild
        MR::calcAnimDirect(this);
    }

    void Part::setModel(LiveActor* pModel) {
        if (mModel != nullptr && mModel != pModel) {
            EditorScene::retire(mModel);
        }
        mModel = pModel;
    }

    bool Part::setGravity(const GravityShape& rShape) {
        if (rShape.kind != mGravityKind && mGravity != nullptr) {
            mGravity->mAppeared = false;  // no unregister: off until the next scene
            mGravity = nullptr;
        }
        mGravityKind = rShape.kind;
        if (rShape.kind == GravityShape::Kind::None) {
            return true;
        }
        if (mGravity == nullptr) {
            if (gravityRoom() <= 0) {
                mGravityKind = GravityShape::Kind::None;
                return false;
            }
            switch (rShape.kind) {
            case GravityShape::Kind::Cube:
                mGravity = new CubeGravity();
                break;
            case GravityShape::Kind::Point:
                mGravity = new PointGravity();
                break;
            default:
                mGravity = new SegmentGravity();
                break;
            }
            mGravity->setPriority(1000);
            MR::registerGravity(mGravity);
        }
        mGravity->mRange = rShape.range;
        switch (rShape.kind) {
        case GravityShape::Kind::Cube: {
            CubeGravity* pCube = static_cast< CubeGravity* >(mGravity);
            TPos3f cube;
            cube.identity();
            cube.mMtx[0][0] = rShape.half.x;
            cube.mMtx[1][1] = rShape.half.y;
            cube.mMtx[2][2] = rShape.half.z;
            cube.setTrans(rShape.center);
            pCube->mActiveFaces = static_cast< u8 >(rShape.activeFaces);
            pCube->mCube = cube;
            break;
        }
        case GravityShape::Kind::Point:
            mGravity->mDistant = rShape.radius;
            static_cast< PointGravity* >(mGravity)->setOrig(rShape.center);
            break;
        default: {
            SegmentGravity* pSegment = static_cast< SegmentGravity* >(mGravity);
            mGravity->mDistant = rShape.radius;
            pSegment->setGravityPoint(0, rShape.a);
            pSegment->setGravityPoint(1, rShape.b);
            pSegment->setSideVector(TVec3f(1.0f, 0.0f, 0.0f));
            pSegment->setValidSideDegree(360.0f);
            pSegment->setEdgeValid(0, true);
            pSegment->setEdgeValid(1, true);
            pSegment->updateLocalParam();
            break;
        }
        }
        mGravity->updateMtx(mBaseMtx);
        return true;
    }

    void Part::retire() {
        if (mCollisionParts != nullptr) {
            MR::invalidateCollisionParts(this);
        }
        if (mGravity != nullptr) {
            mGravity->mAppeared = false;
        }
        setModel(nullptr);
        EditorScene::retire(this);
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

    void releaseArchive(const char* pPath) {
        FileLoader* pLoader = SingletonHolder< FileLoader >::get();
        pLoader->mArchiveHolder->nativeRemove(pPath);
        if (pLoader->isLoaded(pPath)) {
            pLoader->removeFile(pPath);
        }
        PetariNative::HostAllocationScope host;
        PetariEditor::Hooks::removeArchiveOverride(pPath);
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

    s32 gravityRoom() {
        return 128 - EditorScene::gravityCount();
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
