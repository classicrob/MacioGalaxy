#ifdef PETARI_NATIVE
#include "Game/Scene/TestServerInput.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include <petari/input.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <strings.h>
#include <vector>

// Command forms (one line, words separated by spaces):
//   input [log=<file>] <step> [<step>...]      step = frames,x,y,buttons
//       x,y: the stick, -1..1, y up. buttons: '-' or names joined by '+': A B Z Spin Cap CamLeft CamRight Plus Minus.
//       A name is held for the whole step; a trailing '!' presses it for the first 6 frames only (a tap).
//   route <csv> [log=<file>] [from=<frame>] [frames=<n>]
//       replays a PETARI_ROUTE_RECORD csv: per recorded frame, move_x/move_y, jump (A), spin, crouch (Z), camera left/right.
// Reply: ok frames=<n> mario=(x,y,z) vel=(x,y,z) ground=<0|1> dead=<0|1>; the log file has one row per frame:
//   frame,step,x,y,z,vx,vy,vz,ground.
// State is POD and static: a std container kept across frames could live on a scene heap that a scene change reclaims.
namespace {
    using namespace PetariNative;

    enum Bit : unsigned { kA = 1, kB = 2, kZ = 4, kSpin = 8, kCap = 16, kCamLeft = 32, kCamRight = 64, kPlus = 128, kMinus = 256 };
    constexpr int kButtons = 9;
    constexpr int kTapFrames = 6;  // at least one KPAD read at 60 Hz (the smoke's own tap length)
    constexpr int kMaxSteps = 40000;

    struct Step {
        int frames;
        float x, y;
        unsigned held;
        unsigned tap;
    };

    Step gSteps[kMaxSteps];
    int gStepCount = 0;
    bool gRunning = false;
    char gName[64] = {};
    char gLogPath[1024] = {};
    std::FILE* gLog = nullptr;
    int gStep = 0;
    int gStepFrame = 0;
    long gFrame = 0;
    long gLimit = 0;
    unsigned gDown = 0;
    float gStickX = 0, gStickY = 0;

    const char* const kNames[kButtons] = {"A", "B", "Z", "Spin", "Cap", "CamLeft", "CamRight", "Plus", "Minus"};
    const Input::Action kActions[kButtons] = {Input::Action::A,        Input::Action::B,        Input::Action::NunchukZ,
                                              Input::Action::Shake,    Input::Action::ModCappyThrow, Input::Action::DpadLeft,
                                              Input::Action::DpadRight, Input::Action::Plus,      Input::Action::Minus};

    void pressAction(Input::Action action, bool down) {
        const Input::Bindings bindings = Input::bindings();
        const auto& inputs = bindings.inputs(action);
        if (inputs.empty()) {
            std::fprintf(stderr, "[test-server] input: no input is bound to %s\n", Input::actionName(action));
            return;
        }
        const Input::Binding& input = inputs.front();
        if (input.device == Input::Binding::Device::Key) {
            Input::keyEvent(input.code, down, false);
        } else {
            Input::mouseButtonEvent(static_cast< Input::MouseButton >(input.code), down);
        }
    }

    void setStick(float x, float y) {
        if (x == gStickX && y == gStickY) {
            return;
        }
        gStickX = x;
        gStickY = y;
        Input::padAxisEvent(Input::PadAxis::LeftX, x);
        Input::padAxisEvent(Input::PadAxis::LeftY, -y);  // SDL: y down
    }

    void setButtons(unsigned want) {
        for (int i = 0; i < kButtons; i++) {
            const unsigned bit = 1u << i;
            if ((want & bit) != (gDown & bit)) {
                pressAction(kActions[i], (want & bit) != 0);
            }
        }
        gDown = want;
    }

    void releaseAll() {
        setButtons(0);
        setStick(0, 0);
    }

    std::vector< std::string > words(const std::string& line) {
        std::istringstream in(line);
        std::vector< std::string > out;
        for (std::string w; in >> w;) {
            out.push_back(w);
        }
        return out;
    }

    // "A+Z!" -> held/tap masks; false on an unknown name.
    bool parseButtons(const std::string& text, unsigned* pHeld, unsigned* pTap) {
        *pHeld = *pTap = 0;
        if (text == "-" || text.empty()) {
            return true;
        }
        size_t at = 0;
        while (at <= text.size()) {
            size_t plus = text.find('+', at);
            std::string one = text.substr(at, plus == std::string::npos ? std::string::npos : plus - at);
            const bool tap = !one.empty() && one.back() == '!';
            if (tap) {
                one.pop_back();
            }
            int found = -1;
            for (int i = 0; i < kButtons; i++) {
                found = (strcasecmp(one.c_str(), kNames[i]) == 0) ? i : found;
            }
            if (found < 0) {
                return false;
            }
            (tap ? *pTap : *pHeld) |= 1u << found;
            if (plus == std::string::npos) {
                break;
            }
            at = plus + 1;
        }
        return true;
    }

    std::string marioReport(long frames) {
        char text[300];
        if (!MR::isExistSceneObj(SceneObj_MarioHolder)) {
            std::snprintf(text, sizeof(text), "ok frames=%ld no-player", frames);
            return text;
        }
        const TVec3f* p = MR::getPlayerPos();
        const TVec3f* v = MR::getPlayerVelocity();
        std::snprintf(text, sizeof(text), "ok frames=%ld mario=(%.1f,%.1f,%.1f) vel=(%.2f,%.2f,%.2f) ground=%d dead=%d", frames, p->x, p->y, p->z,
                      v->x, v->y, v->z, MR::isOnGroundPlayer() ? 1 : 0, MR::isPlayerDead() ? 1 : 0);
        return text;
    }

    void closeLog() {
        if (gLog != nullptr) {
            std::fclose(gLog);
            gLog = nullptr;
        }
    }

    void begin(const std::string& name, const char* logPath, long limit) {
        std::snprintf(gName, sizeof(gName), "%s", name.c_str());
        gStep = gStepFrame = 0;
        gFrame = 0;
        gLimit = limit;
        gDown = 0;
        gStickX = gStickY = 0;
        gRunning = true;
        Input::focusChanged(true);  // the smoke asserts focus before presses: the input layer ignores them while unfocused
        closeLog();
        if (logPath != nullptr && logPath[0] != '\0') {
            gLog = std::fopen(logPath, "w");
            if (gLog != nullptr) {
                std::fputs("frame,step,x,y,z,vx,vy,vz,ground\n", gLog);
            } else {
                std::fprintf(stderr, "[test-server] input: cannot write %s\n", logPath);
            }
        }
    }

    // CSV fields of one line, honouring the quoted stage name.
    std::vector< std::string > csvFields(const std::string& line) {
        std::vector< std::string > out;
        std::string cur;
        bool quoted = false;
        for (size_t i = 0; i < line.size(); i++) {
            const char c = line[i];
            if (quoted) {
                if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') {
                    cur.push_back('"');
                    i++;
                } else if (c == '"') {
                    quoted = false;
                } else {
                    cur.push_back(c);
                }
            } else if (c == '"') {
                quoted = true;
            } else if (c == ',') {
                out.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        out.push_back(cur);
        return out;
    }

    bool loadRoute(const char* path, long from, long count, std::string* pError) {
        std::FILE* f = std::fopen(path, "r");
        if (f == nullptr) {
            *pError = std::string("error cannot open ") + path;
            return false;
        }
        char buf[4096];
        int col[8];
        for (int& c : col) {
            c = -1;
        }
        static const char* const kCols[] = {"valid", "move_x", "move_y", "jump", "spin", "crouch", "camera_left", "camera_right"};
        if (std::fgets(buf, sizeof(buf), f) == nullptr) {
            std::fclose(f);
            *pError = "error empty route";
            return false;
        }
        const std::vector< std::string > header = csvFields(buf);
        for (size_t i = 0; i < header.size(); i++) {
            for (int k = 0; k < 8; k++) {
                col[k] = header[i] == kCols[k] ? static_cast< int >(i) : col[k];
            }
        }
        for (int k = 0; k < 8; k++) {
            if (col[k] < 0) {
                std::fclose(f);
                *pError = std::string("error route has no column ") + kCols[k];
                return false;
            }
        }
        gStepCount = 0;
        long row = 0;
        while (std::fgets(buf, sizeof(buf), f) != nullptr && gStepCount < kMaxSteps) {
            const std::vector< std::string > v = csvFields(buf);
            if (v.size() <= static_cast< size_t >(col[7]) || v[col[0]] != "1") {
                continue;  // loading or missing-player frame
            }
            if (row++ < from) {
                continue;
            }
            if (count > 0 && row - from > count) {
                break;
            }
            Step& s = gSteps[gStepCount++];
            s.frames = 1;
            s.x = static_cast< float >(std::atof(v[col[1]].c_str()));
            s.y = static_cast< float >(std::atof(v[col[2]].c_str()));
            s.held = (v[col[3]] == "1" ? kA : 0u) | (v[col[4]] == "1" ? kSpin : 0u) | (v[col[5]] == "1" ? kZ : 0u) |
                     (v[col[6]] == "1" ? kCamLeft : 0u) | (v[col[7]] == "1" ? kCamRight : 0u);
            s.tap = 0;
        }
        std::fclose(f);
        if (gStepCount == 0) {
            *pError = "error the route has no valid frames in range";
            return false;
        }
        return true;
    }
}  // namespace

namespace TestServerInput {
    Result start(const std::string& name, const std::string& line, bool playable, std::string* pReply) {
        const std::vector< std::string > w = words(line);
        if (w.empty() || (w[0] != "input" && w[0] != "route")) {
            return Result::Unhandled;
        }
        if (gRunning) {
            *pReply = "error an input plan is already running";
            return Result::Done;
        }
        if (!playable) {
            *pReply = "error not in normal play (retry)";
            return Result::Done;
        }
        const char* logPath = nullptr;
        char logCopy[1024];
        long limit = 0;
        std::string error;
        if (w[0] == "input") {
            gStepCount = 0;
            for (size_t i = 1; i < w.size(); i++) {
                if (w[i].compare(0, 4, "log=") == 0) {
                    std::snprintf(logCopy, sizeof(logCopy), "%s", w[i].c_str() + 4);
                    logPath = logCopy;
                    continue;
                }
                float x = 0, y = 0;
                int frames = 0;
                char buttons[64] = "-";
                if (std::sscanf(w[i].c_str(), "%d,%f,%f,%63s", &frames, &x, &y, buttons) < 3 || frames <= 0 || gStepCount >= kMaxSteps) {
                    *pReply = "error bad step '" + w[i] + "' (frames,x,y,buttons)";
                    return Result::Done;
                }
                Step& s = gSteps[gStepCount];
                if (!parseButtons(buttons, &s.held, &s.tap)) {
                    *pReply = std::string("error unknown button in '") + w[i] + "' (A B Z Spin Cap CamLeft CamRight Plus Minus)";
                    return Result::Done;
                }
                s.frames = frames;
                s.x = std::fmax(-1.0f, std::fmin(1.0f, x));
                s.y = std::fmax(-1.0f, std::fmin(1.0f, y));
                gStepCount++;
            }
            if (gStepCount == 0) {
                *pReply = "error no steps (input [log=file] frames,x,y,buttons ...)";
                return Result::Done;
            }
        } else {
            if (w.size() < 2) {
                *pReply = "error route <csv> [log=file] [from=n] [frames=n]";
                return Result::Done;
            }
            long from = 0, count = 0;
            for (size_t i = 2; i < w.size(); i++) {
                if (w[i].compare(0, 4, "log=") == 0) {
                    std::snprintf(logCopy, sizeof(logCopy), "%s", w[i].c_str() + 4);
                    logPath = logCopy;
                } else if (w[i].compare(0, 5, "from=") == 0) {
                    from = std::atol(w[i].c_str() + 5);
                } else if (w[i].compare(0, 7, "frames=") == 0) {
                    count = std::atol(w[i].c_str() + 7);
                }
            }
            if (!loadRoute(w[1].c_str(), from, count, &error)) {
                *pReply = error;
                return Result::Done;
            }
        }
        long total = 0;
        for (int i = 0; i < gStepCount; i++) {
            total += gSteps[i].frames;
        }
        begin(name, logPath, total);
        std::fprintf(stderr, "[test-server] %s: %d step(s), %ld frame(s)\n", w[0].c_str(), gStepCount, total);
        return Result::Pending;
    }

    bool pending() {
        return gRunning;
    }

    bool poll(std::string* pName, std::string* pReply) {
        if (!gRunning) {
            return false;
        }
        if (gStep < gStepCount) {
            const Step& s = gSteps[gStep];
            unsigned want = s.held;
            if (gStepFrame < kTapFrames) {
                want |= s.tap;
            }
            setStick(s.x, s.y);
            setButtons(want);
            if (gLog != nullptr && MR::isExistSceneObj(SceneObj_MarioHolder)) {
                const TVec3f* p = MR::getPlayerPos();
                const TVec3f* v = MR::getPlayerVelocity();
                std::fprintf(gLog, "%ld,%d,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%d\n", gFrame, gStep, p->x, p->y, p->z, v->x, v->y, v->z,
                             MR::isOnGroundPlayer() ? 1 : 0);
            }
            gFrame++;
            if (++gStepFrame >= s.frames) {
                gStep++;
                gStepFrame = 0;
            }
            return false;
        }
        releaseAll();
        closeLog();
        gRunning = false;
        *pName = gName;
        *pReply = marioReport(gFrame);
        return true;
    }
}  // namespace TestServerInput
#endif
