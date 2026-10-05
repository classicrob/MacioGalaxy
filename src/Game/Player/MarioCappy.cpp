// Native mod: Cappy, Super Mario Odyssey's cap throw (docs/dev/CAPPY.md). Off by
// default; it needs the OdysseyMovement mod (the bounce loop is built on its
// dive and air model). With the mod off at stage load nothing here creates or
// changes anything, and every hook returns at once.
#ifdef PETARI_NATIVE
#include "Game/Player/MarioCappy.hpp"
#include "Game/LiveActor/AllLiveActorGroup.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/HitSensorInfo.hpp"
#include "Game/LiveActor/HitSensorKeeper.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioParts.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <petari/cappy.hpp>
#include <petari/odyssey_move.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" bool petari_mod_enabled(int mod);
extern "C" bool petari_mod_take_press(int mod);
extern "C" bool petari_mod_held(int mod);

namespace {
    namespace Cappy = PetariNative::Cappy;
    namespace Odyssey = PetariNative::Odyssey;
    constexpr int kModOdysseyMovement = 2;  // PetariNative::Mods::Mod
    constexpr int kModCappy = 3;
    constexpr int kModGenericCharacter = 7;  // its figure draws a plain hat for Cappy (GenericFigure.cpp)
    constexpr f32 kSpinDegrees = 40.0f;  // Cappy's spin while out, per frame (I)

    void capSensor(HitSensor* pOwn, HitSensor* pOther);
    void applyGenericLook();

    // The flying cap: a MarioParts with an eye sensor (checked against enemies,
    // map objects, coins and Star Bits, like Mario's own "eye").
    class CappyParts : public MarioParts {
    public:
        CappyParts(LiveActor* pHost, MtxPtr pMtx) : MarioParts(pHost, "キャッピー", "MarioHair", false, nullptr, pMtx) {}
        virtual void attackSensor(HitSensor* pOwn, HitSensor* pOther) override { capSensor(pOwn, pOther); }
    };

    constexpr int kMaxHits = 8;  // enemies hit by one throw (each once), as Mario's spin

    struct State {
        MarioActor* actor = nullptr;
        MarioParts* cap = nullptr;   // the flying cap (MarioHair.arc, hair hidden)
        MarioParts* hair = nullptr;  // Mario's hair while the cap is out (MarioHair.arc, cap hidden)
        Cappy::Cap cappy;
        Cappy::BounceLimit bounce;
        Cappy::HopLimit hop;
        int buffer = 0;       // frames a throw press waits for Mario (PreInputFrameCapThrow)
        bool held = false;    // the throw button, this frame
        TVec3f origin;        // the throw line: release point and direction
        TVec3f dir;
        TVec3f pressFront;    // Mario's facing when the throw was pressed (the line's direction)
        f32 scale = 1.0f;     // drawn size: grows once fully thrown, shrinks on the way back
        TVec3f pos;           // Cappy's centre
        f32 blockedAt = -1.0f;
        f32 spin = 0.0f;
        bool marioCapHidden = false;
        int frame = 0;
        f32 capOffsetUp = 30.0f;  // the cap's centre in MarioHair's model space (PETARI_CAPPY_CAP_OFFSET="up,front")
        f32 capOffsetFront = 5.0f;
        HitSensor* hits[kMaxHits] = {};
        int hitCount = 0;
    };
    State s;
    TPos3f sCapMtx;
    int sBounces = 0;  // for the movement harness (petari_cappy_bounces, _hits, _collected)
    int sHits = 0;
    int sCollected = 0;

    bool trace() {
        static const bool on = std::getenv("PETARI_CAPPY_TRACE") != nullptr;
        return on;
    }

    bool active() { return s.cap != nullptr && petari_mod_enabled(kModCappy) && petari_mod_enabled(kModOdysseyMovement); }

    TVec3f upOf(const MarioActor* pActor) {
        TVec3f up(-pActor->_240);
        MR::normalizeOrZero(&up);
        if (MR::isNearZero(up)) {
            up.set(0.0f, 1.0f, 0.0f);
        }
        return up;
    }

    TVec3f across(const TVec3f& v, const TVec3f& up) {
        TVec3f out(v - up * up.dot(v));
        MR::normalizeOrZero(&out);
        return out;
    }

    // Normal Mario, on foot or in the air (not swimming, not a power-up suit).
    bool normalMario(const MarioActor* pActor) { return pActor->mPlayerMode == 0 && !pActor->mMario->isSwimming(); }

    // Mario's own cap on his head (Cappy home), or his hair (Cappy out). Luigi has
    // no hair model: he keeps his cap (docs/dev/CAPPY.md).
    void showMarioCap(bool show) {
        if (show != s.marioCapHidden) {
            return;
        }
        s.marioCapHidden = !show;
        if (gIsLuigi) {
            return;
        }
        if (show) {
            MR::showJoint(s.actor, "Cap0");
            s.hair->kill();
        } else {
            MR::hideJoint(s.actor, "Cap0");
            s.hair->appear();
            applyGenericLook();
        }
    }

    // The Generic character mod: Cappy and the hair under him are MarioHair's
    // models; with that mod on their materials are hidden (the figure draws a
    // plain hat at sCapMtx instead).
    void applyGenericLook() {
        static bool sHidden = false;
        static const MarioParts* sCap = nullptr;
        const bool generic = petari_mod_enabled(kModGenericCharacter);
        if (sCap != s.cap) {
            sCap = s.cap;
            sHidden = false;  // new models (a new stage): everything shown
        }
        if (generic == sHidden || s.cap == nullptr) {
            return;
        }
        sHidden = generic;
        const char* const cap[] = {"Cap_v"};
        const char* const hair[] = {"Hair_v", "Hair2_v"};
        for (const char* material : cap) {
            generic ? MR::hideMaterial(s.cap, material) : MR::showMaterial(s.cap, material);
        }
        if (s.hair != nullptr) {
            for (const char* material : hair) {
                generic ? MR::hideMaterial(s.hair, material) : MR::showMaterial(s.hair, material);
            }
        }
    }

    // Straight home, at once (the mod was turned off, Mario became something else).
    void reset() {
        if (s.cappy.state != Cappy::State::Idle && trace()) {
            std::fprintf(stderr, "[cappy] reset from %s\n", Cappy::stateName(s.cappy.state));
        }
        s.cappy = Cappy::Cap{};
        s.buffer = 0;
        if (s.cap != nullptr && !MR::isDead(s.cap)) {
            s.cap->kill();
        }
        if (s.hair != nullptr) {
            showMarioCap(true);
        }
    }

    void startThrow(MarioActor* pActor, bool inAir) {
        Mario* mario = pActor->mMario;
        Cappy::startThrow(s.cappy, inAir);
        s.hitCount = 0;
        // The line follows Mario's facing at the press, as the dive does: Galaxy's
        // throw animations (its spin) must not turn it before Cappy leaves.
        s.pressFront = across(mario->mFrontVec, upOf(pActor));
        if (MR::isNearZero(s.pressFront)) {
            s.pressFront = across(mario->getAirFrontVec(), upOf(pActor));
        }
        if (inAir) {
            const f32 height = upOf(pActor).dot(pActor->mPosition);
            const bool hop = s.hop.allows(height);
            petariOdysseyAirThrow(mario, hop);
            s.hop.record(height);
        } else {
            // SMO's ground throw: speed x0.95 a frame for 15 frames (MarioWalk.cpp),
            // with Galaxy's ground spin animation.
            mario->stopAnimationUpper(nullptr);
            mario->changeAnimation("地上ひねり");
            Odyssey::Live::capThrowBrake = Cappy::Const::GroundBrakeFrames;
        }
        mario->playSound("パンチ風切り");
        if (trace()) {
            std::fprintf(stderr, "[cappy] throw (%s) at (%.1f, %.1f, %.1f) front (%.2f, %.2f, %.2f)\n", inAir ? "air" : "ground",
                         pActor->mPosition.x, pActor->mPosition.y, pActor->mPosition.z, mario->mFrontVec.x, mario->mFrontVec.y,
                         mario->mFrontVec.z);
        }
    }

    // Test only (PETARI_CAPPY_TEST_AIM=enemy|item, for the harness): the nearest
    // valid enemy (or coin / Star Bit) sensor, logged, so a throw can be aimed
    // at it. Never set in play.
    HitSensor* testAimTarget(const TVec3f& rFrom) {
        static const char* aim = std::getenv("PETARI_CAPPY_TEST_AIM");
        AllLiveActorGroup* group = aim != nullptr ? MR::getAllLiveActorGroup() : nullptr;
        if (group == nullptr) {
            return nullptr;
        }
        const bool items = std::strcmp(aim, "item") == 0;
        HitSensor* best = nullptr;
        f32 bestDist = 0.0f;
        for (s32 i = 0; i < group->getObjNum(); i++) {
            LiveActor* actor = group->getActor(i);
            if (actor == nullptr || MR::isDead(actor) || MR::isClipped(actor) || MR::isHiddenModel(actor) || actor->getSensorKeeper() == nullptr) {
                continue;
            }
            HitSensorKeeper* keeper = actor->getSensorKeeper();
            for (s32 j = 0; j < keeper->mSensorCount; j++) {
                HitSensorInfo* info = keeper->getNthSensorInfo(j);
                HitSensor* sensor = info != nullptr ? info->mSensor : nullptr;
                if (sensor == nullptr || !sensor->isValid()) {
                    continue;
                }
                const bool wanted = items ? (sensor->isType(ATYPE_COIN) || sensor->isType(ATYPE_STAR_PIECE)) : MR::isSensorEnemy(sensor);
                const f32 dist = (sensor->mPosition - rFrom).length();
                if (wanted && (best == nullptr || dist < bestDist)) {
                    best = sensor;
                    bestDist = dist;
                }
            }
        }
        if (best != nullptr) {
            std::fprintf(stderr, "[cappy] test aim: %s at (%.1f, %.1f, %.1f), %.1f away\n", best->mHost->mName, best->mPosition.x,
                         best->mPosition.y, best->mPosition.z, bestDist);
        }
        return best;
    }

    // Cappy leaves Mario: the line runs along his facing, across gravity, from 70
    // above his feet; a map polygon on it stops the flight (one ray, at the release).
    void release(MarioActor* pActor) {
        const TVec3f up = upOf(pActor);
        s.dir = across(s.pressFront, up);  // re-flattened: gravity may have turned since the press
        if (MR::isNearZero(s.dir)) {
            s.dir = across(pActor->mMario->getAirFrontVec(), up);
        }
        s.origin = pActor->mPosition + up * Cappy::Const::StartHeight;
        if (const HitSensor* target = testAimTarget(s.origin); target != nullptr) {
            const TVec3f aim = across(target->mPosition - s.origin, up);
            if (!MR::isNearZero(aim)) {
                s.dir = aim;
            }
        }
        s.blockedAt = -1.0f;
        const f32 reach = 700.0f + Cappy::Const::Radius;
        TVec3f hit;
        Triangle triangle;
        if (MR::getFirstPolyOnLineToMap(&hit, &triangle, s.origin, s.dir * reach)) {
            s.blockedAt = std::max((hit - s.origin).length() - Cappy::Const::Radius, 0.0f);
        }
        s.spin = 0.0f;
        if (trace()) {
            std::fprintf(stderr, "[cappy] release at (%.1f, %.1f, %.1f) dir (%.2f, %.2f, %.2f) blocked at %.1f\n", s.origin.x, s.origin.y,
                         s.origin.z, s.dir.x, s.dir.y, s.dir.z, s.blockedAt);
        }
    }

    void placeModel(const TVec3f& up) {
        s.spin = std::fmod(s.spin + kSpinDegrees, 360.0f);
        const f32 angle = s.spin * 0.017453292f;
        TVec3f side;
        side.cross(up, s.dir);
        TVec3f front(s.dir * std::cos(angle) + side * std::sin(angle));
        // Fully thrown, Cappy grows (as SMO's does) over GrowFrames; returning he
        // shrinks back to a cap on Mario's head.
        const f32 target = s.cappy.state == Cappy::State::Hover ? Cappy::Const::HoverScale : 1.0f;
        // (Back twice as fast: after a bounce he is on Mario's head in a few frames.)
        const f32 step = (Cappy::Const::HoverScale - 1.0f) / Cappy::Const::GrowFrames *
                         (s.cappy.state == Cappy::State::Return ? 2.0f : 1.0f);
        s.scale = s.scale < target ? std::min(s.scale + step, target) : std::max(s.scale - step, target);
        const TVec3f base(s.pos - (up * s.capOffsetUp + front * s.capOffsetFront) * s.scale);
        MR::makeMtxUpFrontPos(&sCapMtx, up, front, base);
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                sCapMtx.mMtx[row][col] *= s.scale;
            }
        }
        if (MR::isDead(s.cap)) {
            s.cap->appear();
        }
        applyGenericLook();
    }

    void doTrace(const MarioActor* pActor, const TVec3f& up, bool contactTest, bool contact) {
        if (!trace()) {
            return;
        }
        const Mario* mario = pActor->mMario;
        const Odyssey::Air air = Odyssey::Live::airKind;
        if (s.cap != nullptr && !MR::isDead(s.cap)) {
            // Where the cap is drawn (its joints, last calcAnim) against its logical centre.
            MtxPtr capF = MR::getJointMtx(s.cap, "CapF");
            MtxPtr cap2 = MR::getJointMtx(s.cap, "Cap2");
            std::fprintf(stderr, "[cappy] drawn CapF (%.1f, %.1f, %.1f) Cap2 (%.1f, %.1f, %.1f) logical (%.1f, %.1f, %.1f) mario front (%.2f, %.2f, %.2f)\n",
                         capF[0][3], capF[1][3], capF[2][3], cap2[0][3], cap2[1][3], cap2[2][3], s.pos.x, s.pos.y, s.pos.z,
                         mario->mFrontVec.x, mario->mFrontVec.y, mario->mFrontVec.z);
        }
        std::fprintf(stderr,
                     "[cappy] f%d %s t%d dist %.1f pos (%.1f, %.1f, %.1f) held %d latch %d | mario (%.1f, %.1f, %.1f) h %.1f air %d "
                     "jumping %d ground %d gp %d | bounce avail %d test %d contact %d\n",
                     s.frame, Cappy::stateName(s.cappy.state), s.cappy.frame, s.cappy.distance, s.pos.x, s.pos.y, s.pos.z,
                     static_cast< int >(s.held), s.cappy.latch, pActor->mPosition.x, pActor->mPosition.y, pActor->mPosition.z,
                     up.dot(pActor->mPosition), static_cast< int >(air), static_cast< int >(pActor->isJumping()),
                     static_cast< int >(mario->mMovementStates._1), static_cast< int >(mario->mMovementStates._B),
                     static_cast< int >(s.bounce.available), static_cast< int >(contactTest), static_cast< int >(contact));
    }
}  // namespace

void petariCappyInit(MarioActor* pActor) {
    // A new stage: the old models went with the old scene's heap.
    s = State{};
    s.actor = pActor;
    if (!petari_mod_enabled(kModCappy)) {
        return;
    }
    if (const char* offset = std::getenv("PETARI_CAPPY_CAP_OFFSET"); offset != nullptr) {
        std::sscanf(offset, "%f,%f", &s.capOffsetUp, &s.capOffsetFront);
    }
    sCapMtx.identity();
    s.cap = new CappyParts(pActor, sCapMtx.toMtxPtr());
    s.cap->initWithoutIter();
    s.cap->initHitSensor(1);
    MR::addHitSensorPosEye(s.cap, "cappy", 16, Cappy::Const::Radius + 10.0f, &s.pos, TVec3f(0.0f, 0.0f, 0.0f));
    MR::hideMaterial(s.cap, "Hair_v");
    MR::hideMaterial(s.cap, "Hair2_v");
    s.cap->kill();
    s.hair = new MarioParts(pActor, "キャッピー髪", "MarioHair", false, nullptr, nullptr);
    s.hair->initWithoutIter();
    s.hair->initFixedPosition("Cap0");
    MR::hideMaterial(s.hair, "Cap_v");
    s.hair->kill();
    std::fprintf(stderr, "[cappy] cap models created\n");
}

bool petariCappyCapMtx(float (*pOut)[4]) {
    if (s.cap == nullptr || MR::isDead(s.cap) || !active()) {
        return false;
    }
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            pOut[row][col] = sCapMtx.mMtx[row][col];
        }
    }
    pOut[0][3] = s.pos.x;
    pOut[1][3] = s.pos.y;
    pOut[2][3] = s.pos.z;
    return true;
}

void petariCappyInput(MarioActor* pActor) {
    if (s.cap == nullptr) {
        return;
    }
    const bool press = petari_mod_take_press(kModCappy);
    if (!active()) {
        reset();
        return;
    }
    s.held = petari_mod_held(kModCappy);
    if (press) {
        s.buffer = Cappy::Const::PreInputFrame;
        Cappy::notePress(s.cappy);
        if (trace()) {
            std::fprintf(stderr, "[cappy] press (state %s)\n", Cappy::stateName(s.cappy.state));
        }
    } else if (s.buffer > 0) {
        --s.buffer;
    }
}

void petariCappySwingAction(MarioActor* pActor) {
    if (!active() || s.buffer == 0) {
        return;
    }
    Mario* mario = pActor->mMario;
    if (!normalMario(pActor)) {
        s.buffer = 0;
        return;
    }
    namespace Live = Odyssey::Live;
    const bool grounded = !pActor->isJumping() && mario->mMovementStates._1;
    // SMO's Y does more than throw: a dive in a ground pound, a roll from a
    // crouch, a roll boost. The movement mod reads those from its Spin buffer.
    if ((pActor->isJumping() && mario->mMovementStates._B) ||
        (grounded && (Live::roll.rolling || mario->mMovementStates._A || mario->checkSquat(false)))) {
        Live::spinBuffer = Live::SpinBufferFrames;
        s.buffer = 0;
        return;
    }
    if (s.cappy.state != Cappy::State::Idle) {
        return;  // out already: the press only keeps him there (notePress)
    }
    if (Live::noSpinAir && Live::airKind == Odyssey::Air::Dive) {
        return;  // no throw out of a dive (SMO); the press waits for the bounce or landing
    }
    s.buffer = 0;
    startThrow(pActor, !grounded);
}

void petariCappyUpdate(MarioActor* pActor) {
    if (s.cap == nullptr) {
        return;
    }
    ++s.frame;
    if (!active() || !normalMario(pActor)) {
        reset();
        return;
    }
    Mario* mario = pActor->mMario;
    const TVec3f up = upOf(pActor);
    const bool grounded = !pActor->isJumping() && mario->mMovementStates._1;
    if (grounded) {
        s.bounce.land();
        s.hop.land();
    } else {
        Odyssey::Live::capThrowBrake = 0;  // a ground throw's brake ends with the ground
    }

    bool tested = false, contact = false;
    if (s.cappy.state == Cappy::State::Windup || (s.cappy.state == Cappy::State::Fly && s.cappy.frame <= 4)) {
        // Galaxy's spin animations start the spin's light trail and sparkles
        // (MarioAnimator::spinEntry): not a cap throw's.
        mario->stopEffect("スピンライト");
    }
    if (Cappy::step(s.cappy, s.held, s.blockedAt)) {
        release(pActor);
        if (s.blockedAt >= 0.0f && s.blockedAt < Cappy::Const::StartDistance) {
            s.cappy.distance = s.blockedAt;
            s.cappy.state = Cappy::State::Hover;
        }
    }
    switch (s.cappy.state) {
    case Cappy::State::Idle:
    case Cappy::State::Windup:
        s.scale = 1.0f;
        showMarioCap(true);
        if (!MR::isDead(s.cap)) {
            s.cap->kill();
        }
        doTrace(pActor, up, false, false);
        return;
    case Cappy::State::Fly:
    case Cappy::State::Hover:
        s.pos = s.origin + s.dir * Cappy::shownDistance(s.cappy);
        break;
    case Cappy::State::Return: {
        const TVec3f target(pActor->mPosition + up * Cappy::Const::ReturnHeight);
        TVec3f toMario(target - s.pos);
        const f32 distance = toMario.length();
        if (Cappy::stepReturn(s.cappy, distance)) {
            if (trace()) {
                std::fprintf(stderr, "[cappy] caught\n");
            }
            showMarioCap(true);
            s.cap->kill();
            doTrace(pActor, up, false, false);
            return;
        }
        s.pos += toMario * (std::min(s.cappy.speed, distance) / distance);
        break;
    }
    }
    showMarioCap(false);

    // The cap bounce: Cappy hovering, one per airtime, Mario in the air touching him.
    if (Cappy::bounceable(s.cappy) && s.bounce.available && pActor->isJumping() && !mario->mMovementStates._1) {
        tested = true;
        const TVec3f rel(s.pos - pActor->mPosition);
        const f32 upPart = rel.dot(up);
        const f32 acrossPart = (rel - up * upPart).length();
        contact = Cappy::bounceContact(upPart, acrossPart, mario->mMovementStates._B);
        if (contact) {
            if (trace()) {
                std::fprintf(stderr, "[cappy] bounce (air %d gp %d) at (%.1f, %.1f, %.1f)\n", static_cast< int >(Odyssey::Live::airKind),
                             static_cast< int >(mario->mMovementStates._B), pActor->mPosition.x, pActor->mPosition.y, pActor->mPosition.z);
            }
            // The contact frame: Cappy to Mario's touching sphere (head, body or
            // feet, the nearest), so the bounce shows them touching even at the
            // edge of the contact sphere.
            const f32 heights[3] = {Cappy::Const::HeadHeight, Cappy::Const::BodyHeight, Cappy::Const::FootHeight};
            const f32 radii[3] = {Cappy::Const::HeadRadius, Cappy::Const::BodyRadius, Cappy::Const::FootRadius};
            int nearest = 0;
            f32 nearestDist = 0.0f;
            for (int i = 0; i < 3; ++i) {
                const f32 d = (s.pos - (pActor->mPosition + up * heights[i])).length();
                if (i == 0 || d < nearestDist) {
                    nearest = i;
                    nearestDist = d;
                }
            }
            const TVec3f centre(pActor->mPosition + up * heights[nearest]);
            TVec3f away(s.pos - centre);
            if (MR::normalizeOrZero(&away)) {
                away = up * -1.0f;
            }
            const TVec3f snapped(centre + away * (radii[nearest] + Cappy::Const::VisualRadius * 0.8f));
            if (trace()) {
                std::fprintf(stderr, "[cappy] contact: %s sphere, %.1f from Cappy; Cappy moved %.1f to touch\n",
                             nearest == 0 ? "head" : nearest == 1 ? "body" : "foot", nearestDist, (snapped - s.pos).length());
            }
            s.pos = snapped;
            petariOdysseyCapBounce(mario);
            ++sBounces;
            s.bounce.bounce();
            s.hop.land();  // a new air action: the next throw hops again
            Cappy::recall(s.cappy);
        }
    }
    placeModel(up);
    doTrace(pActor, up, tested, contact);
}

namespace {
    // Cappy's touch (nice to have, docs/dev/CAPPY.md): an enemy gets Mario's
    // spin punch (Galaxy's own reaction: stun, knock-back or defeat), each
    // enemy once a throw, and Cappy comes back; coins and Star Bits are
    // collected as if Mario touched them.
    void capSensor(HitSensor* pOwn, HitSensor* pOther) {
        if (!active() || s.actor == nullptr || s.cappy.state == Cappy::State::Idle || s.cappy.state == Cappy::State::Windup) {
            return;
        }
        if (pOther->isType(ATYPE_COIN) || pOther->isType(ATYPE_STAR_PIECE)) {
            if (pOther->receiveMessage(ACTMES_ITEM_GET, s.actor->getSensor("dummy"))) {
                ++sCollected;
                if (trace()) {
                    std::fprintf(stderr, "[cappy] collected %s\n", pOther->mHost->mName);
                }
            }
            return;
        }
        if (!MR::isSensorEnemy(pOther) || MR::isDead(pOther->mHost)) {
            return;
        }
        for (int i = 0; i < s.hitCount; ++i) {
            if (s.hits[i] == pOther) {
                return;
            }
        }
        if (s.hitCount >= kMaxHits) {
            return;
        }
        // From Mario's body, as his spin punch (MarioActor::sendPunch), without
        // its _FCC: an enemy hitting back must still be in reach of Mario.
        if (MR::sendMsgPlayerPunch(pOther, s.actor->getSensor("body"))) {
            s.hits[s.hitCount++] = pOther;
            ++sHits;
            if (!s.actor->selectCustomEffectSpinHitSound(pOther->mHost->mName)) {
                MR::startSpinHitSound(s.actor);
            }
            if (trace()) {
                std::fprintf(stderr, "[cappy] hit %s\n", pOther->mHost->mName);
            }
            Cappy::recall(s.cappy);
        }
    }
}  // namespace

void petariCappyWallJump() {
    s.bounce.wallJump();
}

void petariCappyDiveStarted() {
    // A dive while Cappy is out (ground pound + Jump or Spin) keeps
    // him there like the throw key's press does (SMO dives with Y).
    if (s.cap != nullptr && active()) {
        Cappy::notePress(s.cappy);
    }
}

// The movement harness (native/app/smoke_movement.cpp): cap bounces so far, and
// Cappy's state (petari/cappy.hpp State).
extern "C" int petari_cappy_bounces() {
    return sBounces;
}

extern "C" int petari_cappy_state() {
    return static_cast< int >(s.cappy.state);
}

extern "C" int petari_cappy_collected() {
    return sCollected;
}

extern "C" int petari_cappy_hits() {
    return sHits;
}
#endif
