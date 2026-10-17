#!/usr/bin/env python3
"""ImGui's SDL3 backend (1.91.9b): rebuild the monitor list on display changes, not every frame.

ImGui_ImplSDL3_NewFrame calls ImGui_ImplSDL3_UpdateMonitors unconditionally. On macOS each
SDL_GetDisplayBounds/UsableBounds/ContentScale is a synchronous WindowServer round trip
(SkyLight mach_msg), on the game thread inside aurora_begin_frame: about 1 ms a frame on
average, several ms when WindowServer is busy (the seam_begin_frame tail in frames.csv).
Upstream later moved to the same scheme (update on display events). An SDL event watch,
which sees every event independent of how the app filters or forwards them, marks the list
stale on any display event and on window display/scale changes; the next NewFrame rebuilds
it, exactly as before. Init still builds it once.

usage: patch_imgui_sdl3_monitors.py SOURCE OUTPUT
"""
from pathlib import Path
import sys


def patch(text):
    def replace(old, new):
        nonlocal text
        if text.count(old) != 1:
            raise ValueError(f'ImGui SDL3 monitor patch anchor mismatch: {old}')
        text = text.replace(old, new)

    replace('#include <SDL3/SDL.h>\n', '''#include <SDL3/SDL.h>
#include <atomic>

// Petari (native/gx/patch_imgui_sdl3_monitors.py): the monitor list is stale after a display
// change; set from an SDL event watch (any thread that pushes events), read by NewFrame.
static std::atomic<bool> g_petariMonitorsStale{true};
static bool SDLCALL ImGui_ImplSDL3_PetariMonitorWatch(void*, SDL_Event* event)
{
    if ((event->type >= SDL_EVENT_DISPLAY_FIRST && event->type <= SDL_EVENT_DISPLAY_LAST) ||
        event->type == SDL_EVENT_WINDOW_DISPLAY_CHANGED || event->type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED)
        g_petariMonitorsStale.store(true, std::memory_order_release);
    return true;
}
''')
    replace('''    // Update monitor a first time during init
    ImGui_ImplSDL3_UpdateMonitors();
''', '''    // Update monitor a first time during init (Petari: watching for changes from here on)
    g_petariMonitorsStale.store(false, std::memory_order_release);
    SDL_AddEventWatch(ImGui_ImplSDL3_PetariMonitorWatch, nullptr);
    ImGui_ImplSDL3_UpdateMonitors();
''')
    replace('''    ImGui_ImplSDL3_ShutdownMultiViewportSupport();

    if (bd->ClipboardTextData)''', '''    ImGui_ImplSDL3_ShutdownMultiViewportSupport();
    SDL_RemoveEventWatch(ImGui_ImplSDL3_PetariMonitorWatch, nullptr);
    g_petariMonitorsStale.store(true, std::memory_order_release);

    if (bd->ClipboardTextData)''')
    replace('''    // Update monitors
    ImGui_ImplSDL3_UpdateMonitors();
''', '''    // Update monitors (Petari: only after a display change; see the event watch)
    if (g_petariMonitorsStale.exchange(false, std::memory_order_acq_rel))
        ImGui_ImplSDL3_UpdateMonitors();
''')
    return text


def main():
    if len(sys.argv) != 3:
        raise SystemExit('usage: patch_imgui_sdl3_monitors.py SOURCE OUTPUT')
    source, output = map(Path, sys.argv[1:])
    try:
        text = patch(source.read_text())
    except ValueError as error:
        raise SystemExit(str(error)) from error
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text() != text:
        output.write_text(text)


if __name__ == '__main__':
    main()
