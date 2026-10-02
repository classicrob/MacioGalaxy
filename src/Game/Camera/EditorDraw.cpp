#ifdef PETARI_NATIVE
// The level editor's drawing in the scene (Game/Camera/EditorDraw.hpp).
#include "Game/Camera/EditorDraw.hpp"
#include "Game/Camera/EditorCamera.hpp"
#include "Game/Camera/EditorPicking.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include <JSystem/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3DGraphAnimator/J3DModelData.hpp>
#include <JSystem/J3DGraphBase/J3DMaterial.hpp>
#include <JSystem/J3DGraphBase/J3DPacket.hpp>
#include <JSystem/J3DGraphBase/J3DShape.hpp>
#include <JSystem/J3DGraphBase/J3DSys.hpp>
#include <revolution/gx.h>

#include <chrono>
#include <cmath>

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
    void draw3D() {
        if (!EditorCamera::isActive()) return;
        // The selection breathes a little so it reads against gold-coloured things.
        const double seconds =
            std::chrono::duration< double >(std::chrono::steady_clock::now().time_since_epoch()).count();
        const u8 pulse = static_cast< u8 >(60.0 + 20.0 * std::sin(seconds * 4.0));
        LiveActor* hover = EditorPicking::hoverActor();
        bool hoverSelected = false;
        for (int i = 0; i < EditorPicking::selectedCount(); i++) {
            LiveActor* actor = EditorPicking::selectedAt(i);
            hoverSelected = hoverSelected || actor == hover;
            tint(actor, GXColor{250, 204, 56, pulse}, 40);
        }
        if (hover != nullptr && !hoverSelected) tint(hover, GXColor{102, 219, 255, 70}, 25);
        // Leave the state as the game's own draw passes expect it.
        GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
        GXSetAlphaUpdate(GX_FALSE);
        GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
        J3DShape::resetVcdVatCache();
        j3dSys.reinitGX();
    }
}  // namespace EditorDraw
#endif
