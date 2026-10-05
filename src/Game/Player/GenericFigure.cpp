// Native mod: Generic character (include/Game/Player/GenericFigure.hpp). The
// figure is drawn from our own geometry: ellipsoids and capsules in flat
// colours, lit by the player lights, placed on the joints of the player model
// in use. Every player model (Mario, Luigi, Bee, Ice, Rainbow, Spring) shares
// Mario's 30-joint skeleton, so one table of parts fits all of them.
#ifdef PETARI_NATIVE
#include "Game/Player/GenericFigure.hpp"
#include "Game/Player/J3DModelX.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioCappy.hpp"
#include "Game/Player/MarioParts.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/LightUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include <JSystem/J3DGraphAnimator/J3DJoint.hpp>
#include <JSystem/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3DGraphAnimator/J3DModelData.hpp>
#include <JSystem/J3DGraphAnimator/J3DMtxBuffer.hpp>
#include <JSystem/J3DGraphBase/J3DTransform.hpp>
#include <JSystem/JUtility/JUTNameTab.hpp>
#include <petari/mods.hpp>

#include <cmath>

namespace {
    constexpr int kMaxJoints = 64;
    constexpr int kRings = 10;  // the unit sphere: latitude rings
    constexpr int kSegs = 16;   // and longitude segments (also the capsules' sides)

    enum Slot : u8 { Skin, Suit, Shoe, Hat, Dark, SlotCount };

    struct Palette {
        GXColor c[SlotCount];
    };

    // Bland: no red, no blue overalls, no emblem.
    constexpr Palette kPlain = {{{0xF4, 0xEA, 0xDE, 0xFF}, {0x90, 0xA2, 0xB2, 0xFF}, {0x5E, 0x56, 0x50, 0xFF}, {0x6C, 0x7A, 0x88, 0xFF},
                                 {0x2A, 0x2A, 0x2E, 0xFF}}};

    // A part placed in the bind pose (model units, Mario faces +z, his left is
    // +x; from the bind joint positions and his mesh bounds), on one joint.
    struct Blob {
        const char* joint;
        f32 c[3];
        f32 r[3];
        Slot slot;
        bool dome;  // the upper half only (the hat)
    };

    // A capsule between two points, each on its own joint. A null point means
    // the joint's own origin (limbs: their bind position differs per model).
    struct Limb {
        const char* jointA;
        const f32* a;
        f32 ra;
        const char* jointB;
        const f32* b;
        f32 rb;
        Slot slot;
    };

    constexpr f32 kSpine1Top[3] = {0.0f, 63.0f, -4.0f};
    constexpr f32 kChest[3] = {0.0f, 80.0f, -4.0f};
    constexpr f32 kNeckBase[3] = {0.0f, 84.0f, -4.0f};
    constexpr f32 kNeckTop[3] = {0.0f, 96.0f, -2.0f};

    // Hat: a dome with a flat round brim (a bucket hat), centred kHatCentre.
    constexpr f32 kHatCentre[3] = {0.0f, 113.0f, 4.0f};
    constexpr Blob kHat[] = {
        {"Head", {0.0f, 113.0f, 4.0f}, {22.5f, 20.0f, 21.5f}, Hat, true},
        {"Head", {0.0f, 113.0f, 4.0f}, {29.0f, 2.2f, 28.0f}, Hat, false},
    };

    constexpr Blob kBlobs[] = {
        {"Head", {0.0f, 108.0f, 4.0f}, {21.0f, 21.0f, 20.0f}, Skin, false},
        {"Hip", {0.0f, 56.0f, -3.0f}, {20.0f, 13.0f, 17.0f}, Suit, false},
        {"FootL", {11.5f, 8.0f, 4.0f}, {10.0f, 8.0f, 16.0f}, Shoe, false},
        {"FootR", {-11.5f, 8.0f, 4.0f}, {10.0f, 8.0f, 16.0f}, Shoe, false},
    };

    constexpr Limb kLimbs[] = {
        {"Spine1", kSpine1Top, 18.5f, "Spine2", kChest, 15.5f, Suit},
        {"Spine2", kNeckBase, 9.0f, "Head", kNeckTop, 8.0f, Skin},
        {"ArmL1", nullptr, 6.5f, "ArmL2", nullptr, 5.5f, Suit},
        {"ArmL2", nullptr, 5.5f, "HandL", nullptr, 5.0f, Suit},
        {"ArmR1", nullptr, 6.5f, "ArmR2", nullptr, 5.5f, Suit},
        {"ArmR2", nullptr, 5.5f, "HandR", nullptr, 5.0f, Suit},
        {"LegL1", nullptr, 8.5f, "LegL2", nullptr, 7.0f, Suit},
        {"LegL2", nullptr, 7.0f, "FootL", nullptr, 6.5f, Suit},
        {"LegR1", nullptr, 8.5f, "LegR2", nullptr, 7.0f, Suit},
        {"LegR2", nullptr, 7.0f, "FootR", nullptr, 6.5f, Suit},
    };

    // Shoulders (a sphere at ArmX1) and mittens (past HandX, along the forearm).
    constexpr f32 kShoulderRadius = 7.5f;
    constexpr f32 kMittenRadius = 8.5f;
    constexpr f32 kMittenReach = 7.5f;

    // Boo: a plain ghost on the Boo part's joints (TeresaMario.bdl's bind pose).
    constexpr f32 kBooHatCentre[3] = {0.0f, 143.0f, -6.0f};
    constexpr f32 kBooHatScale = 1.5f;
    constexpr f32 kBooTailBase[3] = {0.0f, 80.0f, -50.0f};
    constexpr Blob kBooBlobs[] = {
        {"SklRoot", {0.0f, 88.0f, 0.0f}, {66.0f, 64.0f, 70.0f}, Skin, false},
        {"SklRoot", {18.0f, 104.0f, 62.0f}, {6.0f, 10.0f, 4.0f}, Dark, false},
        {"SklRoot", {-18.0f, 104.0f, 62.0f}, {6.0f, 10.0f, 4.0f}, Dark, false},
    };
    constexpr Limb kBooLimbs[] = {
        {"ArmL1", nullptr, 13.0f, "ArmL2", nullptr, 11.0f, Skin},
        {"ArmR1", nullptr, 13.0f, "ArmR2", nullptr, 11.0f, Skin},
        {"SklRoot", kBooTailBase, 22.0f, "Tail1", nullptr, 5.0f, Skin},
    };
    constexpr const char* kBooMaterials[] = {"Cap_v", "Eye_v", "body_m_v"};

    // The joints of one model this frame: anmMtx * inverse(bind world), which
    // takes a bind-pose point to where the animation has it.
    struct Pose {
        J3DModel* model = nullptr;
        Mtx skin[kMaxJoints];
        int count = 0;

        void bindWalk(J3DJoint* pJoint, const Mtx parent, Mtx* pBind) {
            for (; pJoint != nullptr; pJoint = pJoint->getYounger()) {
                const J3DTransformInfo& info = pJoint->getTransformInfo();
                Mtx local;
                J3DGetTranslateRotateMtx(info, local);
                for (int row = 0; row < 3; ++row) {
                    local[row][0] *= info.mScale.x;
                    local[row][1] *= info.mScale.y;
                    local[row][2] *= info.mScale.z;
                }
                const u16 no = pJoint->getJntNo();
                if (no < count) {
                    PSMTXConcat(parent, local, pBind[no]);
                    bindWalk(pJoint->getChild(), pBind[no], pBind);
                }
            }
        }

        bool set(J3DModel* pModel) {
            model = pModel;
            J3DModelData* data = pModel != nullptr ? pModel->mModelData : nullptr;
            if (data == nullptr || data->mJointTree.mJointNum > kMaxJoints || data->mJointTree.mRootNode == nullptr) {
                return false;
            }
            count = data->mJointTree.mJointNum;
            Mtx bind[kMaxJoints];
            for (int i = 0; i < count; ++i) {
                PSMTXIdentity(bind[i]);
            }
            Mtx identity;
            PSMTXIdentity(identity);
            bindWalk(data->mJointTree.mRootNode, identity, bind);
            for (int i = 0; i < count; ++i) {
                Mtx inverse;
                if (!PSMTXInverse(bind[i], inverse)) {
                    PSMTXIdentity(inverse);
                }
                PSMTXConcat(pModel->mMtxBuffer->mpAnmMtx[i], inverse, skin[i]);
            }
            return true;
        }

        int index(const char* pName) const {
            const s32 i = model->mModelData->mJointTree.mJointName->getIndex(pName);
            return i >= 0 && i < count ? i : -1;
        }

        // The joint's origin, now.
        TVec3f origin(int joint) const {
            const MtxPtr m = model->mMtxBuffer->mpAnmMtx[joint];
            return TVec3f(m[0][3], m[1][3], m[2][3]);
        }

        // The model's scale at a joint (Luigi, squash and stretch).
        f32 scale(int joint) const {
            const MtxPtr m = const_cast< MtxPtr >(skin[joint]);
            const f32 x = std::sqrt(m[0][0] * m[0][0] + m[1][0] * m[1][0] + m[2][0] * m[2][0]);
            const f32 y = std::sqrt(m[0][1] * m[0][1] + m[1][1] * m[1][1] + m[2][1] * m[2][1]);
            const f32 z = std::sqrt(m[0][2] * m[0][2] + m[1][2] * m[1][2] + m[2][2] * m[2][2]);
            return (x + y + z) / 3.0f;
        }
    };

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

    // The unit sphere's point at ring i (0 = top), segment j.
    TVec3f spherePoint(int i, int j) {
        const f32 lat = 3.14159265f * (0.5f - static_cast< f32 >(i) / kRings);
        const f32 lon = 6.2831853f * static_cast< f32 >(j) / kSegs;
        return TVec3f(std::cos(lat) * std::sin(lon), std::sin(lat), std::cos(lat) * std::cos(lon));
    }

    void sendVertex(const TVec3f& rPos, const TVec3f& rNormal) {
        GXPosition3f32(rPos.x, rPos.y, rPos.z);
        GXNormal3f32(rNormal.x, rNormal.y, rNormal.z);
    }

    // An ellipsoid centre c, radii r in m's space (a joint's skin matrix, or
    // identity for world space). dome: the upper half only.
    void drawEllipsoid(const Mtx m, const TVec3f& c, const TVec3f& r, bool dome) {
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

    // A capsule in world space: a sphere at each end and the side between.
    void drawCapsule(const TVec3f& a, f32 ra, const TVec3f& b, f32 rb) {
        Mtx identity;
        PSMTXIdentity(identity);
        drawEllipsoid(identity, a, TVec3f(ra, ra, ra), false);
        drawEllipsoid(identity, b, TVec3f(rb, rb, rb), false);
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
            const f32 t0 = 6.2831853f * static_cast< f32 >(j) / kSegs;
            const f32 t1 = 6.2831853f * static_cast< f32 >(j + 1) / kSegs;
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

    // The GX state for the figure: positions and normals in world space (the
    // camera view as the position and normal matrix), one colour channel.
    // Lit by the player lights.
    void beginFigure() {
        TDDraw::cameraInit3D();
        TDDraw::setup(0, 0, 0);
        MR::loadLightPlayer();  // LIGHT0/1 and the ambient colour (after setup, which clears it)
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

    void endFigure() {
        MR::reinitGX();
        MR::loadViewMtx();
        MR::loadProjectionMtx();
    }

    struct Painter {
        const Palette* palette;

        void use(Slot slot) const {
            GXSetChanMatColor(GX_COLOR0A0, palette->c[slot]);
        }
    };

    void drawBlob(const Pose& rPose, const Painter& rPaint, const Blob& rBlob, f32 scale = 1.0f, const f32* pAround = nullptr) {
        const int joint = rPose.index(rBlob.joint);
        if (joint < 0) {
            return;
        }
        rPaint.use(rBlob.slot);
        // scale: about pAround (the Boo's bigger hat).
        TVec3f c(rBlob.c[0], rBlob.c[1], rBlob.c[2]);
        if (pAround != nullptr) {
            const TVec3f around(pAround[0], pAround[1], pAround[2]);
            c = around + (c - TVec3f(kHatCentre[0], kHatCentre[1], kHatCentre[2])) * scale;
        }
        drawEllipsoid(rPose.skin[joint], c, TVec3f(rBlob.r[0] * scale, rBlob.r[1] * scale, rBlob.r[2] * scale), rBlob.dome);
    }

    bool limbPoint(const Pose& rPose, const char* pJoint, const f32* pPoint, TVec3f* pOut, f32* pScale) {
        const int joint = rPose.index(pJoint);
        if (joint < 0) {
            return false;
        }
        *pOut = pPoint != nullptr ? apply(rPose.skin[joint], TVec3f(pPoint[0], pPoint[1], pPoint[2])) : rPose.origin(joint);
        *pScale = rPose.scale(joint);
        return true;
    }

    void drawLimb(const Pose& rPose, const Painter& rPaint, const Limb& rLimb) {
        TVec3f a, b;
        f32 sa, sb;
        if (!limbPoint(rPose, rLimb.jointA, rLimb.a, &a, &sa) || !limbPoint(rPose, rLimb.jointB, rLimb.b, &b, &sb)) {
            return;
        }
        rPaint.use(rLimb.slot);
        drawCapsule(a, rLimb.ra * sa, b, rLimb.rb * sb);
    }

    void drawMittens(const Pose& rPose, const Painter& rPaint) {
        const char* const arms[2][3] = {{"ArmL1", "ArmL2", "HandL"}, {"ArmR1", "ArmR2", "HandR"}};
        Mtx identity;
        PSMTXIdentity(identity);
        for (const auto& arm : arms) {
            const int elbow = rPose.index(arm[1]);
            const int hand = rPose.index(arm[2]);
            if (elbow < 0 || hand < 0) {
                continue;
            }
            const f32 s = rPose.scale(hand);
            const TVec3f wrist(rPose.origin(hand));
            const TVec3f along(unitNormal(wrist - rPose.origin(elbow)));
            rPaint.use(Skin);
            const f32 r = kMittenRadius * s;
            drawEllipsoid(identity, wrist + along * (kMittenReach * s), TVec3f(r, r, r), false);
        }
    }

    void drawShoulders(const Pose& rPose, const Painter& rPaint) {
        Mtx identity;
        PSMTXIdentity(identity);
        for (const char* name : {"ArmL1", "ArmR1"}) {
            const int joint = rPose.index(name);
            if (joint < 0) {
                continue;
            }
            rPaint.use(Suit);
            const f32 r = kShoulderRadius * rPose.scale(joint);
            drawEllipsoid(identity, rPose.origin(joint), TVec3f(r, r, r), false);
        }
    }

    // The hat on Cappy's matrix (axes scaled, origin his centre).
    void drawThrownHat(const Painter& rPaint, const Mtx capMtx) {
        rPaint.use(Hat);
        for (const Blob& blob : kHat) {
            const TVec3f c(blob.c[0] - kHatCentre[0], blob.c[1] - kHatCentre[1], blob.c[2] - kHatCentre[2]);
            drawEllipsoid(capMtx, c, TVec3f(blob.r[0], blob.r[1], blob.r[2]), blob.dome);
        }
    }

    GXColor hue(int frame) {
        const f32 h = std::fmod(static_cast< f32 >(frame) * 8.0f, 360.0f) / 60.0f;
        const f32 x = 1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f);
        f32 r = 0, g = 0, b = 0;
        switch (static_cast< int >(h)) {
        case 0: r = 1; g = x; break;
        case 1: r = x; g = 1; break;
        case 2: g = 1; b = x; break;
        case 3: g = x; b = 1; break;
        case 4: r = x; b = 1; break;
        default: r = 1; b = x; break;
        }
        const auto channel = [](f32 v) { return static_cast< u8 >(80.0f + v * 175.0f); };
        return GXColor{channel(r), channel(g), channel(b), 0xFF};
    }

    // The player's form (MarioActor::mPlayerMode) as a tint of the plain figure.
    Palette paletteFor(const MarioActor* pActor) {
        static int sFrame = 0;
        Palette p = kPlain;
        switch (pActor->mPlayerMode) {
        case 1:  // Rainbow
            p.c[Suit] = hue(++sFrame);
            p.c[Hat] = hue(sFrame + 22);
            break;
        case 2:  // Fire
            p.c[Suit] = GXColor{0xEC, 0xE6, 0xDE, 0xFF};
            p.c[Hat] = GXColor{0xD8, 0x6A, 0x2C, 0xFF};
            break;
        case 3:  // Ice
            p.c[Skin] = GXColor{0xC8, 0xEA, 0xF6, 0xFF};
            p.c[Suit] = GXColor{0x9C, 0xD4, 0xEE, 0xFF};
            p.c[Shoe] = GXColor{0x80, 0xB8, 0xD8, 0xFF};
            p.c[Hat] = GXColor{0xA8, 0xDA, 0xF0, 0xFF};
            break;
        case 4:  // Bee
            p.c[Suit] = GXColor{0xE6, 0xBC, 0x2E, 0xFF};
            p.c[Hat] = GXColor{0x34, 0x2E, 0x28, 0xFF};
            break;
        case 5:  // Spring
            p.c[Suit] = GXColor{0xA6, 0xAE, 0xB6, 0xFF};
            p.c[Hat] = GXColor{0x8A, 0x92, 0x9A, 0xFF};
            break;
        case 7:  // Flying (Red Star)
            p.c[Suit] = GXColor{0x3C, 0x3C, 0x44, 0xFF};
            p.c[Hat] = GXColor{0xC8, 0xA2, 0x34, 0xFF};
            break;
        default:
            break;
        }
        return p;
    }

    bool sCapOutMtxValid = false;
    Mtx sCapOutMtx;

    void drawBody(const Pose& rPose, const Painter& rPaint, bool handsOnly, bool hat) {
        drawMittens(rPose, rPaint);
        if (handsOnly) {
            return;
        }
        for (const Limb& limb : kLimbs) {
            drawLimb(rPose, rPaint, limb);
        }
        drawShoulders(rPose, rPaint);
        for (const Blob& blob : kBlobs) {
            drawBlob(rPose, rPaint, blob);
        }
        if (hat) {
            for (const Blob& blob : kHat) {
                drawBlob(rPose, rPaint, blob);
            }
        }
    }

    MarioParts* sBooHiddenOn = nullptr;  // the Boo part whose materials we hid
}  // namespace

namespace GenericFigure {
    bool on() {
        return PetariNative::Mods::enabled(PetariNative::Mods::Mod::GenericCharacter);
    }

    void update(MarioActor* pActor) {
        MarioParts* boo = pActor->_9A4;
        const bool want = on() && boo != nullptr;
        if (want && sBooHiddenOn != boo) {
            for (const char* material : kBooMaterials) {
                MR::hideMaterial(boo, material);
            }
            sBooHiddenOn = boo;
        } else if (!want && sBooHiddenOn != nullptr && sBooHiddenOn == boo) {
            for (const char* material : kBooMaterials) {
                MR::showMaterial(boo, material);
            }
            sBooHiddenOn = nullptr;
        } else if (sBooHiddenOn != nullptr && sBooHiddenOn != boo) {
            sBooHiddenOn = nullptr;  // a new stage: the old part went with its heap
        }
    }

    bool drawPlayer(const MarioActor* pActor) {
        if (!on()) {
            return false;
        }
        if (pActor->mFlag.mIsHiddenModel) {
            return true;
        }
        const bool fpView = pActor->mMario->isStatusActive(MarioStatus_FpView);
        if (fpView && pActor->_1A1) {
            return true;
        }
        Pose pose;
        if (!pose.set(pActor->mModels[pActor->mCurrModel])) {
            return false;  // not a skeleton we know: Mario's own draw
        }
        sCapOutMtxValid = petariCappyCapMtx(sCapOutMtx);
        const Palette palette = paletteFor(pActor);
        const Painter paint = {&palette};
        beginFigure();
        drawBody(pose, paint, fpView, !sCapOutMtxValid);
        if (sCapOutMtxValid) {
            drawThrownHat(paint, sCapOutMtx);
        }
        endFigure();
        return true;
    }

    bool drawSilhouette(const MarioActor* pActor) {
        // The game's see-through-walls silhouette is built from Mario's own
        // models (the simple model marks destination alpha, his body's display
        // lists blend the tint), so with the figure there is none: nothing
        // Mario-shaped is drawn.
        return on();
    }

    void drawBoo(const MarioActor* pActor) {
        MarioParts* boo = pActor->_9A4;
        if (!on() || boo == nullptr || MR::isDead(boo) || MR::isHiddenModel(boo)) {
            return;
        }
        Pose pose;
        if (!pose.set(MR::getJ3DModel(boo))) {
            return;
        }
        Palette palette = kPlain;
        palette.c[Skin] = GXColor{0xF2, 0xF0, 0xEC, 0xFF};
        const Painter paint = {&palette};
        beginFigure();
        for (const Blob& blob : kBooBlobs) {
            drawBlob(pose, paint, blob);
        }
        for (const Limb& limb : kBooLimbs) {
            drawLimb(pose, paint, limb);
        }
        for (const Blob& blob : kHat) {
            Blob onBoo = blob;
            onBoo.joint = "SklRoot";
            drawBlob(pose, paint, onBoo, kBooHatScale, kBooHatCentre);
        }
        endFigure();
    }
}  // namespace GenericFigure

#endif
