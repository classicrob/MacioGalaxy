#pragma once

#ifdef PETARI_NATIVE
#include <string>
#include <vector>

// The test server's frame-counted input, ONE engine for every caller (docs/dev/level-editor/TESTING.md). A plan is a list of steps;
// step i is applied for `frames` game frames, one frame per GameScene::update, through the same Input layer calls the smoke
// movement tasks use (the stick as the pad's left axis, analog; buttons through their bindings), so a run is frame-exact and
// independent of the OS. Everything here runs on the game thread.
//
// Two owners, one engine:
//   - the test-server text commands `input ...` / `route ...` (start/pending/poll below): the answer is a text reply under the
//     command's name when the plan ends;
//   - API-managed runs (startSteps/status/cancel): no reply; the caller polls status(). Commands (status, cancel, ...) keep
//     dispatching while a plan runs; only a second plan is refused.
// A plan ends early, releasing the stick and every button, when the stage or scenario changes, Mario dies or is lost, or a demo
// takes control (reason "stage-changed", "died", "player-lost", "control-lost"), or on cancel() ("cancelled").
namespace TestServerInput {
    // Button bits for Step::held / Step::tap.
    enum Button : unsigned {
        A = 1,
        B = 2,
        Z = 4,  // crouch / nunchuk Z
        Spin = 8,
        Cap = 16,  // Cappy throw
        CamLeft = 32,
        CamRight = 64,
        Plus = 128,
        Minus = 256,
    };

    struct Step {
        int frames = 1;        // game frames this step lasts (>= 1)
        float stickX = 0.0f;   // left stick, -1..1, x right
        float stickY = 0.0f;   // y UP (the pad axis is sent as SDL's y down)
        unsigned held = 0;     // Button bits held for the whole step
        unsigned tap = 0;      // Button bits pressed for the first 6 frames of the step only
    };

    struct Status {
        bool active = false;      // a plan is running (either owner)
        bool finished = false;    // the last plan ended (completed or not); false before the first one
        bool completed = false;   // ... and played every step
        long frame = 0;           // frames applied so far
        long total = 0;           // frames in the plan
        char reason[48] = {};     // why it ended: "done", "cancelled", "stage-changed", "died", "player-lost", "control-lost"
        bool hasPlayer = false;
        float pos[3] = {};        // Mario now
        float vel[3] = {};
        bool ground = false;
        bool dead = false;
        char stage[64] = {};
        int scenario = 0;
    };

    // Starts an API-managed plan. False and *pError: not started (a plan is running, not in normal play, no steps, bad step).
    bool startSteps(const std::vector< Step >& steps, std::string* pError, const char* pLogPath = nullptr);
    Status status();
    bool active();
    // Ends the running plan ("cancelled"), releasing stick and buttons. No-op when idle.
    void cancel();

    // ---- the test server's text commands and per-frame tick (TestServer.cpp) ----
    enum class Result { Unhandled, Done, Pending };
    // line: "input ..." / "route ...". Done: *pReply is the answer already (an error, usually).
    Result start(const std::string& name, const std::string& line, bool playable, std::string* pReply);
    // True while a plan of either owner runs.
    bool pending();
    // Applies this frame's step (every frame, both owners). True when a text-command plan ended: *pName / *pReply carry its answer.
    bool poll(std::string* pName, std::string* pReply);
}  // namespace TestServerInput
#endif
