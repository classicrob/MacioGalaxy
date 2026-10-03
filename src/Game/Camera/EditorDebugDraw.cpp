#ifdef PETARI_NATIVE
#include "Game/Camera/EditorDraw.hpp"
#include "Game/Util/DirectDraw.hpp"
#include <cassert>
#include <petari/editor/debug_draw.hpp>
#include <revolution/gx.h>

namespace PetariEditor::DebugDraw {
    namespace {
        void vertex(Vec3 p, std::uint32_t color, float alpha) {
            GXPosition3f32(p.x, p.y, p.z);
            GXColor4u8(color & 255, (color >> 8) & 255, (color >> 16) & 255, static_cast< u8 >(((color >> 24) & 255) * alpha));
        }
        void pass(const Mesh& mesh, GXCompare depth, float alpha) {
            GXSetZMode(GX_TRUE, depth, GX_FALSE);
            for (std::size_t base = 0; base < mesh.triangles.size(); base += kMaxBatchTriangles) {
                const std::size_t count = std::min< std::size_t >(kMaxBatchTriangles, mesh.triangles.size() - base);
                assert(count > 0 && count * 3 <= kMaxBatchVertices);
                if (!EditorDraw::spendBytes(static_cast< std::uint32_t >(count * 6 + 16))) {
                    break;
                }
                GXBegin(GX_TRIANGLES, GX_VTXFMT0, static_cast< u16 >(count * 3));
                for (std::size_t i = base; i < base + count; ++i) {
                    const auto& t = mesh.triangles[i];
                    vertex(t.a, t.color, alpha);
                    vertex(t.b, t.color, alpha);
                    vertex(t.c, t.color, alpha);
                }
                GXEnd();
            }
            for (std::size_t base = 0; base < mesh.lines.size();) {
                const u8 width = lineWidth(mesh.lines[base].width);
                const std::size_t end = lineBatchEnd(mesh.lines, base);
                assert(end > base && (end - base) * 4 <= kMaxBatchVertices);
                if (!EditorDraw::spendBytes(static_cast< std::uint32_t >((end - base) * 12 + 16))) {
                    break;
                }
                GXSetLineWidth(width, GX_TO_ZERO);
                GXBegin(GX_LINES, GX_VTXFMT0, static_cast< u16 >((end - base) * 2));
                for (std::size_t i = base; i < end; ++i) {
                    vertex(mesh.lines[i].a, mesh.lines[i].color, alpha);
                    vertex(mesh.lines[i].b, mesh.lines[i].color, alpha);
                }
                GXEnd();
                base = end;
            }
        }
    }  // namespace
    void draw(const Mesh& mesh, float xrayAlpha) {
        if (mesh.lines.empty() && mesh.triangles.empty())
            return;
        TDDraw::setup(0, 1, 0);
        GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
        GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
        GXSetFog(GX_FOG_NONE, 0, 0, 0, 0, GXColor{0, 0, 0, 0});
        pass(mesh, GX_LEQUAL, 1);
        if (xrayAlpha > 0)
            pass(mesh, GX_GREATER, std::clamp(xrayAlpha, 0.0f, 1.0f));
        TDDraw::close();
    }
}  // namespace PetariEditor::DebugDraw
#endif
