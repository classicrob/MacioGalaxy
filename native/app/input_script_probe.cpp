// PETARI_INPUT_SCRIPT=<file>: types a player's keyboard script through the native input layer
// (Input::keyEvent, the path a person's keys take, as the skip and point probes do), once normal
// play has been steady for PETARI_INPUT_SCRIPT_STEADY frames (default 60). Test-only; inert unless set.
// Script lines: "<frames> [key ...]" with key names as in controls.txt (Space, W, LeftShift, J, ...);
// '#' starts a comment. Each line holds its keys for <frames> frames; presses and releases are sent
// only when the held set changes, so a key held across lines stays down.
// PETARI_INPUT_SCRIPT_LOG=<csv>: one row per scripted frame: frame, line, held keys, Mario's position,
// velocity and ground flag, the camera's position and forward axis, whether he is dead, and his gravity vector.
// Logs "[input-script] start ..." and "[input-script] done after N frames". The start also marks the image-dump
// phase "input-script" (petari/efb_dump_mark.hpp), so PETARI_SURFACE_DUMP_LABELS=input-script dumps only the script.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "petari/efb_dump_mark.hpp"
#include "petari/input.hpp"
#include "smoke.hpp"

namespace PetariNative::App::Smoke {
namespace {

struct Line {
    int frames = 0;
    std::vector<Input::KeyCode> keys;
    std::string text;
};

enum class Phase { Off, Wait, Run, Done };

struct InputScript {
    Phase phase = Phase::Off;
    std::vector<Line> lines;
    int steadyNeeded = 60, steady = 0;
    int frame = 0, line = 0, lineFrame = 0;
    std::vector<Input::KeyCode> held;
    std::FILE* log = nullptr;
};

InputScript& script() {
    static InputScript s = [] {
        InputScript s;
        const char* path = std::getenv("PETARI_INPUT_SCRIPT");
        if (path == nullptr || path[0] == '\0') return s;
        if (const char* v = std::getenv("PETARI_INPUT_SCRIPT_STEADY")) s.steadyNeeded = std::atoi(v);
        std::ifstream in(path);
        std::string text;
        int number = 0;
        while (std::getline(in, text)) {
            ++number;
            const std::string body = text.substr(0, text.find('#'));
            std::istringstream words(body);
            Line line;
            if (!(words >> line.frames)) continue;
            std::string name;
            bool ok = line.frames > 0;
            while (ok && words >> name) {
                Input::KeyCode code = 0;
                if (!Input::parseKeyName(name, &code)) {
                    std::fprintf(stderr, "[input-script] %s:%d: unknown key %s; script ignored\n", path, number, name.c_str());
                    ok = false;
                }
                line.keys.push_back(code);
            }
            if (!ok) return InputScript{};
            line.text = body;
            s.lines.push_back(line);
        }
        if (s.lines.empty()) {
            std::fprintf(stderr, "[input-script] %s: no lines; ignored\n", path);
            return s;
        }
        if (const char* logPath = std::getenv("PETARI_INPUT_SCRIPT_LOG")) {
            s.log = std::fopen(logPath, "w");
            if (s.log != nullptr) {
                std::fputs("frame,line,keys,x,y,z,vx,vy,vz,ground,cam_x,cam_y,cam_z,cam_fx,cam_fy,cam_fz,dead,grav_x,grav_y,grav_z\n", s.log);
            }
        }
        s.phase = Phase::Wait;
        std::fprintf(stderr, "[input-script] on: %zu lines from %s\n", s.lines.size(), path);
        return s;
    }();
    return s;
}

bool sceneReadable() {
    GameSystem* pSystem = SingletonHolder< GameSystem >::get();
    if (pSystem == nullptr || pSystem->mSceneController == nullptr) return false;
    if (!pSystem->mSceneController->isSceneInitializeState(SceneInitializeState_End)) return false;
    return MR::isExistSceneObj(SceneObj_MarioHolder) && MR::isExistMario();
}

bool contains(const std::vector<Input::KeyCode>& keys, Input::KeyCode code) {
    for (Input::KeyCode k : keys) {
        if (k == code) return true;
    }
    return false;
}

void hold(InputScript& s, const std::vector<Input::KeyCode>& want) {
    for (Input::KeyCode k : s.held) {
        if (!contains(want, k)) Input::keyEvent(k, false, false);
    }
    for (Input::KeyCode k : want) {
        if (!contains(s.held, k)) Input::keyEvent(k, true, false);
    }
    s.held = want;
}

void finish(InputScript& s, const char* why) {
    hold(s, {});
    if (s.log != nullptr) {
        std::fclose(s.log);
        s.log = nullptr;
    }
    s.phase = Phase::Done;
    std::fprintf(stderr, "[input-script] %s after %d frames\n", why, s.frame);
    std::fflush(stderr);
}

}  // namespace

bool inputScriptFrame() {
    InputScript& s = script();
    if (s.phase == Phase::Off || s.phase == Phase::Done) return false;
    if (!sceneReadable()) {
        if (s.phase == Phase::Run) finish(s, "stopped (the scene went away)");
        s.steady = 0;
        return false;
    }
    if (s.phase == Phase::Wait) {
        const Observation o = observeGame(true, false);
        const bool play = o.playerValid && o.pausePermitted && !o.demoActive && !o.talkActive;
        s.steady = play ? s.steady + 1 : 0;
        if (s.steady < s.steadyNeeded) return false;
        s.phase = Phase::Run;
        Input::focusChanged(true);  // keys count as a focused player's, as for the other probes
        // Image dumps (PETARI_SURFACE_DUMP / PETARI_XFB_DUMP with _LABELS=input-script) count from here, so dump
        // index N is script frame N (plus the presentation latency; see docs/dev/ODYSSEY_CAMERA.md).
        PetariNative::EfbDump::mark("input-script");
        const TVec3f& pos = *MR::getPlayerPos();
        std::fprintf(stderr, "[input-script] start at (%.1f, %.1f, %.1f)\n", pos.x, pos.y, pos.z);
    }
    if (s.line >= static_cast< int >(s.lines.size())) {
        finish(s, "done");
        return false;
    }
    const Line& line = s.lines[s.line];
    hold(s, line.keys);
    if (s.log != nullptr) {
        const TVec3f& p = *MR::getPlayerPos();
        const TVec3f& v = *MR::getPlayerVelocity();
        const TVec3f c = MR::getCamPos();
        const TVec3f f = MR::getCamZdir();
        std::string keys;
        for (Input::KeyCode k : s.held) keys += (keys.empty() ? "" : "+") + Input::keyName(k);
        const TVec3f& g = *MR::getPlayerGravity();
        std::fprintf(s.log, "%d,%d,%s,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%d,%.2f,%.2f,%.2f,%.4f,%.4f,%.4f,%d,%.4f,%.4f,%.4f\n", s.frame, s.line,
                     keys.empty() ? "-" : keys.c_str(), p.x, p.y, p.z, v.x, v.y, v.z, MR::isOnGroundPlayer() ? 1 : 0, c.x, c.y, c.z, f.x,
                     f.y, f.z, MR::isPlayerDead() ? 1 : 0, g.x, g.y, g.z);
    }
    ++s.frame;
    if (++s.lineFrame >= line.frames) {
        s.lineFrame = 0;
        ++s.line;
    }
    return true;
}

}  // namespace PetariNative::App::Smoke
