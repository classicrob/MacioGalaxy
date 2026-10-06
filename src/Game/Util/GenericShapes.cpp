// Native mods GenericCharacter and GenericWorld (include/Game/Util/GenericShapes.hpp).
#ifdef PETARI_NATIVE
#include "Game/Util/GenericShapes.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/LightUtil.hpp"

#include <cmath>

namespace {
    constexpr int kRings = 10;  // the unit sphere: latitude rings
    constexpr int kSegs = 16;   // and longitude segments (also the round sides)
    constexpr f32 kTwoPi = 6.2831853f;

    // The unit sphere's point at ring i (0 = top), segment j.
    TVec3f spherePoint(int i, int j) {
        const f32 lat = 3.14159265f * (0.5f - static_cast< f32 >(i) / kRings);
        const f32 lon = kTwoPi * static_cast< f32 >(j) / kSegs;
        return TVec3f(std::cos(lat) * std::sin(lon), std::sin(lat), std::cos(lat) * std::cos(lon));
    }

    void sendVertex(const TVec3f& rPos, const TVec3f& rNormal) {
        GXPosition3f32(rPos.x, rPos.y, rPos.z);
        GXNormal3f32(rNormal.x, rNormal.y, rNormal.z);
    }

    // One flat triangle in m's space, its normal from the winding (either side lit the same: no culling).
    void sendFlat(const Mtx m, const TVec3f& a, const TVec3f& b, const TVec3f& c) {
        TVec3f n;
        n.cross(b - a, c - a);
        n = GenericShapes::unitNormal(GenericShapes::rotate(m, n));
        sendVertex(GenericShapes::apply(m, a), n);
        sendVertex(GenericShapes::apply(m, b), n);
        sendVertex(GenericShapes::apply(m, c), n);
    }
}  // namespace

namespace GenericShapes {
    TVec3f apply(const Mtx m, const TVec3f& v) {
        return TVec3f(m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3], m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3],
                      m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3]);
    }

    TVec3f rotate(const Mtx m, const TVec3f& v) {
        return TVec3f(m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                      m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z);
    }

    TVec3f unitNormal(const TVec3f& v) {
        TVec3f n(v);
        const f32 length = n.length();
        if (length > 0.0001f) {
            n *= 1.0f / length;
        } else {
            n.set(0.0f, 1.0f, 0.0f);
        }
        return n;
    }

    void begin(bool lightPlayer) {
        TDDraw::cameraInit3D();
        TDDraw::setup(0, 0, 0);
        if (lightPlayer) {
            MR::loadLightPlayer();  // LIGHT0/1 and the ambient colour (after setup, which clears it)
        }
        GXClearVtxDesc();
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        GXSetVtxDesc(GX_VA_NRM, GX_DIRECT);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0);
        GXSetNumChans(1);
        GXSetNumTexGens(0);
        GXSetNumTevStages(1);
        GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
        GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
        GXSetChanCtrl(GX_COLOR0, GX_TRUE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT0 | GX_LIGHT1, GX_DF_CLAMP, GX_AF_NONE);
        GXSetChanCtrl(GX_ALPHA0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
        GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, GXColor{0, 0, 0, 0});
        GXSetCullMode(GX_CULL_NONE);
        GXSetZCompLoc(GX_TRUE);
        GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
        GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    }

    void end() {
        MR::reinitGX();
        MR::loadViewMtx();
        MR::loadProjectionMtx();
    }

    void color(const GXColor& rColor) {
        GXSetChanMatColor(GX_COLOR0A0, rColor);
    }

    void ellipsoid(const Mtx m, const TVec3f& c, const TVec3f& r, bool dome) {
        const int rings = dome ? kRings / 2 : kRings;
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, static_cast< u16 >(rings * kSegs * 6));
        for (int i = 0; i < rings; ++i) {
            for (int j = 0; j < kSegs; ++j) {
                const TVec3f u[4] = {spherePoint(i, j), spherePoint(i + 1, j), spherePoint(i + 1, j + 1), spherePoint(i, j + 1)};
                TVec3f pos[4], nrm[4];
                for (int k = 0; k < 4; ++k) {
                    pos[k] = apply(m, TVec3f(c.x + r.x * u[k].x, c.y + r.y * u[k].y, c.z + r.z * u[k].z));
                    nrm[k] = unitNormal(rotate(m, TVec3f(u[k].x / r.x, u[k].y / r.y, u[k].z / r.z)));
                }
                const int order[6] = {0, 1, 2, 0, 2, 3};
                for (int k : order) {
                    sendVertex(pos[k], nrm[k]);
                }
            }
        }
        GXEnd();
    }

    void capsule(const TVec3f& a, f32 ra, const TVec3f& b, f32 rb) {
        Mtx identity;
        PSMTXIdentity(identity);
        ellipsoid(identity, a, TVec3f(ra, ra, ra), false);
        ellipsoid(identity, b, TVec3f(rb, rb, rb), false);
        TVec3f axis(b - a);
        if (axis.length() < 0.01f) {
            return;
        }
        axis = unitNormal(axis);
        TVec3f helper = std::fabs(axis.y) < 0.9f ? TVec3f(0.0f, 1.0f, 0.0f) : TVec3f(1.0f, 0.0f, 0.0f);
        TVec3f e1;
        e1.cross(axis, helper);
        e1 = unitNormal(e1);
        TVec3f e2;
        e2.cross(axis, e1);
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, static_cast< u16 >(kSegs * 6));
        for (int j = 0; j < kSegs; ++j) {
            const f32 t0 = kTwoPi * static_cast< f32 >(j) / kSegs;
            const f32 t1 = kTwoPi * static_cast< f32 >(j + 1) / kSegs;
            const TVec3f n0(e1 * std::cos(t0) + e2 * std::sin(t0));
            const TVec3f n1(e1 * std::cos(t1) + e2 * std::sin(t1));
            const TVec3f a0(a + n0 * ra), a1(a + n1 * ra), b0(b + n0 * rb), b1(b + n1 * rb);
            sendVertex(a0, n0);
            sendVertex(b0, n0);
            sendVertex(b1, n1);
            sendVertex(a0, n0);
            sendVertex(b1, n1);
            sendVertex(a1, n1);
        }
        GXEnd();
    }

    void cylinder(const Mtx m, const TVec3f& c, f32 radius, f32 halfHeight) {
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, static_cast< u16 >(kSegs * 12));
        const TVec3f top(c.x, c.y + halfHeight, c.z), bottom(c.x, c.y - halfHeight, c.z);
        for (int j = 0; j < kSegs; ++j) {
            const f32 t0 = kTwoPi * static_cast< f32 >(j) / kSegs;
            const f32 t1 = kTwoPi * static_cast< f32 >(j + 1) / kSegs;
            const TVec3f d0(std::cos(t0) * radius, 0.0f, std::sin(t0) * radius);
            const TVec3f d1(std::cos(t1) * radius, 0.0f, std::sin(t1) * radius);
            const TVec3f n0(unitNormal(rotate(m, d0))), n1(unitNormal(rotate(m, d1)));
            const TVec3f a0(apply(m, top + d0)), a1(apply(m, top + d1)), b0(apply(m, bottom + d0)), b1(apply(m, bottom + d1));
            sendVertex(a0, n0);
            sendVertex(b0, n0);
            sendVertex(b1, n1);
            sendVertex(a0, n0);
            sendVertex(b1, n1);
            sendVertex(a1, n1);
            sendFlat(m, top, top + d1, top + d0);
            sendFlat(m, bottom, bottom + d0, bottom + d1);
        }
        GXEnd();
    }

    void starPrism(const Mtx m, const TVec3f& c, f32 outer, f32 inner, f32 halfDepth) {
        TVec3f rim[10];
        for (int i = 0; i < 10; ++i) {
            const f32 angle = kTwoPi * static_cast< f32 >(i) / 10.0f;
            const f32 r = (i & 1) != 0 ? inner : outer;
            rim[i] = TVec3f(c.x + std::sin(angle) * r, c.y + std::cos(angle) * r, c.z);
        }
        const TVec3f front(0.0f, 0.0f, halfDepth), back(0.0f, 0.0f, -halfDepth);
        // Each face a fan from a raised centre (a slightly domed star), plus the sides.
        const TVec3f frontTip(c.x, c.y, c.z + halfDepth * 2.0f), backTip(c.x, c.y, c.z - halfDepth * 2.0f);
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, static_cast< u16 >(10 * 12));
        for (int i = 0; i < 10; ++i) {
            const TVec3f a(rim[i]), b(rim[(i + 1) % 10]);
            sendFlat(m, frontTip, a + front, b + front);
            sendFlat(m, backTip, b + back, a + back);
            sendFlat(m, a + front, a + back, b + back);
            sendFlat(m, a + front, b + back, b + front);
        }
        GXEnd();
    }
    void box(const Mtx m, const TVec3f& lo, const TVec3f& hi) {
        const TVec3f c[8] = {TVec3f(lo.x, lo.y, lo.z), TVec3f(hi.x, lo.y, lo.z), TVec3f(hi.x, hi.y, lo.z), TVec3f(lo.x, hi.y, lo.z),
                             TVec3f(lo.x, lo.y, hi.z), TVec3f(hi.x, lo.y, hi.z), TVec3f(hi.x, hi.y, hi.z), TVec3f(lo.x, hi.y, hi.z)};
        const int faces[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {3, 7, 6, 2}, {0, 4, 7, 3}, {1, 2, 6, 5}};
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, 36);
        for (const auto& f : faces) {
            sendFlat(m, c[f[0]], c[f[1]], c[f[2]]);
            sendFlat(m, c[f[0]], c[f[2]], c[f[3]]);
        }
        GXEnd();
    }

    void arrowPrism(const Mtx m, f32 y0, f32 headY, f32 y1, f32 shaftHalf, f32 headHalf, f32 halfDepth) {
        // The outline (counter-clockwise from the shaft's back left), as triangles per face plus the sides.
        const TVec3f outline[7] = {TVec3f(-shaftHalf, y0, 0.0f),  TVec3f(shaftHalf, y0, 0.0f),     TVec3f(shaftHalf, headY, 0.0f),
                                   TVec3f(headHalf, headY, 0.0f), TVec3f(0.0f, y1, 0.0f),          TVec3f(-headHalf, headY, 0.0f),
                                   TVec3f(-shaftHalf, headY, 0.0f)};
        const int tris[5][3] = {{0, 1, 2}, {0, 2, 6}, {3, 4, 5}, {2, 3, 5}, {2, 5, 6}};
        const TVec3f front(0.0f, 0.0f, halfDepth), back(0.0f, 0.0f, -halfDepth);
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, static_cast< u16 >((5 * 2 + 7 * 2) * 3));
        for (const auto& t : tris) {
            sendFlat(m, outline[t[0]] + front, outline[t[1]] + front, outline[t[2]] + front);
            sendFlat(m, outline[t[2]] + back, outline[t[1]] + back, outline[t[0]] + back);
        }
        for (int i = 0; i < 7; ++i) {
            const TVec3f a(outline[i]), b(outline[(i + 1) % 7]);
            sendFlat(m, a + front, a + back, b + back);
            sendFlat(m, a + front, b + back, b + front);
        }
        GXEnd();
    }
}  // namespace GenericShapes

#endif
