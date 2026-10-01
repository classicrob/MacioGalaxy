// Native mod: Cappy, Super Mario Odyssey's cap throw (docs/dev/CAPPY.md). Off by
// default; it needs the OdysseyMovement mod (the bounce loop is built on its
// dive and air model). With the mod off at stage load nothing here creates or
// changes anything, and every hook returns at once.
#ifdef PETARI_NATIVE
#include "Game/Player/MarioCappy.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioParts.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include <petari/cappy.hpp>
#include <petari/odyssey_move.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>

extern "C" bool petari_mod_enabled(int mod);
extern "C" bool petari_mod_take_press(int mod);
extern "C" bool petari_mod_held(int mod);

namespace {
    namespace Cappy = PetariNative::Cappy;
    namespace Odyssey = PetariNative::Odyssey;
    constexpr int kModOdysseyMovement = 2;  // PetariNative::Mods::Mod
    constexpr int kModCappy = 3;
    constexpr f32 kSpinDegrees = 40.0f;  // Cappy's spin while out, per frame (I)

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
        TVec3f pos;           // Cappy's centre
        f32 blockedAt = -1.0f;
        f32 spin = 0.0f;
        bool marioCapHidden = false;
        int frame = 0;
        f32 capOffsetUp = 30.0f;  // the cap's centre in MarioHair's model space (PETARI_CAPPY_CAP_OFFSET="up,front")
        f32 capOffsetFront = 5.0f;
    };
    State s;
    TPos3f sCapMtx;
    int sBounces = 0;  // for the movement harness (petari_cappy_bounces)

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
            std::fprintf(stderr, "[cappy] throw (%s) at (%.1f, %.1f, %.1f)\n", inAir ? "air" : "ground", pActor->mPosition.x,
                         pActor->mPosition.y, pActor->mPosition.z);
        }
    }

    // Cappy leaves Mario: the line runs along his facing, across gravity, from 70
    // above his feet; a map polygon on it stops the flight (one ray, at the release).
    void release(MarioActor* pActor) {
        const TVec3f up = upOf(pActor);
        s.dir = across(pActor->mMario->mFrontVec, up);
        if (MR::isNearZero(s.dir)) {
            s.dir = across(pActor->mMario->getAirFrontVec(), up);
        }
        s.origin = pActor->mPosition + up * Cappy::Const::StartHeight;
        s.blockedAt = -1.0f;
        const f32 reach = 700.0f + Cappy::Const::Radius;
        TVec3f hit;
        Triangle triangle;
        if (MR::getFirstPolyOnLineToMap(&hit, &triangle, s.origin, s.dir * reach)) {
            s.blockedAt = std::max((hit - s.origin).length() - Cappy::Const::Radius, 0.0f);
        }
        s.spin = 0.0f;
    }

    void placeModel(const TVec3f& up) {
        s.spin = std::fmod(s.spin + kSpinDegrees, 360.0f);
        const f32 angle = s.spin * 0.017453292f;
        TVec3f side;
        side.cross(up, s.dir);
        TVec3f front(s.dir * std::cos(angle) + side * std::sin(angle));
        const TVec3f base(s.pos - up * s.capOffsetUp - front * s.capOffsetFront);
        MR::makeMtxUpFrontPos(&sCapMtx, up, front, base);
        if (MR::isDead(s.cap)) {
            s.cap->appear();
        }
    }

    void doTrace(const MarioActor* pActor, const TVec3f& up, bool contactTest, bool contact) {
        if (!trace()) {
            return;
        }
        const Mario* mario = pActor->mMario;
        const Odyssey::Air air = Odyssey::Live::airKind;
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
    s.cap = new MarioParts(pActor, "キャッピー", "MarioHair", false, nullptr, sCapMtx.toMtxPtr());
    s.cap->initWithoutIter();
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
        showMarioCap(true);
        if (!MR::isDead(s.cap)) {
            s.cap->kill();
        }
        doTrace(pActor, up, false, false);
        return;
    case Cappy::State::Fly:
    case Cappy::State::Hover:
        s.pos = s.origin + s.dir * s.cappy.distance;
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

void petariCappyWallJump() {
    s.bounce.wallJump();
}

// The movement harness (native/app/smoke_movement.cpp): cap bounces so far, and
// Cappy's state (petari/cappy.hpp State).
extern "C" int petari_cappy_bounces() {
    return sBounces;
}

extern "C" int petari_cappy_state() {
    return static_cast< int >(s.cappy.state);
}
#endif
