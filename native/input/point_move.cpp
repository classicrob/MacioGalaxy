#include "petari/point_move.hpp"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "petari/mods.hpp"

namespace PetariNative::PointMove {
namespace {
std::atomic<bool> gSteerable{false};
std::atomic<float> gStickX{0.0f};
std::atomic<float> gStickY{0.0f};
std::atomic<bool> gClaimed{false};
std::atomic<bool> gPointsValid{false};
std::atomic<float> gMarioX{0.0f}, gMarioY{0.0f}, gPointerX{0.0f}, gPointerY{0.0f};
std::atomic<unsigned> gStickReads{0};  // RemoteModel reads of the pointer stick (trace)

// PETARI_POINT_TRACE=1: claim decisions and, while steering, what is published and used.
bool tracing() {
    static const bool sOn = [] {
        const char* v = std::getenv("PETARI_POINT_TRACE");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return sOn;
}
}  // namespace

void stickFor(float dx, float dy, float screenHeight, float* x, float* y) {
    *x = 0.0f;
    *y = 0.0f;
    if (!(screenHeight > 0.0f)) return;
    const float length = std::sqrt(dx * dx + dy * dy);
    const float distance = length / screenHeight;
    if (!(distance > kDeadZone)) return;
    *x = dx / length;
    *y = -dy / length;  // screen up is stick up
}

void leftButton(bool down) {
    const bool on = Mods::enabled(Mods::Mod::PointToMove);
    const bool steerable = gSteerable.load(std::memory_order_relaxed);
    gClaimed.store(down && on && steerable, std::memory_order_relaxed);
    if (tracing()) {
        std::fprintf(stderr, "[point] left %s: mod %s, steerable %d -> %s (stick reads by the remote so far %u)\n", down ? "down" : "up",
                     on ? "on" : "off", steerable ? 1 : 0, down ? (on && steerable ? "claimed (steers, no B)" : "not claimed (B)") : "released",
                     gStickReads.load());
    }
}

bool claimsLeft() {
    return gClaimed.load(std::memory_order_relaxed);
}

bool stick(bool leftHeld, float* x, float* y) {
    if (!leftHeld || !claimsLeft() || !Mods::enabled(Mods::Mod::PointToMove)) return false;
    if (!gSteerable.load(std::memory_order_relaxed)) {
        // Normal play paused (a demo, a talk) during the hold: no movement, still no B.
        *x = 0.0f;
        *y = 0.0f;
        return true;
    }
    *x = gStickX.load(std::memory_order_relaxed);
    *y = gStickY.load(std::memory_order_relaxed);
    gStickReads.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void resetForTesting() {
    gSteerable.store(false);
    gStickX.store(0.0f);
    gStickY.store(0.0f);
    gClaimed.store(false);
    gPointsValid.store(false);
}

}  // namespace PetariNative::PointMove

extern "C" void petari_point_move_publish(bool steerable, float stickX, float stickY) {
    using namespace PetariNative::PointMove;
    if (tracing() && claimsLeft()) {
        static unsigned frames = 0;
        if (frames++ % 30 == 0) {
            std::fprintf(stderr, "[point] steering: steerable %d, stick (%.2f, %.2f), Mario (%.2f, %.2f), pointer (%.2f, %.2f), "
                                 "stick reads by the remote %u\n",
                         steerable ? 1 : 0, stickX, stickY, gMarioX.load(), gMarioY.load(), gPointerX.load(), gPointerY.load(),
                         gStickReads.load());
        }
    }
    gStickX.store(steerable ? stickX : 0.0f, std::memory_order_relaxed);
    gStickY.store(steerable ? stickY : 0.0f, std::memory_order_relaxed);
    gSteerable.store(steerable, std::memory_order_relaxed);
    if (!steerable) gPointsValid.store(false, std::memory_order_relaxed);
}

extern "C" void petari_point_move_publish_points(float marioX, float marioY, float pointerX, float pointerY) {
    using namespace PetariNative::PointMove;
    gMarioX.store(marioX, std::memory_order_relaxed);
    gMarioY.store(marioY, std::memory_order_relaxed);
    gPointerX.store(pointerX, std::memory_order_relaxed);
    gPointerY.store(pointerY, std::memory_order_relaxed);
    gPointsValid.store(true, std::memory_order_relaxed);
}

extern "C" bool petari_point_move_cue(float* marioX, float* marioY, float* pointerX, float* pointerY) {
    using namespace PetariNative::PointMove;
    if (!claimsLeft() || !gSteerable.load(std::memory_order_relaxed) || !gPointsValid.load(std::memory_order_relaxed)) return false;
    *marioX = gMarioX.load(std::memory_order_relaxed);
    *marioY = gMarioY.load(std::memory_order_relaxed);
    *pointerX = gPointerX.load(std::memory_order_relaxed);
    *pointerY = gPointerY.load(std::memory_order_relaxed);
    return true;
}

extern "C" bool petari_point_move_claimed(void) {
    return PetariNative::PointMove::claimsLeft();
}
