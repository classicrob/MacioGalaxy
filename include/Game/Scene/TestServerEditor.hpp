#pragma once

#ifdef PETARI_NATIVE
#include <functional>
#include <string>
#include <vector>

// The test server's live-editing commands (src/Game/Scene/TestServerEditor.cpp; docs/dev/level-editor/AGENT_API_X.md):
// editor-begin, editor-state, editor-apply <op.json>, editor-undo/redo [n], editor-reload, editor-play [x y z [up] [front]],
// editor-save <mod> [--full], editor-checks, mario, warp x y z, ground/gravity x y z [dx dy dz], screenshot <png>, wait <frames>.
namespace TestServerEditor {
    using Reply = std::function< void(const std::string& name, const std::string& text) >;
    // A command (words[0] is its name): false when it is not one of these. Some reply later, from tick().
    bool run(const std::string& name, const std::vector< std::string >& words, bool playable, const Reply& reply);
    // Every frame: finishes a pending command. True while one is pending (the server takes no new command).
    bool tick(bool playable, const Reply& reply);
}  // namespace TestServerEditor
#endif
