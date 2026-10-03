#pragma once

namespace PetariNative {
/// Smoke scripts must finish their stage checks before the server can change the scene.
/// A normal private save has no smoke script and never sets that flag.
constexpr bool testServerReady(bool enabled, const char* smoke, bool stageChecksDone) {
    return enabled && ((!smoke || !smoke[0]) || stageChecksDone);
}
}  // namespace PetariNative
