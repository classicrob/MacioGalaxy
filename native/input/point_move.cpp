#include "petari/point_move.hpp"

#include <atomic>
#include <cmath>

#include "petari/mods.hpp"

namespace PetariNative::PointMove {
namespace {
std::atomic<bool> gSteerable{false};
std::atomic<float> gStickX{0.0f};
std::atomic<float> gStickY{0.0f};
std::atomic<bool> gClaimed{false};
}  // namespace

void stickFor(float dx, float dy, float screenHeight, float* x, float* y) {
    *x = 0.0f;
    *y = 0.0f;
    if (!(screenHeight > 0.0f)) return;
    const float length = std::sqrt(dx * dx + dy * dy);
    const float distance = length / screenHeight;
    if (!(distance > kDeadZone)) return;
    const float magnitude = std::fmin(1.0f, (distance - kDeadZone) / (kFullStick - kDeadZone));
    *x = dx / length * magnitude;
    *y = -dy / length * magnitude;  // screen up is stick up
}

void leftButton(bool down) {
    gClaimed.store(down && Mods::enabled(Mods::Mod::PointToMove) && gSteerable.load(std::memory_order_relaxed),
                   std::memory_order_relaxed);
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
    return true;
}

void resetForTesting() {
    gSteerable.store(false);
    gStickX.store(0.0f);
    gStickY.store(0.0f);
    gClaimed.store(false);
}

}  // namespace PetariNative::PointMove

extern "C" void petari_point_move_publish(bool steerable, float stickX, float stickY) {
    using namespace PetariNative::PointMove;
    gStickX.store(steerable ? stickX : 0.0f, std::memory_order_relaxed);
    gStickY.store(steerable ? stickY : 0.0f, std::memory_order_relaxed);
    gSteerable.store(steerable, std::memory_order_relaxed);
}

extern "C" bool petari_point_move_claimed(void) {
    return PetariNative::PointMove::claimsLeft();
}
