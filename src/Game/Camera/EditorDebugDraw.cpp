#ifdef PETARI_NATIVE
#include <petari/editor/debug_draw.hpp>
#include "Game/Util/DirectDraw.hpp"
#include <revolution/gx.h>

namespace PetariEditor::DebugDraw {
namespace {
void vertex(Vec3 p, std::uint32_t color, float alpha) {
    GXPosition3f32(p.x, p.y, p.z);
    GXColor4u8(color & 255, (color >> 8) & 255, (color >> 16) & 255,
               static_cast<u8>(((color >> 24) & 255) * alpha));
}
void pass(const Mesh& mesh, GXCompare depth, float alpha) {
    GXSetZMode(GX_TRUE, depth, GX_FALSE);
    for (std::size_t base = 0; base < mesh.triangles.size(); base += 20000) {
        const std::size_t count = std::min<std::size_t>(20000, mesh.triangles.size() - base);
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, static_cast<u16>(count * 3));
        for (std::size_t i = base; i < base + count; ++i) {
            const auto& t = mesh.triangles[i];
            vertex(t.a,t.color,alpha); vertex(t.b,t.color,alpha); vertex(t.c,t.color,alpha);
        }
        GXEnd();
    }
    for (const Line& line : mesh.lines) {
        GXSetLineWidth(static_cast<u8>(std::clamp(line.width * 6.0f, 6.0f, 255.0f)), GX_TO_ZERO);
        GXBegin(GX_LINES, GX_VTXFMT0, 2);
        vertex(line.a,line.color,alpha); vertex(line.b,line.color,alpha);
        GXEnd();
    }
}
}
void draw(const Mesh& mesh, float xrayAlpha) {
    if (mesh.lines.empty() && mesh.triangles.empty()) return;
    TDDraw::setup(0, 1, 0);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetFog(GX_FOG_NONE, 0, 0, 0, 0, GXColor{0,0,0,0});
    if (xrayAlpha > 0) pass(mesh, GX_GREATER, std::clamp(xrayAlpha, 0.0f, 1.0f));
    pass(mesh, GX_LEQUAL, 1);
    TDDraw::close();
}
}  // namespace PetariEditor::DebugDraw
#endif
