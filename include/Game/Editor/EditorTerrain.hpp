#pragma once

#ifdef PETARI_NATIVE
#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry.hpp>

class PlanetGravity;

// The level editor's terrain in a running scene (docs/dev/level-editor/TECH.md 6.3): a part whose
// collision is a generated KCL in memory, so a brush is walkable the moment it is built. Native
// only; called with the CPU baton held, in a scene that has finished loading.
namespace EditorTerrain {
    class Part : public LiveActor {
    public:
        Part(const char* pName);

        virtual void init(const JMapInfoIter& rIter);
        /// The collision matrix (no model; LiveActor's would read the model's).
        virtual MtxPtr getBaseMtx() const;
        virtual void calcAnim();

        /// Replaces the part's collision with `pKcl` (a host-order KCL: PetariNative::KCL::
        /// convertInPlace already ran) and its .pa `pPa` (may be null). Both are used in place and
        /// must outlive the scene. The old collision parts stop colliding (nothing is freed
        /// mid-scene, TECH.md 2.3).
        void setCollision(void* pKcl, const void* pPa);

        TPos3f mBaseMtx;  // translation and rotation, from mPosition and mRotation
    };

    /// Creates a part at `rPos` (world space, zone 0) with the given collision, and connects it to
    /// the scene like EditorScene::createFromRow. Null and *ppError on failure.
    Part* createPart(const char* pName, const TVec3f& rPos, void* pKcl, const void* pPa, const char** ppError);

    /// Serves `pBytes` (a plain RARC; copied to the scene heap) as the archive `pPath`
    /// ("/ObjectData/EditorTerrainBox.arc") for the rest of the scene: the game's loaders find it
    /// as if it were on the disc (MR::isFileExist answers for it, and it is already mounted).
    void injectArchive(const char* pPath, const void* pBytes, u32 size);
    /// True for an archive injectArchive served to the current scene.
    bool isInjectedArchive(const char* pPath);

    /// A ModelObj drawing `pModelName` (archive /ObjectData/<pModelName>.arc) with the matrix
    /// `pMtx` (e.g. a Part's mBaseMtx, so the model follows it), in the Planet draw buffer.
    /// Connected like createPart. Null and *ppError on failure.
    LiveActor* createModel(const char* pName, const char* pModelName, MtxPtr pMtx, const char** ppError);

    /// Gravity fitted to a brush, as the placement rows GlobalCubeGravity and GlobalPointGravity
    /// would make it (GravityCreator), registered with the scene's gravity manager. The manager
    /// holds 128 and never unregisters (TECH.md 2.4).
    PlanetGravity* addCubeGravity(const TVec3f& rCenter, const TVec3f& rHalf, f32 range, s32 priority);
    PlanetGravity* addPointGravity(const TVec3f& rCenter, f32 radius, f32 range, s32 priority);
}  // namespace EditorTerrain

// The terrain live check (src/Game/Editor/EditorTerrainTest.cpp); GameScene::update calls it.
namespace EditorTerrainTest {
    void update(bool actionNerve);
}
#endif
