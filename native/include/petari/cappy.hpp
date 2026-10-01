#pragma once
// Cappy for the Cappy mod (docs/dev/CAPPY.md): the cap's flight as a state
// machine over distances along the throw line, the cap-bounce contact test and
// the one-bounce-per-airtime rule. No game types, so the rules are unit-tested
// on their own (native/tests/cappy_tests.cpp); src/Game/Player/MarioCappy.cpp
// turns them into positions in Galaxy's world.
//
// Units are SMO's (= Galaxy's, scale 1.0): units/frame at 60 fps. Sources: D =
// OdysseyDecomp's PlayerConst, C = smo.wiki / the "Odyssey Movement Data v2"
// sheet, I = our choice where nothing is published (HackCap's own parameters
// are not).

#include <algorithm>
#include <cmath>

namespace PetariNative::Cappy {

namespace Const {
// Mario's throw.
constexpr int ThrowFrameGround = 12;   // SpinCapThrowFrame (D): Cappy leaves on this frame of the throw
constexpr int ThrowFrameAir = 8;       // SpinCapThrowFrameAir (D)
constexpr int PreInputFrame = 10;      // PreInputFrameCapThrow (D): an early press waits this long
constexpr int GroundBrakeFrames = 15;  // SpinBrakeFrame (D)
constexpr float GroundBrakeRate = 0.95f;  // SpinBrakeRate (D)
constexpr int JumpLockGround = 13;     // no jump or ground pound this many frames after the press (C)
constexpr int JumpLockAir = 9;
// Cappy's flight (C: the sheet's measured throw, 678 u by frame 21).
constexpr float StartDistance = 182.0f;  // ahead of Mario on the first frame
constexpr float StartHeight = 70.0f;     // above Mario's feet, along -gravity
constexpr float CruiseSpeed = 32.0f;     // frames 2-12
constexpr int CruiseEndFrame = 12;
constexpr float Brake = 3.2f;            // frames 13-21: 28.8, 25.6, ... 3.2
constexpr int HoldMaxFrame = 122;        // held: hovers until this frame after leaving (C)
constexpr int HoverFrames = 10;          // released: hovers this long (I)
constexpr int HoldLatchFrames = 30;      // a throw press while Cappy is out counts as held this long (I)
constexpr float ReturnAccel = 2.0f;      // (I)
constexpr float ReturnSpeedMax = 40.0f;  // (I)
constexpr float CatchRadius = 80.0f;     // (I)
constexpr float Radius = 50.0f;          // Cappy's size: he rests 50 above the floor (C)
constexpr float ReturnHeight = 110.0f;   // he returns to Mario's head (I)
// Cap bounce contact (C: smo.wiki Cap bounce). Heights above Mario's feet.
constexpr float BounceRadius = 100.0f;
constexpr float HeadHeight = 110.0f, HeadRadius = 40.0f;
constexpr float BodyHeight = 75.0f, BodyRadius = 50.0f;
constexpr float FootHeight = 40.0f, FootRadius = 40.0f;
constexpr float CatchPointHeight = 20.0f;  // above Cappy
constexpr float FootAboveMax = 70.0f;
constexpr float FootElevationMinDeg = 20.0f;
}  // namespace Const

enum class State : int {
    Idle,     // on Mario's head
    Windup,   // Mario's throw, before Cappy leaves
    Fly,      // out along the throw line
    Hover,    // at the end of the line
    Return,   // back to Mario
};

inline const char* stateName(State s) {
    switch (s) {
    case State::Idle: return "idle";
    case State::Windup: return "windup";
    case State::Fly: return "fly";
    case State::Hover: return "hover";
    case State::Return: return "return";
    }
    return "?";
}

struct Cap {
    State state = State::Idle;
    int frame = 0;          // frames in Windup; frames since leaving in Fly/Hover
    int windup = 0;         // Windup length (ground or air)
    float distance = 0.0f;  // along the throw line from the release point
    float speed = 0.0f;     // Fly: along the line; Return: toward Mario
    int hover = 0;          // frames hovered
    int latch = 0;          // frames a throw press still counts as held
};

// Mario starts a throw: Cappy leaves after the wind-up.
inline void startThrow(Cap& c, bool inAir) {
    c = Cap{};
    c.state = State::Windup;
    c.windup = inAir ? Const::ThrowFrameAir : Const::ThrowFrameGround;
}

// One frame of the flight. held: the throw button is down; blockedAt: if >= 0,
// a wall stops the line at this distance (the game's map check). Returns true
// on the frame Cappy leaves Mario (the game then fixes the throw line).
inline bool step(Cap& c, bool held, float blockedAt = -1.0f) {
    if (c.latch > 0) --c.latch;
    const bool holding = held || c.latch > 0;
    switch (c.state) {
    case State::Idle:
    case State::Return:  // the game moves him toward Mario (stepReturn)
        return false;
    case State::Windup:
        if (++c.frame < c.windup) return false;
        c.state = State::Fly;
        c.frame = 1;
        c.distance = Const::StartDistance;
        c.speed = Const::CruiseSpeed;
        if (blockedAt >= 0.0f && blockedAt < c.distance) {
            c.distance = blockedAt;
            c.state = State::Hover;
        }
        return true;
    case State::Fly: {
        ++c.frame;
        if (c.frame > Const::CruiseEndFrame) c.speed = std::max(c.speed - Const::Brake, 0.0f);
        const float next = c.distance + c.speed;
        if (blockedAt >= 0.0f && next >= blockedAt) {
            c.distance = std::max(blockedAt, c.distance);
            c.state = State::Hover;
        } else {
            c.distance = next;
            if (c.speed <= 0.0f) c.state = State::Hover;
        }
        return false;
    }
    case State::Hover:
        ++c.frame;
        ++c.hover;
        // Held: until HoldMaxFrame after leaving; released: HoverFrames.
        if (!(holding && c.frame <= Const::HoldMaxFrame) && c.hover > Const::HoverFrames) {
            c.state = State::Return;
            c.speed = 0.0f;
        }
        return false;
    }
    return false;
}

// A throw press while Cappy is out (for example the dive's): counts as holding
// for HoldLatchFrames, so a tapped loop (throw, ground pound, dive) still finds
// him there (I: SMO wants the button held).
inline void notePress(Cap& c) {
    if (c.state == State::Fly || c.state == State::Hover) c.latch = Const::HoldLatchFrames;
}

// Returning: the speed toward Mario this frame, given the distance to him.
// Returns true when he catches Cappy.
inline bool stepReturn(Cap& c, float distanceToMario) {
    c.speed = std::min(c.speed + Const::ReturnAccel, Const::ReturnSpeedMax);
    if (distanceToMario <= std::max(Const::CatchRadius, c.speed)) {
        c = Cap{};
        return true;
    }
    return false;
}

// After a bounce (or when the throw has to end): straight back to Mario.
inline void recall(Cap& c) {
    if (c.state == State::Fly || c.state == State::Hover) {
        c.state = State::Return;
        c.speed = 0.0f;
    }
}

inline bool bounceable(const Cap& c) { return c.state == State::Hover; }

// Contact for a cap bounce. up: Mario's up component and across distance of
// Cappy relative to Mario's feet (Cappy minus feet: `up` along -gravity,
// `across` the rest). groundPound: only the foot rule counts.
inline bool bounceContact(float up, float across, bool groundPound) {
    const auto sphere = [&](float height, float radius) {
        const float dy = up - height;
        return dy * dy + across * across <= (Const::BounceRadius + radius) * (Const::BounceRadius + radius);
    };
    if (!groundPound && (sphere(Const::HeadHeight, Const::HeadRadius) || sphere(Const::BodyHeight, Const::BodyRadius))) return true;
    // Foot: at most FootAboveMax above Cappy's catch point, and at least
    // FootElevationMinDeg above it seen from there.
    const float footAbove = Const::FootHeight - (up + Const::CatchPointHeight);
    if (footAbove < 0.0f || footAbove > Const::FootAboveMax + Const::FootRadius) return false;
    const float elevation = std::atan2(footAbove, across) * 57.29578f;
    return elevation >= Const::FootElevationMinDeg;
}

// One cap bounce per airtime: refreshed on landing, removed by a wall jump
// (until the next landing) and by the bounce itself.
struct BounceLimit {
    bool available = true;
    void land() { available = true; }
    void wallJump() { available = false; }
    void bounce() { available = false; }
};

// The air throw's hop limit (PlayerCapActionHistory's limit height): a new hop
// only below the height of the last air throw in this airtime.
struct HopLimit {
    bool recorded = false;
    float height = 0.0f;  // along -gravity, world
    bool allows(float h) const { return !recorded || h < height; }
    void record(float h) {
        recorded = true;
        height = h;
    }
    void land() { recorded = false; }
};

}  // namespace PetariNative::Cappy
