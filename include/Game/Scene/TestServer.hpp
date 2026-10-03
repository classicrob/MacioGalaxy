#pragma once

#ifdef PETARI_NATIVE
#include <revolution.h>

// Test server (PETARI_TEST_SERVER=<dir>, test-only, off when unset): one background-smoke app stays up and takes
// commands, so a worker's smokes reuse one process instead of paying a boot each (docs/dev/level-editor/TESTING.md).
// Commands are files <dir>/cmd/<name>.txt, run in name order on the game thread; the reply goes to
// <dir>/reply/<name>.txt. See native/tools/test_server.py for the client.
namespace TestServer {
    // GameScene::update, every frame; actionNerve: the scene is in normal play.
    void update(bool actionNerve);
    // The smoke harness should not end the run or fail on a scene change while this is on.
    bool active();
}  // namespace TestServer
#endif
