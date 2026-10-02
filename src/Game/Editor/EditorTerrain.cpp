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
#include "Game/System/ArchiveHolder.hpp"
#include "Game/System/FileLoader.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include <JSystem/JKernel/JKRSolidHeap.hpp>
#include <cstdio>
#include <cstring>
#include <strings.h>

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

    namespace {
        const s32 kInjectedMax = 16;
        char sInjected[kInjectedMax][64];
        s32 sInjectedCount = 0;
    }  // namespace

    void injectArchive(const char* pPath, const void* pBytes, u32 size) {
        FileLoader* pLoader = SingletonHolder< FileLoader >::get();
        if (pLoader->isLoaded(pPath)) {
            return;  // already served (or loaded from the disc) in this scene
        }
        if (sInjectedCount >= kInjectedMax && !isInjectedArchive(pPath)) {
            return;
        }
        const s32 mounted = pLoader->mArchiveHolder->mEntries.size();
        if (mounted >= pLoader->mArchiveHolder->mEntries.capacity()) {
            std::fprintf(stderr, "[editor-terrain] cannot serve %s: the game's %d mounted archives are in use\n", pPath, mounted);
            return;
        }
        // As EditorReload::injectOverriddenArchives: the file, then its archive, on the scene heap
        // (both go when the scene ends). A path that is not on the disc is told apart by name in
        // FileHolder::findEntry (native).
        JKRHeap* pHeap = MR::getSceneHeapGDDR3();
        u8* pData = new (pHeap, 32) u8[size];
        std::memcpy(pData, pBytes, size);
        OSLockMutex(&pLoader->mMutex);
        pLoader->mFileHolder->add(pPath, pHeap, pData);
        OSUnlockMutex(&pLoader->mMutex);
        pLoader->createAndAddArchive(pData, pHeap, pPath);
        if (!isInjectedArchive(pPath)) {
            snprintf(sInjected[sInjectedCount++], sizeof(sInjected[0]), "%s", pPath);
        }
        std::fprintf(stderr, "[editor-terrain] served %s from memory: %u bytes; mounted archives %d of %d\n", pPath, size,
                     pLoader->mArchiveHolder->mEntries.size(), pLoader->mArchiveHolder->mEntries.capacity());
    }

    bool isInjectedArchive(const char* pPath) {
        for (s32 i = 0; i < sInjectedCount; i++) {
            if (strcasecmp(sInjected[i], pPath) == 0) {
                // Only while the scene that loaded it lives (it goes with the scene heap).
                return SingletonHolder< FileLoader >::get()->isLoaded(pPath);
            }
        }
        return false;
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
