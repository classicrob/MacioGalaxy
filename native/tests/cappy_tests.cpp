// Cappy mod model (native/include/petari/cappy.hpp) and its air moves in the
// movement model (petari/odyssey_move.hpp) against the numbers in
// docs/dev/CAPPY.md: the measured throw (678 u by frame 21), PlayerConst's hop
// and bounces, and smo.wiki's bounce rules.
#include "petari/cappy.hpp"
#include "petari/odyssey_move.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace PetariNative;

static int checks;
static void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        std::exit(1);
    }
}
static bool near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) <= eps; }

// Peak height of an air move, and the frame it is reached.
struct Apex {
    float height = 0.0f;
    int frame = 0;
};
static Apex apex(Odyssey::AirState s) {
    float y = 0.0f;
    Apex best;
    for (int f = 0; f < 600; ++f) {
        y += Odyssey::stepAir(s, false, 0.0f, 0.0f);
        if (y > best.height) {
            best.height = y;
            best.frame = f + 1;
        }
        if (s.up < 0.0f && y < best.height) break;
    }
    return best;
}

static void testThrowFlight() {
    Cappy::Cap c;
    Cappy::startThrow(c, false);
    int frames = 0;
    while (!Cappy::step(c, false)) ++frames;
    check(frames + 1 == Cappy::Const::ThrowFrameGround, "a ground throw leaves on frame 12");
    check(c.state == Cappy::State::Fly && near(c.distance, 182.0f), "first frame: 182 ahead");
    check(near(Cappy::shownDistance(c), 50.0f + 132.0f * 0.25f), "first frame shown out of Mario's hand");
    for (int f = 2; f <= 12; ++f) Cappy::step(c, false);
    check(near(c.distance, 182.0f + 11.0f * 32.0f), "frames 2-12 at 32 u/f: 534");
    check(near(Cappy::shownDistance(c), c.distance), "on the measured line after the emergence");
    Cappy::step(c, false);
    check(near(c.distance, 534.0f + 28.8f), "frame 13 brakes to 28.8");
    for (int f = 14; f <= 21; ++f) Cappy::step(c, false);
    check(near(c.distance, 678.0f), "frame 21: 678, the measured reach");
    check(c.state == Cappy::State::Fly, "still flying on frame 21 (3.2 u/f)");
    Cappy::step(c, false);
    check(c.state == Cappy::State::Hover && near(c.distance, 678.0f), "hovers from frame 22 at 678");

    Cappy::startThrow(c, true);
    frames = 0;
    while (!Cappy::step(c, false)) ++frames;
    check(frames + 1 == Cappy::Const::ThrowFrameAir, "an air throw leaves on frame 8");
}

static int hoverFrames(bool held, int pressAt = -1) {
    Cappy::Cap c;
    Cappy::startThrow(c, true);
    while (!Cappy::step(c, held)) {}
    int f = 1;
    while (c.state == Cappy::State::Fly) {
        Cappy::step(c, held);
        ++f;
    }
    int hover = 0;
    while (c.state == Cappy::State::Hover) {
        if (f == pressAt) Cappy::notePress(c);
        Cappy::step(c, held);
        ++f;
        ++hover;
    }
    return hover;
}

static void testHover() {
    check(hoverFrames(false) == Cappy::Const::HoverFrames + 1, "released: hovers 10 frames, then returns");
    // Held: hovers from frame 22 to frame 122 after leaving.
    check(hoverFrames(true) == 122 - 22 + 1, "held: at max length on frames 22-122");
    // A press at frame 25 (the dive's) keeps him there 30 frames from then.
    const int latched = hoverFrames(false, 25);
    check(latched == 25 + Cappy::Const::HoldLatchFrames - 22, "a press while out counts as held for 30 frames");

    Cappy::Cap c;
    Cappy::startThrow(c, false);
    while (!Cappy::step(c, true)) {}
    for (int f = 0; f < 300 && c.state != Cappy::State::Return; ++f) Cappy::step(c, true);
    check(c.state == Cappy::State::Return, "held forever: still returns");
}

static void testWallsAndReturn() {
    Cappy::Cap c;
    Cappy::startThrow(c, false);
    while (!Cappy::step(c, false, 300.0f)) {}
    Cappy::step(c, false, 300.0f);
    Cappy::step(c, false, 300.0f);
    Cappy::step(c, false, 300.0f);
    Cappy::step(c, false, 300.0f);
    check(c.state == Cappy::State::Hover && near(c.distance, 300.0f), "a wall at 300 stops the flight there");
    Cappy::startThrow(c, false);
    while (!Cappy::step(c, false, 100.0f)) {}
    check(c.state == Cappy::State::Hover && near(c.distance, 100.0f), "a wall closer than the first frame: hovers at it");

    Cappy::recall(c);
    check(c.state == Cappy::State::Return, "recall: returns");
    float dist = 678.0f;
    int frames = 0;
    while (!Cappy::stepReturn(c, dist)) {
        dist -= c.speed;
        ++frames;
    }
    check(c.state == Cappy::State::Idle, "caught: back on Mario's head");
    check(frames > 15 && frames < 30, "returns from 678 in 15-30 frames (" + std::to_string(frames) + ")");
}

static void testBounceContact() {
    using Cappy::bounceContact;
    check(bounceContact(75.0f, 0.0f, false), "Cappy at Mario's body: contact");
    check(bounceContact(-45.0f, 0.0f, false), "Cappy just under Mario's feet: contact");
    check(!bounceContact(-60.0f, 0.0f, false), "Cappy 60 under his feet (his top 35 below): no contact");
    check(bounceContact(75.0f, 119.0f, false), "body sphere: 70 + 50 across");
    check(!bounceContact(75.0f, 121.0f, false), "beyond 120 across: no contact");
    // The bounce the user saw with air: Cappy 85 below Mario's feet and 231
    // across met the foot cone, which had no distance bound.
    check(!bounceContact(-85.0f, 231.0f, false), "Cappy 85 below and 231 across: no contact (was a bounce with air)");
    check(!bounceContact(-30.0f, 150.0f, false), "Cappy 150 across at the feet: no contact");
    check(!bounceContact(-300.0f, 0.0f, false), "Cappy far below: no contact");
    // Ground pound: only the foot rule. Foot (40 up) at most 70 (+ its radius)
    // above the catch point (20 above Cappy), touching its sphere, at least 20
    // degrees above it.
    check(bounceContact(-45.0f, 0.0f, true), "pound onto Cappy 45 below: foot rule");
    check(!bounceContact(-200.0f, 0.0f, true), "pound with Cappy 200 below: too far");
    check(!bounceContact(-60.0f, 400.0f, true), "pound with Cappy far to the side: below 20 degrees");
    check(!bounceContact(75.0f, 0.0f, true), "pound: the body sphere does not count");

    Cappy::BounceLimit limit;
    check(limit.available, "a bounce is available at the start");
    limit.bounce();
    check(!limit.available, "one bounce per airtime");
    limit.land();
    check(limit.available, "landing refreshes it");
    limit.wallJump();
    check(!limit.available, "a wall jump removes it");
    limit.land();
    check(limit.available, "until the next landing");
}

static void testAirMoves() {
    using namespace Odyssey;
    // The air throw: up 6, gravity 0.3, measured 5.7 on the first frame and a
    // 57 u peak at frame 19.
    AirState hop = startCapThrow(14.0f, -10.0f, true);
    check(near(hop.front, 7.0f) && near(hop.cap, 7.0f), "air throw: horizontal speed capped at 7");
    const float first = stepAir(hop, false, 0.0f, 0.0f);
    check(near(first, 5.7f), "air throw: 5.7 on the first frame");
    const Apex a = apex(startCapThrow(0.0f, 0.0f, true));
    check(near(a.height, 57.0f) && a.frame == 19, "air throw: peak 57 at frame 19 (" + std::to_string(a.height) + ")");
    AirState fall = startCapThrow(0.0f, 0.0f, true);
    for (int f = 0; f < Const::SpinAirFrames; ++f) stepAir(fall, false, 0.0f, 0.0f);
    check(fall.kind == Air::CapThrow, "the throw lasts 24 frames");
    const float up = fall.up;
    stepAir(fall, false, 0.0f, 0.0f);
    check(fall.kind == Air::Fall && near(fall.up, up - 1.5f) && near(fall.cap, 11.0f), "then a normal fall (gravity 1.5, cap 11)");
    // Side speed: 24 frames to reach 7 at 0.3 a frame.
    AirState side = startCapThrow(0.0f, 0.0f, true);
    int frames = 0;
    while (side.side < 7.0f - 1e-3f && frames < 100) {
        stepAir(side, false, 0.0f, 1.0f);
        ++frames;
    }
    check(frames == 24, "air throw: 24 frames to full side speed (" + std::to_string(frames) + ")");
    AirState noHop = startCapThrow(10.0f, -12.0f, false);
    check(near(noHop.up, -12.0f), "a repeat throw above the limit: no hop, the fall goes on");
    AirState rising = startCapThrow(10.0f, 8.0f, false);
    check(near(rising.up, 0.0f), "a repeat throw while rising: stops the rise, no new height");

    // Bounces (measured = constant minus one frame of gravity).
    const Apex dive = apex(startAir(Air::DiveCapBounce, 20.0f));
    check(near(dive.height, 231.0f) && dive.frame == 21, "dive cap bounce: 231 in 21 frames");
    const Apex regular = apex(startAir(Air::CapBounce, 14.0f));
    check(near(regular.height, 300.0f) && regular.frame == 24, "regular cap bounce: 300 in 24 frames");
    const Apex pound = apex(startAir(Air::GroundPoundCapBounce, 0.0f));
    // Measured 390.5 in 22 frames; the per-frame sum adds a last 0.5 on frame 23
    // (as the ground-pound jump's 513.5 against a measured 513).
    check(near(pound.height, 391.0f) && pound.frame == 23, "ground-pound cap bounce: 391 (measured 390.5)");
    check(near(startAir(Air::DiveCapBounce, 20.0f).front, 16.0f), "dive bounce: horizontal min(v, 16)");
    check(near(startAir(Air::CapBounce, 30.0f).front, 24.0f) && near(startAir(Air::CapBounce, 10.0f).cap, 14.0f),
          "regular bounce: start min(v, 24), cap max(v, 14)");
    check(!extends(Air::CapBounce) && !extends(Air::DiveCapBounce), "bounces do not extend while A is held");

    Cappy::HopLimit limit;
    check(limit.allows(100.0f), "the first air throw hops");
    limit.record(100.0f);
    check(!limit.allows(120.0f) && !limit.allows(100.0f) && limit.allows(80.0f), "later ones only below its height");
    limit.land();
    check(limit.allows(500.0f), "landing clears the limit");
}

int main() {
    testThrowFlight();
    testHover();
    testWallsAndReturn();
    testBounceContact();
    testAirMoves();
    std::printf("cappy model: %d checks passed\n", checks);
    return 0;
}
