#!/usr/bin/env python3
"""patch_imgui_sdl3_monitors.py: NewFrame rebuilds the monitor list only after display changes.

usage: imgui_sdl3_monitors_tests.py BACKEND_SOURCE (imgui_impl_sdl3.cpp)
"""
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'native/gx'))
import patch_imgui_sdl3_monitors  # noqa: E402

original = Path(sys.argv[1]).read_text()
text = patch_imgui_sdl3_monitors.patch(original)


def body(signature):
    start = text.index(signature)
    depth = 0
    for i in range(text.index('{', start), len(text)):
        depth += {'{': 1, '}': -1}.get(text[i], 0)
        if depth == 0:
            return text[start:i + 1]
    raise AssertionError(signature)


new_frame = body('void ImGui_ImplSDL3_NewFrame()')
init = body('static bool ImGui_ImplSDL3_Init(')
shutdown = body('void ImGui_ImplSDL3_Shutdown()')
watch = body('static bool SDLCALL ImGui_ImplSDL3_PetariMonitorWatch(')
calls = re.findall(r'^(.*)ImGui_ImplSDL3_UpdateMonitors\(\);', new_frame, re.M)
assert len(calls) == 1, calls
assert re.search(r'if \(g_petariMonitorsStale\.exchange\(false, std::memory_order_acq_rel\)\)\n\s+ImGui_ImplSDL3_UpdateMonitors\(\);', new_frame)
# Init: clear, start watching, then build once (a change during the build marks it stale again).
assert init.index('g_petariMonitorsStale.store(false') < init.index('SDL_AddEventWatch(ImGui_ImplSDL3_PetariMonitorWatch') \
    < init.index('ImGui_ImplSDL3_UpdateMonitors();')
assert 'SDL_RemoveEventWatch(ImGui_ImplSDL3_PetariMonitorWatch, nullptr);' in shutdown
for event in ('SDL_EVENT_DISPLAY_FIRST', 'SDL_EVENT_DISPLAY_LAST', 'SDL_EVENT_WINDOW_DISPLAY_CHANGED',
              'SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED'):
    assert event in watch, event
assert 'return true;' in watch  # a watch never filters events
# Nothing else changed: removing the patch's additions gives back the original calls.
assert original.count('ImGui_ImplSDL3_UpdateMonitors();') == text.count('ImGui_ImplSDL3_UpdateMonitors();')
print('ImGui SDL3 monitors: rebuilt at init and after display events only; events pass through')
