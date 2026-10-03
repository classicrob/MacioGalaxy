#pragma once

#ifdef PETARI_NATIVE
#include <string>

// The test server's frame-counted input (docs/dev/level-editor/TESTING.md): `input` plays a list of steps
// {frames, stick, buttons}, `route` replays a PETARI_ROUTE_RECORD csv, both injected through the same Input layer
// calls the smoke movement tasks use (stick as the pad's left axis, buttons through their bindings), one step per
// game frame, so a run is frame-exact and independent of the OS. Dispatch mirrors editor agent commands:
// start() from TestServer's command switch, pending()/poll() each frame while a plan runs.
namespace TestServerInput {
    enum class Result { Unhandled, Done, Pending };
    // line: "input ..." / "route ...". Done: *pReply is the answer already (an error, usually).
    Result start(const std::string& name, const std::string& line, bool playable, std::string* pReply);
    bool pending();
    // Applies this frame's step. True when the plan finished: *pName / *pReply carry the answer.
    bool poll(std::string* pName, std::string* pReply);
}  // namespace TestServerInput
#endif
