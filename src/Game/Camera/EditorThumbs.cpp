#ifdef PETARI_NATIVE
// The level editor's thumbnails (Game/Camera/EditorThumbs.hpp).
#include "Game/Camera/EditorThumbs.hpp"
#include "Game/Camera/EditorCamera.hpp"
#include "Game/Camera/EditorPicking.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/LightUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/System/MainLoopFramework.hpp"
#include <JSystem/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3DGraphAnimator/J3DModelData.hpp>
#include <JSystem/J3DGraphAnimator/J3DJoint.hpp>
#include <JSystem/J3DGraphBase/J3DMaterial.hpp>
#include <JSystem/J3DGraphBase/J3DShape.hpp>
#include <JSystem/J3DGraphBase/J3DSys.hpp>
#include <petari/editor/edit_api.hpp>
#include <petari/editor_pick.hpp>
#include <revolution/gx.h>
#include <revolution/mtx.h>

#include <cmath>

extern "C" void petari_present_request_readback(const void* copy);
extern "C" void GXCreateFrameBuffer(u32 width, u32 height);  // Aurora (dolphin/gx/GXAurora.h)
extern "C" void GXRestoreFrameBuffer(void);

namespace {
    constexpr int kSize = 256;  // pixels, square (cards are 66 points, 132 pixels on Retina)
    constexpr int kSlots = 8;   // copies in flight
    alignas(32) unsigned char gCopies[kSlots][kSize * kSize * 4];
    int gNextSlot = 0;

    struct V {
        float x, y, z;
    };
    V sub(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    V add(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    V mul(V a, float s) { return {a.x * s, a.y * s, a.z * s}; }
    float dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    V cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
    V norm(V a, V fallback) {
        const float l = std::sqrt(dot(a, a));
        return l > 1e-5f ? mul(a, 1.0f / l) : fallback;
    }

    // The key colour behind the model (the readback makes it transparent), and far depth: a clearing copy
    // (no draw, so nothing waits on a pipeline compiling), then the frame's own clear values back.
    alignas(32) unsigned char gScratch[kSize * kSize * 4];
    void clearKey() {
        GXSetColorUpdate(GX_TRUE);
        GXSetAlphaUpdate(GX_TRUE);
        GXSetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
        GXSetCopyClear(GXColor{255, 0, 255, 255}, 0xFFFFFF);
        GXSetTexCopySrc(0, 0, kSize, kSize);
        GXSetTexCopyDst(kSize, kSize, GX_TF_RGBA8, GX_FALSE);
        GXCopyTex(gScratch, GX_TRUE);
        const MainLoopFramework* frame = MainLoopFramework::getManager();
        GXSetCopyClear(frame->mClearColor, frame->mClearZ);
    }

    // The model's extent in the world: its joints' boxes through their current matrices. False when the
    // model has none (then the actor's bounds frame it).
    bool modelBounds(J3DModel* model, V* center, float* radius) {
        J3DModelData* data = model->getModelData();
        V lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        for (u16 i = 0; i < data->getJointNum(); i++) {
            J3DJoint* joint = data->getJointNodePointer(i);
            const Vec* mn = joint->getMin();
            const Vec* mx = joint->getMax();
            if (mn->x > mx->x || mn->y > mx->y || mn->z > mx->z) continue;
            MtxPtr m = model->getAnmMtx(i);
            for (int corner = 0; corner < 8; corner++) {
                Vec local{corner & 1 ? mx->x : mn->x, corner & 2 ? mx->y : mn->y, corner & 4 ? mx->z : mn->z}, world;
                PSMTXMultVec(m, &local, &world);
                lo = {std::fmin(lo.x, world.x), std::fmin(lo.y, world.y), std::fmin(lo.z, world.z)};
                hi = {std::fmax(hi.x, world.x), std::fmax(hi.y, world.y), std::fmax(hi.z, world.z)};
            }
        }
        if (lo.x > hi.x) return false;
        const V extent = sub(hi, lo);
        const float r = 0.5f * std::sqrt(dot(extent, extent));
        if (!(r > 1.0f) || r > 1e6f) return false;
        *center = mul(add(lo, hi), 0.5f);
        *radius = r;
        return true;
    }

}  // namespace

namespace EditorThumbs {
    void render() {
        if (!EditorCamera::isActive()) return;
        char name[64];
        std::uint64_t rows[PetariNative::EditorPick::kThumbnailExamples] = {};
        if (!PetariNative::EditorPick::takeThumbnailRequest(name, sizeof(name), rows)) return;
        // The first example with a live model (others may be in a mission or zone not loaded).
        LiveActor* actor = nullptr;
        J3DModel* model = nullptr;
        for (std::uint64_t row : rows) {
            actor = row != 0 ? PetariEditor::actorForRow(PetariEditor::RowId{row}) : nullptr;
            model = actor != nullptr ? MR::getJ3DModel(actor) : nullptr;
            if (model != nullptr) break;
        }
        if (model == nullptr) {
            PetariNative::EditorPick::publishThumbnail(name, nullptr);
            return;
        }
        V center;
        float radius;
        if (!modelBounds(model, &center, &radius)) {
            float c[3];
            EditorPicking::bounds(actor, c, &radius);
            center = {c[0], c[1], c[2]};
        }
        radius = radius > 10.0f ? radius : 10.0f;
        // A three-quarter view from the object's front, a little above, upright for its gravity.
        TVec3f gravity(0.0f, -1.0f, 0.0f);
        MR::calcGravityVectorOrZero(actor, TVec3f(center.x, center.y, center.z), &gravity, nullptr, 0);
        const V up = norm(V{-gravity.x, -gravity.y, -gravity.z}, V{0, 1, 0});
        Mtx rotation;
        MR::makeMtxTR(rotation, 0.0f, 0.0f, 0.0f, actor->mRotation.x, actor->mRotation.y, actor->mRotation.z);
        V front = norm(V{rotation[0][2], rotation[1][2], rotation[2][2]}, V{0, 0, 1});
        front = norm(sub(front, mul(up, dot(front, up))), norm(cross(up, V{1, 0, 0}), V{0, 0, 1}));
        const V right = cross(up, front);
        const V dir = norm(add(add(front, mul(right, 0.6f)), mul(up, 0.45f)), front);
        constexpr float kFovy = 30.0f;
        const float distance = radius / std::tan(kFovy * 0.5f * 3.14159265f / 180.0f) * 1.1f;
        const V eye = add(center, mul(dir, distance));
        Mtx view;
        Vec camPos{eye.x, eye.y, eye.z}, camUp{up.x, up.y, up.z}, target{center.x, center.y, center.z};
        C_MTXLookAt(view, &camPos, &camUp, &target);
        Mtx44 projection;
        C_MTXPerspective(projection, kFovy, 1.0f, std::fmax(distance - radius * 1.5f, 1.0f), distance + radius * 1.5f);

        // Draw: the key colour, then the model with its own materials under the player's light.
        Mtx savedView;
        PSMTXCopy(j3dSys.mViewMtx, savedView);
        PSMTXCopy(view, j3dSys.mViewMtx);
        model->viewCalc();
        GXCreateFrameBuffer(kSize, kSize);  // Aurora's offscreen pass: the scene's EFB is left alone
        clearKey();
        GXSetViewport(0.0f, 0.0f, kSize, kSize, 0.0f, 1.0f);
        GXSetScissor(0, 0, kSize, kSize);
        GXSetProjection(projection, GX_PERSPECTIVE);
        MR::loadLightPlayer();
        GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
        J3DModelData* data = model->getModelData();
        j3dSys.setVtxPos(nullptr);
        for (u16 i = 0; i < data->getMaterialNum(); i++) MR::simpleDraw(model, data->getMaterialNodePointer(i));
        // Copy out, then back to the EFB.
        unsigned char* copy = gCopies[gNextSlot];
        gNextSlot = (gNextSlot + 1) % kSlots;
        GXSetTexCopySrc(0, 0, kSize, kSize);
        GXSetTexCopyDst(kSize, kSize, GX_TF_RGBA8, GX_FALSE);
        GXCopyTex(copy, GX_TRUE);
        GXRestoreFrameBuffer();
        // Back to the scene: its view (and the model's matrices for it), projection, viewport, state.
        PSMTXCopy(savedView, j3dSys.mViewMtx);
        model->viewCalc();
        MR::loadViewMtx();
        MR::loadProjectionMtx();
        MR::setDefaultViewportAndScissor();
        J3DShape::resetVcdVatCache();
        j3dSys.reinitGX();
        petari_present_request_readback(copy);
        PetariNative::EditorPick::publishThumbnail(name, copy);
    }
}  // namespace EditorThumbs
#endif
