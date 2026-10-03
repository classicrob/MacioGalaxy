#ifdef PETARI_NATIVE
// The level editor's drawing in the scene (Game/Camera/EditorDraw.hpp).
#include "Game/Camera/EditorDraw.hpp"
#include "Game/Camera/EditorAreas.hpp"
#include "Game/Camera/EditorCamera.hpp"
#include "Game/Camera/EditorGravity.hpp"
#include "Game/Camera/EditorPaths.hpp"
#include "Game/Camera/EditorPicking.hpp"
#include "Game/Camera/EditorPlaytest.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include <JSystem/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3DGraphAnimator/J3DModelData.hpp>
#include <JSystem/J3DGraphBase/J3DMaterial.hpp>
#include <JSystem/J3DGraphBase/J3DPacket.hpp>
#include <JSystem/J3DGraphBase/J3DShape.hpp>
#include <JSystem/J3DGraphBase/J3DSys.hpp>
#include <revolution/gx.h>

#include <JSystem/J3DGraphAnimator/J3DModelData.hpp>
#include <JSystem/J3DGraphBase/J3DShapeDraw.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <petari/editor/array.hpp>
#include <petari/host_allocation.hpp>

namespace {
    // A flat colour, blended over what is there; no textures, no lighting.
    void setupFlat(GXColor color, GXCompare depth) {
        GXSetNumChans(1);
        GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        GXSetChanCtrl(GX_COLOR1A1, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        GXSetChanMatColor(GX_COLOR0A0, color);
        GXSetNumTexGens(0);
        GXSetNumIndStages(0);
        GXSetNumTevStages(1);
        GXSetTevDirect(GX_TEVSTAGE0);
        GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
        GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
        GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
        GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
        GXSetZMode(GX_TRUE, depth, GX_FALSE);
        GXSetZCompLoc(GX_TRUE);
        GXSetCullMode(GX_CULL_BACK);
        GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, GXColor{0, 0, 0, 0});
        GXSetColorUpdate(GX_TRUE);
        GXSetAlphaUpdate(GX_FALSE);
    }

    // EP-004: the budget for the editor's own model draws this frame (estimated index bytes). Aurora's whole
    // frame has 2 MiB of indices; the game's scene needs most of it, so the editor keeps to a slice.
    constexpr std::uint32_t kFrameBudget = 640 * 1024;
    std::uint32_t gSpent = 0;

    // The model's shapes as they were posed this frame, in the colour set up before.
    void drawShapes(LiveActor* actor) {
        if (actor == nullptr || MR::isDead(actor) || MR::isClipped(actor) || MR::isHiddenModel(actor)) {
            return;  // not drawn this frame: its draw matrices are not current
        }
        J3DModel* model = MR::getJ3DModel(actor);
        if (model == nullptr) return;
        J3DModelData* data = model->getModelData();
        J3DShape::resetVcdVatCache();
        for (u16 i = 0; i < data->getMaterialNum(); i++) {
            J3DMaterial* material = data->getMaterialNodePointer(i);
            if (material == nullptr || material->mShape == nullptr || material->mShape->checkFlag(1)) continue;
            J3DShapePacket* packet = model->getShapePacket(material->mShape->mIndex);
            packet->getShape()->loadPreDrawSetting();
            packet->drawFast();
        }
    }

    // The selection outline (TECH.md 3.4, done in screen space): mark the object's visible pixels in
    // the EFB's alpha, draw its flat silhouette shifted a few pixels each way where the mark is not
    // (blending by destination alpha), then clear the mark. GX projections are parameterised, so the
    // shift goes in the perspective's off-centre terms: a constant offset on screen at any depth.
    void outline(LiveActor* actor, GXColor color, float pixels) {
        // 1. Mark: alpha 255 where the object is visible; colour untouched.
        setupFlat(GXColor{0, 0, 0, 255}, GX_LEQUAL);
        GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
        GXSetColorUpdate(GX_FALSE);
        GXSetAlphaUpdate(GX_TRUE);
        drawShapes(actor);
        // 2. The shifted silhouettes, only outside the mark: out = src * (1 - dstA) + dst * dstA.
        setupFlat(color, GX_ALWAYS);
        GXSetBlendMode(GX_BM_BLEND, GX_BL_INVDSTALPHA, GX_BL_DSTALPHA, GX_LO_CLEAR);
        GXSetColorUpdate(GX_TRUE);
        GXSetAlphaUpdate(GX_FALSE);
        const TProj3f& projection = MR::getCameraProjectionMtx();
        const float dx = pixels * 2.0f / static_cast< float >(MR::getFrameBufferWidth());
        const float dy = pixels * 2.0f / static_cast< float >(MR::getScreenHeight());
        static const float kOffsets[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {0.7f, 0.7f}, {-0.7f, 0.7f}, {0.7f, -0.7f}, {-0.7f, -0.7f}};
        for (const auto& o : kOffsets) {
            Mtx44 shifted;
            for (int r = 0; r < 4; r++) {
                for (int c = 0; c < 4; c++) shifted[r][c] = projection.mMtx[r][c];
            }
            shifted[0][2] += o[0] * dx;
            shifted[1][2] += o[1] * dy;
            GXSetProjection(shifted, GX_PERSPECTIVE);
            drawShapes(actor);
        }
        MR::loadProjectionMtx();
        // 3. Clear the mark (the same pixels: the depth test passes exactly where it did).
        setupFlat(GXColor{0, 0, 0, 0}, GX_LEQUAL);
        GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
        GXSetColorUpdate(GX_FALSE);
        GXSetAlphaUpdate(GX_TRUE);
        drawShapes(actor);
        GXSetColorUpdate(GX_TRUE);
        GXSetAlphaUpdate(GX_FALSE);
    }

    void drawArray() {
        namespace A = PetariEditor::Arrays;
        namespace G = PetariEditor::Gizmo;
        namespace D = PetariEditor::DebugDraw;
        const auto& preview = A::view();
        if (!preview.active)
            return;
        PetariNative::HostAllocationScope host;
        D::Mesh fallback;
        int modelsLeft = 24;
        for (const auto& copy : preview.copies) {
            LiveActor* actor = PetariEditor::actorForRow(copy.source);
            J3DModel* model = actor ? MR::getJ3DModel(actor) : nullptr;
            if (modelsLeft > 0 && model && !MR::isDead(actor) && !MR::isClipped(actor) && !MR::isHiddenModel(actor) && EditorDraw::spend(model, 1)) {
                --modelsLeft;
                // Transform the view, not the actor: even animated models can be drawn as frozen copies.
                const auto rotate = [&](G::V3 v) {
                    return copy.basis.x * G::dot(copy.originalBasis.x, v) + copy.basis.y * G::dot(copy.originalBasis.y, v) +
                           copy.basis.z * G::dot(copy.originalBasis.z, v);
                };
                const auto x = rotate({1, 0, 0}), y = rotate({0, 1, 0}), z = rotate({0, 0, 1});
                const auto t = copy.position - rotate(copy.originalPosition);
                Mtx delta = {{x.x, y.x, z.x, t.x}, {x.y, y.y, z.y, t.y}, {x.z, y.z, z.z, t.z}};
                Mtx saved;
                PSMTXCopy(j3dSys.mViewMtx, saved);
                PSMTXConcat(saved, delta, j3dSys.mViewMtx);
                model->viewCalc();
                setupFlat(GXColor{100, 235, 230, 110}, GX_LEQUAL);
                drawShapes(actor);
                PSMTXCopy(saved, j3dSys.mViewMtx);
                model->viewCalc();
            } else {
                G::V3 corners[8];
                for (int i = 0; i < 8; ++i)
                    corners[i] = copy.position +
                                 (copy.basis.x * (i & 1 ? 1.f : -1.f) + copy.basis.y * (i & 2 ? 1.f : -1.f) + copy.basis.z * (i & 4 ? 1.f : -1.f)) *
                                     (copy.radius * 0.577f);
                for (int i = 0; i < 8; ++i)
                    for (int bit = 1; bit <= 4; bit *= 2)
                        if (!(i & bit))
                            fallback.lines.push_back({D::point(corners[i]), D::point(corners[i | bit]), 0xcce6eb64, 2});
            }
        }
        D::draw(fallback);
        MR::loadViewMtx();
    }

    void tint(LiveActor* actor, GXColor visible, u8 xrayAlpha) {
        GXColor xray = visible;
        xray.a = xrayAlpha;
        setupFlat(xray, GX_GREATER);  // the parts something stands in front of
        drawShapes(actor);
        setupFlat(visible, GX_LEQUAL);
        drawShapes(actor);
    }
}  // namespace

namespace EditorDraw {
    void beginFrame() { gSpent = 0; }

    std::uint32_t modelCost(J3DModel* model) {
        if (model == nullptr) return 0;
        // Each vertex reference in a display list becomes an index; strips become lists. Twice the display
        // lists' bytes covers the usual vertex formats (a few bytes a vertex, 16-bit indices).
        J3DModelData* data = model->getModelData();
        std::uint32_t bytes = 0;
        for (u16 i = 0; i < data->getShapeNum(); i++) {
            J3DShape* shape = data->getShapeNodePointer(i);
            if (shape == nullptr) continue;
            for (u16 g = 0; g < shape->getMtxGroupNum(); g++) {
                if (J3DShapeDraw* draw = shape->getShapeDraw(g)) bytes += draw->getDisplayListSize();
            }
        }
        return bytes * 2;
    }

    bool spend(J3DModel* model, int passes) {
        static const bool unlimited = std::getenv("PETARI_EDITOR_DRAW_BUDGET_OFF") != nullptr;  // tests: the pre-EP-004 behaviour
        if (unlimited) return true;
        const std::uint64_t cost = static_cast< std::uint64_t >(modelCost(model)) * static_cast< std::uint64_t >(passes);
        if (gSpent + cost > kFrameBudget) return false;
        gSpent += static_cast< std::uint32_t >(cost);
        return true;
    }

    bool spendBytes(std::uint32_t indexBytes) {
        static const bool unlimited = std::getenv("PETARI_EDITOR_DRAW_BUDGET_OFF") != nullptr;
        if (unlimited) return true;
        if (gSpent + static_cast< std::uint64_t >(indexBytes) > kFrameBudget) return false;
        gSpent += indexBytes;
        return true;
    }

    std::uint32_t remaining() { return gSpent < kFrameBudget ? kFrameBudget - gSpent : 0; }

    void draw3D() {
        if (!EditorCamera::isActive()) return;
        // The selection breathes a little so it reads against gold-coloured things.
        const double seconds =
            std::chrono::duration< double >(std::chrono::steady_clock::now().time_since_epoch()).count();
        const u8 pulse = static_cast< u8 >(60.0 + 20.0 * std::sin(seconds * 4.0));
        LiveActor* hover = EditorPicking::hoverActor();
        bool hoverSelected = false;
        // Within the frame's budget: the tint (2 draws) first, then the outline (10) if it still fits. A model too
        // big for either (a whole planet) keeps only the viewport's ring and label.
        auto modelOf = [](LiveActor* actor) { return actor != nullptr ? MR::getJ3DModel(actor) : nullptr; };
        for (int i = 0; i < EditorPicking::selectedCount(); i++) {
            LiveActor* actor = EditorPicking::selectedAt(i);
            hoverSelected = hoverSelected || actor == hover;
            if (spend(modelOf(actor), 2)) tint(actor, GXColor{250, 204, 56, static_cast< u8 >(pulse / 2)}, 40);
            if (spend(modelOf(actor), 10)) outline(actor, GXColor{250, 204, 56, 255}, 2.5f);
        }
        if (hover != nullptr && !hoverSelected && spend(modelOf(hover), 10)) outline(hover, GXColor{102, 219, 255, 255}, 1.5f);
        drawArray();            // Array: budgeted render-only copies, no temporary actors.
        EditorPlaytest::draw();  // M3 play-test: recorded trail and jump peaks.
        EditorGravity::draw();  // M3 gravity: translucent fields before restoring GX.
        EditorAreas::draw();  // M3 areas: shared depth-tested volumes.
        EditorPaths::draw();  // M3 paths: depth-tested rails and wires.
        // Leave the state as the game's own draw passes expect it.
        GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
        GXSetAlphaUpdate(GX_FALSE);
        GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
        J3DShape::resetVcdVatCache();
        j3dSys.reinitGX();
    }
}  // namespace EditorDraw
#endif
