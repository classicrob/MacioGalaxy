#pragma once

#ifdef PETARI_NATIVE
#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry.hpp>

class J3DModel;
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

        /// The scale the collision and model were built at (a brush row's scale). While mScale
        /// differs (a gizmo scale preview), the model and collision stretch by mScale / this.
        void setBuiltScale(const TVec3f& rScale);

        /// The model drawn with this part's matrix (a ModelObj from createModel), replacing and
        /// retiring the previous one.
        void setModel(LiveActor* pModel);

        /// A gravity field in the part's own space that follows its matrix (the brush's fit).
        /// The scene's gravity manager holds 128 and never unregisters (TECH.md 2.4), so the part
        /// keeps one field and changes it in place; a different kind retires the old one.
        struct GravityShape {
            enum class Kind { None, Cube, Point, Segment } kind = Kind::None;
            TVec3f center, half, a, b;
            u32 activeFaces = 0x3F;
            f32 radius = 0.0f;
            f32 range = 0.0f;
        };
        /// False when a new field is needed and the scene has no room for it.
        bool setGravity(const GravityShape& rShape);

        void retire();  // dead, hidden, no collision, gravity off; with its model

        TPos3f mBaseMtx;  // TR from mPosition and mRotation, times the preview stretch
        TVec3f mBuiltScale;
        LiveActor* mModel;
        PlanetGravity* mGravity;
        GravityShape::Kind mGravityKind;
    };

    /// Creates a part at `rPos` (world space, zone 0) with the given collision, and connects it to
    /// the scene like EditorScene::createFromRow. Null and *ppError on failure.
    Part* createPart(const char* pName, const TVec3f& rPos, void* pKcl, const void* pPa, const char** ppError);

    /// The model drawn for `pActor` when it is one of this scene's parts (a brush row's actor
    /// has no model of its own: the editor's thumbnails), else null.
    J3DModel* partModel(const LiveActor* pActor);

    /// Serves `pBytes` (a plain RARC) as the archive `pPath` ("/ObjectData/EditorTerrainBox.arc"):
    /// it is registered with the editor's archive overrides (PetariEditor::Hooks), so the game's
    /// loaders mount it from memory as if it were on the disc.
    void injectArchive(const char* pPath, const void* pBytes, u32 size);
    /// Unmounts an archive injectArchive served once its model has been replaced (the retired
    /// actor keeps its model data: only the archive slot, of the game's 384, is freed).
    void releaseArchive(const char* pPath);

    /// A ModelObj drawing `pModelName` (archive /ObjectData/<pModelName>.arc) with the matrix
    /// `pMtx` (e.g. a Part's mBaseMtx, so the model follows it), in the Planet draw buffer.
    /// Connected like createPart. Null and *ppError on failure.
    LiveActor* createModel(const char* pName, const char* pModelName, MtxPtr pMtx, const char** ppError);

    /// Room left in the scene's gravity manager (128 fields, never unregistered).
    s32 gravityRoom();

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
