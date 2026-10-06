// PETARI_FIGURE_PROBE=1: proof runs for the Generic character mod (native/MODS.md
// "Generic character"), next to any smoke script. Once normal play has been
// steady for 90 frames it plays a fixed key script (running right, so the
// camera sees the figure side on) and takes screenshots into
// PETARI_FIGURE_PROBE_SHOTS (default: PETARI_SKIP_PROBE_SHOTS) as
// figure-<pose>.png: idle, run, jump, spin, longjump, front, cappy-throw,
// cappy-out, dive, and logs where the player is in the image for crops. The same script with the mod off gives the side-by-side. The poses that
// need other mods (Cappy: Cappy + OdysseyMovement; the dive: OdysseyMovement)
// show whatever the keys do without them. PETARI_FIGURE_PROBE_FROM=<frame> starts
// the script there (300: the front view, Cappy and the dive). PETARI_FIGURE_PROBE_WHEN=mode:N
// waits for that player mode (MarioActor::mPlayerMode: 2 fire, 4 bee, 6 boo, ...).
// PETARI_FIGURE_PROBE_SHOTS_AT=<frame>,<frame>: no keys, only figure-at-<frame>.png at those
// frames after steady play (beside another driver such as PETARI_INPUT_SCRIPT). While it runs, the smoke script's own
// presses are dropped (pointProbeFrame reports it active).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "petari/input.hpp"
#include "petari/screenshot.hpp"
#include "smoke.hpp"

namespace PetariNative::App::Smoke {
namespace {

struct PoseStep {
    int frame;
    Input::KeyCode key;  // 0: a screenshot
    bool down;
    const char* shot;
};

constexpr PoseStep kScript[] = {
    {0, 0, false, "idle"},
    {5, Input::Key::D, true, nullptr},  // a short run right (side on), a jump, and back
    {35, 0, false, "run"},
    {38, Input::Key::Space, true, nullptr},
    {48, 0, false, "jump"},
    {52, Input::Key::Space, false, nullptr},
    {62, Input::Key::D, false, nullptr},
    {64, Input::Key::A, true, nullptr},
    {100, Input::Key::A, false, nullptr},
    {130, Input::Key::F, true, nullptr},
    {134, Input::Key::F, false, nullptr},
    {140, 0, false, "spin"},
    {180, Input::Key::W, true, nullptr},  // a long jump away from the camera
    {210, Input::Key::LeftShift, true, nullptr},
    {213, Input::Key::Space, true, nullptr},
    {218, Input::Key::LeftShift, false, nullptr},
    {218, Input::Key::Space, false, nullptr},
    {226, 0, false, "longjump"},
    {262, Input::Key::W, false, nullptr},
    {300, Input::Key::S, true, nullptr},  // back toward the camera: a front view
    {322, Input::Key::S, false, nullptr},
    {360, 0, false, "front"},
    {370, Input::Key::R, true, nullptr},
    {374, Input::Key::R, false, nullptr},
    {382, 0, false, "cappy-throw"},
    {398, 0, false, "cappy-out"},
    {450, Input::Key::S, true, nullptr},  // a jump, a ground pound, Jump: the Odyssey dive
    {465, Input::Key::Space, true, nullptr},
    {475, Input::Key::Space, false, nullptr},
    {482, Input::Key::LeftShift, true, nullptr},
    {486, Input::Key::LeftShift, false, nullptr},
    {490, Input::Key::Space, true, nullptr},
    {498, 0, false, "dive"},
    {502, Input::Key::Space, false, nullptr},
    {520, Input::Key::S, false, nullptr},
    {560, 0, false, nullptr},  // the end
};

enum class Phase { Wait, Run, Done };

struct Probe {
    bool enabled = false;
    Phase phase = Phase::Wait;
    int steady = 0;
    int frame = 0;
    int whenMode = -1;  // PETARI_FIGURE_PROBE_WHEN=mode:N: start once the player is in that mode
    std::vector<int> shotsAt;  // PETARI_FIGURE_PROBE_SHOTS_AT: screenshots only, at these frames (no keys)
    std::string shots;
};

Probe& probe() {
    static Probe p = [] {
        Probe p;
        const char* on = std::getenv("PETARI_FIGURE_PROBE");
        p.enabled = on != nullptr && on[0] != '\0' && on[0] != '0';
        const char* dir = std::getenv("PETARI_FIGURE_PROBE_SHOTS");
        if (dir == nullptr) dir = std::getenv("PETARI_SKIP_PROBE_SHOTS");
        if (dir != nullptr) p.shots = dir;
        if (const char* at = std::getenv("PETARI_FIGURE_PROBE_SHOTS_AT"); at != nullptr) {
            for (const char* q = at; *q != '\0';) {
                p.shotsAt.push_back(std::atoi(q));
                const char* comma = std::strchr(q, ',');
                if (comma == nullptr) break;
                q = comma + 1;
            }
        }
        if (const char* when = std::getenv("PETARI_FIGURE_PROBE_WHEN"); when != nullptr && std::strncmp(when, "mode:", 5) == 0) {
            p.whenMode = std::atoi(when + 5);
        }
        if (p.enabled) std::fprintf(stderr, "[figure-probe] on, shots in %s\n", p.shots.empty() ? "(none)" : p.shots.c_str());
        return p;
    }();
    return p;
}

bool sceneReadable() {
    GameSystem* pSystem = SingletonHolder< GameSystem >::get();
    if (pSystem == nullptr || pSystem->mSceneController == nullptr) return false;
    if (!pSystem->mSceneController->isSceneInitializeState(SceneInitializeState_End)) return false;
    return MR::isExistSceneObj(SceneObj_MarioHolder) && MR::isExistMario();
}

void releaseAll() {
    for (const PoseStep& s : kScript) {
        if (s.key != 0) Input::keyEvent(s.key, false, false);
    }
}

}  // namespace

bool figureProbeFrame() {
    Probe& p = probe();
    if (!p.enabled || p.phase == Phase::Done) return false;
    if (!sceneReadable()) {
        if (p.phase == Phase::Run && !p.shotsAt.empty()) {
            p.phase = Phase::Wait;  // screenshots only: count again from the next scene's player
            p.steady = 0;
            return false;
        }
        if (p.phase == Phase::Run) {
            releaseAll();
            std::fprintf(stderr, "[figure-probe] the scene went away during the script; stopped\n");
            p.phase = Phase::Done;
        }
        p.steady = 0;
        return false;
    }
    const Observation o = observeGame(true, false);
    const bool play = o.playerValid && o.pausePermitted && !o.demoActive && !o.talkActive;
    if (p.phase == Phase::Wait) {
        p.steady = play && (p.whenMode < 0 || o.playerMode == p.whenMode) ? p.steady + 1 : 0;
        // Screenshots only: from the first frame with the player (another driver may keep play from looking steady).
        const bool placed = o.playerValid && (o.playerX != 0.0f || o.playerY != 0.0f || o.playerZ != 0.0f);
        if (p.steady < 90 && !(!p.shotsAt.empty() && placed)) return false;
        p.phase = Phase::Run;
        p.frame = 0;
        if (const char* from = std::getenv("PETARI_FIGURE_PROBE_FROM")) p.frame = std::atoi(from);  // skip the steps before it
        std::fprintf(stderr, "[figure-probe] normal play steady (player mode %d); script starts at (%.1f, %.1f, %.1f)\n", o.playerMode,
                     o.playerX, o.playerY, o.playerZ);
    }
    if (!p.shotsAt.empty()) {
        // Screenshots only (beside another driver, e.g. PETARI_INPUT_SCRIPT): no keys pressed.
        int last = 0;
        for (int at : p.shotsAt) {
            last = std::max(last, at);
            if (at == p.frame && !p.shots.empty()) {
                const std::string file = p.shots + "/figure-at-" + std::to_string(at) + ".png";
                const bool requested = Screenshot::request(file.c_str());
                std::fprintf(stderr, "[figure-probe] frame %d at (%.1f, %.1f, %.1f): %s\n", p.frame, o.playerX, o.playerY, o.playerZ,
                             requested ? file.c_str() : "no screenshot");
            }
        }
        if (p.frame % 200 == 0) {
            std::fprintf(stderr, "[figure-probe] shots-only frame %d at (%.1f, %.1f, %.1f)\n", p.frame, o.playerX, o.playerY, o.playerZ);
        }
        if (p.frame >= last) {
            std::fprintf(stderr, "[figure-probe] done\n");
            p.phase = Phase::Done;
        }
        std::fflush(stderr);
        ++p.frame;
        return false;  // the other driver keeps going
    }
    Input::focusChanged(true);
    for (const PoseStep& s : kScript) {
        if (s.frame != p.frame) continue;
        if (s.key != 0) {
            Input::keyEvent(s.key, s.down, false);
        } else if (s.shot != nullptr) {
            const std::string file = p.shots + "/figure-" + s.shot + ".png";
            const bool requested = !p.shots.empty() && Screenshot::request(file.c_str());
            // Where the player is in the image (0..1), for crops.
            TVec2f screen(0.0f, 0.0f);
            const bool seen = MR::calcScreenPosition(&screen, *MR::getPlayerCenterPos());
            const float u = seen ? screen.x / static_cast<float>(MR::getScreenWidth()) : -1.0f;
            const float v = seen ? screen.y / static_cast<float>(MR::getScreenHeight()) : -1.0f;
            std::fprintf(stderr, "[figure-probe] frame %d %s at (%.1f, %.1f, %.1f) mode %d screen %.3f %.3f: %s\n", p.frame, s.shot, o.playerX,
                         o.playerY, o.playerZ, o.playerMode, u, v, requested ? file.c_str() : "no screenshot");
        } else {
            releaseAll();
            std::fprintf(stderr, "[figure-probe] done\n");
            p.phase = Phase::Done;
        }
    }
    std::fflush(stderr);
    ++p.frame;
    return true;
}

}  // namespace PetariNative::App::Smoke
