// The level editor's Edit mode, host side (petari/editor_settings.hpp, petari/editor_input.h).
#include "petari/editor_input.h"
#include "petari/editor_settings.hpp"
#include "petari/mods.hpp"

#include <atomic>
#include <chrono>

namespace PetariNative::EditorSettings {
namespace {

long long nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void addFloat(std::atomic<float>& value, float delta) {
    float current = value.load(std::memory_order_relaxed);
    while (!value.compare_exchange_weak(current, current + delta, std::memory_order_relaxed)) {
    }
}

std::atomic<bool> gActive{false};
std::atomic<bool> gRequested{false};
std::atomic<long long> gRequestTime{0};
std::atomic<int> gToggles{0}, gEnters{0}, gLeaves{0}, gFrames{0}, gRepivots{0};
std::atomic<float> gOrbitYaw{0}, gOrbitPitch{0}, gLookYaw{0}, gLookPitch{0}, gPanRight{0}, gPanUp{0}, gDolly{0};
std::atomic<float> gMoveRight{0}, gMoveForward{0}, gMoveUp{0}, gSpeed{1}, gCursorX{0}, gCursorY{0};
std::atomic<bool> gWorldUp{false};
std::atomic<int> gPlayFrom{0};
std::atomic<bool> gTyping{false}, gDeferredToggle{false};

void request() {
    gRequestTime.store(nowNs(), std::memory_order_relaxed);
    gRequested.store(true, std::memory_order_release);
}

void resetCamera() {
    gFrames.store(0);
    gRepivots.store(0);
    for (auto* value : {&gOrbitYaw, &gOrbitPitch, &gLookYaw, &gLookPitch, &gPanRight, &gPanUp, &gDolly, &gMoveRight,
                        &gMoveForward, &gMoveUp}) {
        value->store(0.0f);
    }
    gSpeed.store(1.0f);
}

}  // namespace

bool enabled() { return Mods::enabled(Mods::Mod::LevelEditor); }
bool active() { return gActive.load(std::memory_order_acquire); }
void setTyping(bool typing) { gTyping.store(typing, std::memory_order_relaxed); }
bool typing() { return gTyping.load(std::memory_order_relaxed); }
void deferToggle() { gDeferredToggle.store(true, std::memory_order_relaxed); }
bool takeDeferredToggle() { return gDeferredToggle.exchange(false, std::memory_order_relaxed); }

bool capturing() {
    return active() || (gRequested.load(std::memory_order_acquire) &&
                        nowNs() - gRequestTime.load(std::memory_order_relaxed) < 300'000'000LL);
}
void toggle() {
    if (!enabled()) return;
    if (!active()) request();
    gToggles.fetch_add(1, std::memory_order_relaxed);
}
void requestEnter() {
    if (!enabled() || active()) return;
    gEnters.fetch_add(1, std::memory_order_relaxed);
}
void requestLeave() {
    if (active()) gLeaves.fetch_add(1, std::memory_order_relaxed);
}

void orbit(float yaw, float pitch) { addFloat(gOrbitYaw, yaw); addFloat(gOrbitPitch, pitch); }
void look(float yaw, float pitch) { addFloat(gLookYaw, yaw); addFloat(gLookPitch, pitch); }
void pan(float right, float up) { addFloat(gPanRight, right); addFloat(gPanUp, up); }
void dolly(float steps) { addFloat(gDolly, steps); }
void move(float right, float forward, float up) {
    gMoveRight.store(right, std::memory_order_relaxed);
    gMoveForward.store(forward, std::memory_order_relaxed);
    gMoveUp.store(up, std::memory_order_relaxed);
}
void speed(float multiplier) { gSpeed.store(multiplier, std::memory_order_relaxed); }
void repivot() { gRepivots.fetch_add(1, std::memory_order_relaxed); }
void cursor(float x, float y) {
    gCursorX.store(x, std::memory_order_relaxed);
    gCursorY.store(y, std::memory_order_relaxed);
}
void frame() { gFrames.fetch_add(1, std::memory_order_relaxed); }
void setWorldUp(bool world) { gWorldUp.store(world, std::memory_order_relaxed); }
bool worldUp() { return gWorldUp.load(std::memory_order_relaxed); }
void setPlayFrom(int from) { gPlayFrom.store(from, std::memory_order_relaxed); }

void resetForTesting() {
    gActive.store(false);
    gRequested.store(false);
    gToggles.store(0);
    gEnters.store(0);
    gLeaves.store(0);
    gWorldUp.store(false);
    resetCamera();
}

}  // namespace PetariNative::EditorSettings

extern "C" void petari_editor_take_input(PetariEditorInput* out) {
    namespace S = PetariNative::EditorSettings;
    *out = PetariEditorInput{};
    out->enabled = S::enabled() ? 1 : 0;
    out->toggle = S::gToggles.exchange(0, std::memory_order_relaxed);
    if (!S::capturing()) {
        // Pressed where no scene could answer (and lapsed): not carried over.
        out->toggle = 0;
        S::gRequested.store(false, std::memory_order_release);
    }
    out->enter = S::gEnters.exchange(0, std::memory_order_relaxed);
    out->leave = S::gLeaves.exchange(0, std::memory_order_relaxed);
    out->orbitYaw = S::gOrbitYaw.exchange(0.0f, std::memory_order_relaxed);
    out->orbitPitch = S::gOrbitPitch.exchange(0.0f, std::memory_order_relaxed);
    out->lookYaw = S::gLookYaw.exchange(0.0f, std::memory_order_relaxed);
    out->lookPitch = S::gLookPitch.exchange(0.0f, std::memory_order_relaxed);
    out->panRight = S::gPanRight.exchange(0.0f, std::memory_order_relaxed);
    out->panUp = S::gPanUp.exchange(0.0f, std::memory_order_relaxed);
    out->dolly = S::gDolly.exchange(0.0f, std::memory_order_relaxed);
    out->moveRight = S::gMoveRight.load(std::memory_order_relaxed);
    out->moveForward = S::gMoveForward.load(std::memory_order_relaxed);
    out->moveUp = S::gMoveUp.load(std::memory_order_relaxed);
    out->speed = S::gSpeed.load(std::memory_order_relaxed);
    out->repivot = S::gRepivots.exchange(0, std::memory_order_relaxed);
    out->cursorX = S::gCursorX.load(std::memory_order_relaxed);
    out->cursorY = S::gCursorY.load(std::memory_order_relaxed);
    out->frame = S::gFrames.exchange(0, std::memory_order_relaxed);
    out->worldUp = S::worldUp() ? 1 : 0;
    out->playFrom = S::gPlayFrom.load(std::memory_order_relaxed);
}

extern "C" void petari_editor_set_active(int active) {
    namespace S = PetariNative::EditorSettings;
    S::gActive.store(active != 0, std::memory_order_release);
    S::gRequested.store(false, std::memory_order_release);
    S::gEnters.store(0);
    S::gLeaves.store(0);
    S::resetCamera();
}
