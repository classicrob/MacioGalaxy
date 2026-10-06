// PETARI_POINT_PROBE=1: proof runs for the Point to move mod (native/MODS.md
// "Point to move"), next to any smoke script; needs PETARI_MODS=PointToMove.
// Once normal play has been steady for 90 frames, it drives the real mouse
// path (pointer motion and the left button through the native input layer)
// and logs "[point-probe] <check>: ok|FAIL ..." lines:
//   near-runs: the pointer 5% of the screen height above Mario (just outside the 3%
//     dead zone): he must still run (at least 400 units in 75 frames).
//   up / right / down-left: the pointer kept 28% of the screen height from Mario
//     in that screen direction, left held 75 frames; Mario must move at least 100
//     units, and his movement seen on screen (each frame's step projected with
//     that frame's camera, summed) must point the same way (cosine at least 0.7).
//   stop: 20..40 frames after the release he moves under 15 units.
//   jump: running up, Space tapped mid-hold; he must rise at least 60 units and
//     keep moving up the screen.
//   menu: the pause menu open (Escape held), a left press is B for the game and
//     is not claimed.
// PETARI_POINT_PROBE_WHEN=mode:N or status:N: start only once Mario is in that player mode
// (MarioActor::mPlayerMode: 2 fire, 3 ice, 4 bee, 5 spring, 6 boo, 7 flying) or status (6 swim),
// e.g. after the stage script's bee/swim mechanic route or a warp onto a power-up.
// While it runs, the smoke script's own presses are dropped. Each left press starts the
// image-dump phase "point-hold" (petari/efb_dump_mark.hpp).

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/GamePadUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "petari/efb_dump_mark.hpp"
#include "petari/input.hpp"
#include "petari/point_move.hpp"
#include "smoke.hpp"

namespace PetariNative::App::Smoke {
namespace {

struct Check {
    const char* name;
    float dx, dy;  // screen direction (y down)
    bool jump;
    float distance;  // pointer distance from Mario, in screen heights
};
constexpr Check kChecks[] = {{"up", 0.0f, -1.0f, false, 0.28f},
                             {"right", 1.0f, 0.0f, false, 0.28f},
                             {"down-left", -0.7071f, 0.7071f, false, 0.28f},
                             {"near-runs", 0.0f, -1.0f, false, 0.05f},
                             {"jump", 0.0f, -1.0f, true, 0.28f}};
constexpr int kCheckCount = sizeof(kChecks) / sizeof(kChecks[0]);
constexpr int kHold = 75;

enum class Phase { Wait, Run, Stop, MenuOpen, MenuPress, MenuClose, Done };

struct Probe {
    bool enabled = false;
    Phase phase = Phase::Wait;
    int steady = 0;
    int check = 0;
    int frames = 0;
    TVec3f start{0, 0, 0}, releasePos{0, 0, 0}, stopFrom{0, 0, 0}, previous{0, 0, 0};
    float screenX = 0.0f, screenY = 0.0f;  // Mario's summed on-screen motion
    float maxRise = 0.0f;
    int ok = 0, fail = 0;
    int whenMode = -1, whenStatus = -1;  // PETARI_POINT_PROBE_WHEN
};

Probe& probe() {
    static Probe p = [] {
        Probe p;
        const char* on = std::getenv("PETARI_POINT_PROBE");
        p.enabled = on != nullptr && on[0] != '\0' && on[0] != '0';
        if (const char* when = std::getenv("PETARI_POINT_PROBE_WHEN")) {
            if (std::strncmp(when, "mode:", 5) == 0) p.whenMode = std::atoi(when + 5);
            if (std::strncmp(when, "status:", 7) == 0) p.whenStatus = std::atoi(when + 7);
        }
        if (p.enabled) std::fprintf(stderr, "[point-probe] on (when mode %d, status %d)\n", p.whenMode, p.whenStatus);
        return p;
    }();
    return p;
}

void report(Probe& p, const char* name, bool good, const std::string& detail) {
    (good ? p.ok : p.fail)++;
    std::fprintf(stderr, "[point-probe] %s: %s %s\n", name, good ? "ok" : "FAIL", detail.c_str());
    std::fflush(stderr);
}

bool screenOf(const TVec3f& world, TVec2f* out) {
    return MR::calcScreenPosition(out, world);
}

float length(const TVec3f& a, const TVec3f& b) {
    const float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return std::sqrt(x * x + y * y + z * z);
}

// The scene and Mario can be read (not while a scene changes or the app shuts down).
bool sceneReadable() {
    GameSystem* pSystem = SingletonHolder< GameSystem >::get();
    if (pSystem == nullptr || pSystem->mSceneController == nullptr) return false;
    if (!pSystem->mSceneController->isSceneInitializeState(SceneInitializeState_End)) return false;
    return MR::isExistSceneObj(SceneObj_MarioHolder) && MR::isExistMario();
}

void left(bool down) {
    Input::focusChanged(true);
    Input::mouseButtonEvent(Input::MouseButton::Left, down);
}

}  // namespace

PointProbeStep pointProbeFrame() {
    PointProbeStep step;
    // First, so its screenshots-only mode (no keys; returns false) runs beside an input script.
    const bool figure = figureProbeFrame();
    if (inputScriptFrame()) {
        step.active = true;  // the input script's keys: the smoke script's presses wait
        return step;
    }
    if (figure) {
        step.active = true;  // the figure probe's keys: the smoke script's presses wait
        return step;
    }
    Probe& p = probe();
    if (!p.enabled || p.phase == Phase::Done) return step;
    if (!sceneReadable()) {
        // A scene change or the shutdown: Mario's state is gone. Mid-probe, let go and stop.
        if (p.phase != Phase::Wait) {
            left(false);
            Input::keyEvent(Input::Key::Space, false, false);
            Input::keyEvent(Input::Key::Escape, false, false);
            std::fprintf(stderr, "[point-probe] the scene went away during the probe; stopped (%d ok, %d FAIL)\n", p.ok, p.fail);
            p.phase = Phase::Done;
        }
        p.steady = 0;
        return step;
    }
    const Observation o = observeGame(true, false);
    const bool play = o.playerValid && o.pausePermitted && !o.demoActive && !o.talkActive && MR::isExistMario();
    const float height = static_cast<float>(MR::getScreenHeight());
    const float width = static_cast<float>(MR::getScreenWidth());
    ++p.frames;

    auto aim = [&](float dx, float dy, float distance) {
        TVec2f mario;
        if (!screenOf(*MR::getPlayerCenterPos(), &mario)) return;
        step.pointer = true;
        step.u = (mario.x + dx * distance * height) / width;
        step.v = (mario.y + dy * distance * height) / height;
    };

    switch (p.phase) {
    case Phase::Wait:
        p.steady = play && (p.whenMode < 0 || o.playerMode == p.whenMode) && (p.whenStatus < 0 || o.marioStatus == p.whenStatus)
                       ? p.steady + 1
                       : 0;
        if (p.steady >= 90) {
            p.phase = Phase::Run;
            p.frames = 0;
            std::fprintf(stderr, "[point-probe] normal play steady (player mode %d, status %d); starting at (%.1f, %.1f, %.1f)\n",
                         o.playerMode, o.marioStatus, o.playerX, o.playerY, o.playerZ);
        }
        break;
    case Phase::Run: {
        const Check& c = kChecks[p.check];
        aim(c.dx, c.dy, c.distance);
        if (p.frames == 2) {
            p.start = *MR::getPlayerPos();
            p.previous = p.start;
            p.screenX = p.screenY = 0.0f;
            p.maxRise = 0.0f;
            PetariNative::EfbDump::mark("point-hold");  // image dumps (PETARI_SURFACE_DUMP_LABELS=point-hold)
            left(true);
        }
        if (p.check == 0 && p.frames == 30) {
            // The overlay's steering cue: the pointer point equals where the probe put the cursor.
            float mx = 0.0f, my = 0.0f, px = 0.0f, py = 0.0f;
            const bool cue = petari_point_move_cue(&mx, &my, &px, &py);
            const float ex = step.u * 2.0f - 1.0f, ey = step.v * 2.0f - 1.0f;
            char text[160];
            std::snprintf(text, sizeof(text), "cue %s: Mario (%.2f, %.2f), pointer (%.2f, %.2f), cursor set to (%.2f, %.2f)",
                          cue ? "shown" : "MISSING", mx, my, px, py, ex, ey);
            report(p, "cue", cue && std::fabs(px - ex) < 0.08f && std::fabs(py - ey) < 0.08f && py < my, text);
        }
        if (c.jump && p.frames == 40) Input::keyEvent(Input::Key::Space, true, false);
        if (c.jump && p.frames == 52) Input::keyEvent(Input::Key::Space, false, false);
        if (p.frames > 2) {
            const TVec3f& pos = *MR::getPlayerPos();
            TVec2f a, b;
            if (screenOf(p.previous, &a) && screenOf(pos, &b)) {
                p.screenX += b.x - a.x;
                p.screenY += b.y - a.y;
            }
            p.previous = pos;
            const float rise = -((pos.x - p.start.x) * o.gravityX + (pos.y - p.start.y) * o.gravityY + (pos.z - p.start.z) * o.gravityZ);
            if (rise > p.maxRise) p.maxRise = rise;
        }
        if (p.frames == 2 + kHold) {
            const TVec3f end = *MR::getPlayerPos();
            left(false);
            const bool seen = true;
            const float mx = p.screenX, my = p.screenY;
            const float ml = std::sqrt(mx * mx + my * my);
            const float cosine = ml > 0.0f ? (mx * c.dx + my * c.dy) / ml : 0.0f;
            const float moved = length(end, p.start);
            char text[200];
            std::snprintf(text, sizeof(text), "moved %.0f units, on-screen direction cosine %.2f (screen move %.0f, %.0f)%s%s", moved,
                          cosine, mx, my, c.jump ? ", max rise " : "", c.jump ? std::to_string(static_cast<int>(p.maxRise)).c_str() : "");
            const float minimum = c.distance < 0.1f ? 400.0f : 100.0f;  // near-runs: run distance, not a walk
            bool good = seen && moved >= minimum && cosine >= (c.jump ? 0.5f : 0.7f);
            if (c.jump) good = good && p.maxRise >= 60.0f;
            report(p, c.name, good, text);
            p.releasePos = end;
            p.phase = Phase::Stop;
            p.frames = 0;
        }
        break;
    }
    case Phase::Stop:
        if (p.frames == 20) p.stopFrom = *MR::getPlayerPos();
        if (p.frames == 40) {
            const float drift = length(*MR::getPlayerPos(), p.stopFrom);
            char text[120];
            std::snprintf(text, sizeof(text), "after releasing (%s): moved %.1f units in frames 20..40", kChecks[p.check].name, drift);
            if (!kChecks[p.check].jump) report(p, "stop", drift < 15.0f, text);
        }
        if (p.frames >= 90 && play) {
            p.frames = 0;
            p.phase = ++p.check < kCheckCount ? Phase::Run : Phase::MenuOpen;
        }
        break;
    case Phase::MenuOpen:
        if (p.frames == 1) Input::keyEvent(Input::Key::Escape, true, false);
        if (p.frames == 19) Input::keyEvent(Input::Key::Escape, false, false);
        if (p.frames == 80) {
            p.phase = Phase::MenuPress;
            p.frames = 0;
        }
        break;
    case Phase::MenuPress:
        step.pointer = true;
        step.u = 0.5f;
        step.v = 0.9f;
        if (p.frames == 2) left(true);
        if (p.frames == 8) {
            const bool b = MR::testCorePadButtonB(WPAD_CHAN0);
            report(p, "menu", !o.pausePermitted && b && !petari_point_move_claimed(),
                   std::string("pause menu ") + (o.pausePermitted ? "NOT open" : "open") + ", left press is " + (b ? "B" : "not B") +
                       (petari_point_move_claimed() ? ", claimed" : ", not claimed"));
            left(false);
        }
        if (p.frames == 30) {
            p.phase = Phase::MenuClose;
            p.frames = 0;
        }
        break;
    case Phase::MenuClose:
        if (p.frames == 1) Input::keyEvent(Input::Key::Escape, true, false);
        if (p.frames == 6) Input::keyEvent(Input::Key::Escape, false, false);
        if (p.frames == 60) {
            std::fprintf(stderr, "[point-probe] done: %d ok, %d FAIL\n", p.ok, p.fail);
            p.phase = Phase::Done;
        }
        break;
    case Phase::Done:
        break;
    }
    step.active = p.phase != Phase::Wait && p.phase != Phase::Done;
    return step;
}

}  // namespace PetariNative::App::Smoke
