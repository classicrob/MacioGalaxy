#!/usr/bin/env python3
"""Aurora's ImGui glue, two Petari changes:
- prepare ImGui's WebGPU pipeline during renderer initialization, before OS startup;
- the pointer as SDL's mouse events place it (EP-001, the editor's panels ignoring clicks). ImGui's SDL
  backend takes the position only from motion events and, when focused, a per-frame poll of the OS cursor;
  a pointer that only clicks (automation tools, remote desktops, tablets) sends neither, so clicks landed
  where the mouse last moved. Here the last event position overrides the poll, and a click at a new place
  moves the pointer first and is held one frame (ImGui presses overlapping items - tabs, selectables -
  only if they were hovered the frame before)."""
from pathlib import Path
import sys


def patch(text):
    anchor = '    ImGui_ImplWGPU_Init(&info);'
    if text.count(anchor) != 1:
        raise ValueError('Aurora ImGui pipeline prewarm anchor mismatch')
    replacement = '''    ImGui_ImplWGPU_Init(&info);
    // NewFrame otherwise compiles this pipeline lazily on the first game frame.
    const auto petariImGuiStart = std::chrono::steady_clock::now();
    const bool petariImGuiReady = ImGui_ImplWGPU_CreateDeviceObjects();
    const auto petariImGuiMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - petariImGuiStart).count();
    std::fprintf(stderr, "[gx pipeline prewarm] kind=imgui ready=%d build_ms=%.3f\\n",
                 petariImGuiReady ? 1 : 0, petariImGuiMs);'''
    text = text.replace(anchor, replacement)
    # EP-001: the pointer as SDL's mouse events deliver it (see the module docstring).
    click = '  ImGui_ImplSDL3_ProcessEvent(&renderEvent);\n}'
    if text.count(click) != 1:
        raise ValueError('Aurora ImGui click-position anchor mismatch')
    text = text.replace(click, """  // Petari (EP-001): the pointer as SDL's mouse events place it. A button at a new place moves the pointer
  // there first and is held one frame, as a real mouse's motion would make it (ImGui needs an item hovered
  // the frame before it is pressed for overlapping items: tabs, selectables).
  if (petari_mouse::observe(renderEvent)) {
    return;
  }
  ImGui_ImplSDL3_ProcessEvent(&renderEvent);
}""")
    helpers = """namespace petari_mouse {
// The last position an SDL mouse event carried (window points) and whether it still applies: the backend's
// per-frame poll of the OS cursor is overridden by it, since a cursor that only clicks (automation, remote
// desktops, tablets) never moves the OS cursor or sends motion. A mouse-leave hands back to the poll.
bool g_override = false;
ImVec2 g_position;
std::vector<SDL_Event> g_held;   // buttons held to the next frame (this poll's)
std::vector<SDL_Event> g_ready;  // last poll's, replayed at the next frame's start
bool g_holding = false;           // this poll is holding mouse events (keeps their order)

// True if the event was held for the next frame.
bool observe(const SDL_Event& event) {
  if (ImGui::GetCurrentContext() == nullptr) {
    return false;
  }
  ImVec2 at;
  switch (event.type) {
  case SDL_EVENT_MOUSE_MOTION: at = {event.motion.x, event.motion.y}; break;
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
  case SDL_EVENT_MOUSE_BUTTON_UP: at = {event.button.x, event.button.y}; break;
  case SDL_EVENT_MOUSE_WHEEL: at = {event.wheel.mouse_x, event.wheel.mouse_y}; break;
  case SDL_EVENT_WINDOW_MOUSE_LEAVE: g_override = false; return false;
  default: return false;
  }
  const bool moved = !g_override || std::fabs(at.x - g_position.x) > 0.5f || std::fabs(at.y - g_position.y) > 0.5f;
  g_override = true;
  g_position = at;
  const bool button = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP;
  if (button && moved) {
    g_holding = true;  // later mouse events of this poll wait with it, in order
  }
  if (g_holding && event.type != SDL_EVENT_MOUSE_MOTION) {
    ImGui::GetIO().AddMousePosEvent(at.x, at.y);
    g_held.push_back(event);
    return true;
  }
  return false;
}

// At the next frame, after the backend's poll: the event position wins, then last poll's held buttons.
void frame() {
  if (ImGui::GetCurrentContext() == nullptr) {
    return;
  }
  if (g_override) {
    ImGui::GetIO().AddMousePosEvent(g_position.x, g_position.y);
  }
  for (SDL_Event& event : g_ready) {
    ImGui_ImplSDL3_ProcessEvent(&event);
  }
  g_ready.swap(g_held);
  g_held.clear();
  g_holding = false;
}
} // namespace petari_mouse

"""
    anchor_ns = 'void process_event(const SDL_Event& event) noexcept {'
    if text.count(anchor_ns) != 1:
        raise ValueError('Aurora ImGui process_event anchor mismatch')
    text = text.replace(anchor_ns, helpers + anchor_ns)
    newframe = '  ImGui_ImplSDL3_NewFrame();\n'
    if text.count(newframe) != 1:
        raise ValueError('Aurora ImGui NewFrame anchor mismatch')
    text = text.replace(newframe, newframe + '  petari_mouse::frame();  // EP-001\n')
    return '#include <chrono>\n#include <cmath>\n#include <cstdio>\n#include <vector>\n' + text


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('usage: patch_aurora_pipeline_imgui.py SOURCE OUTPUT')
    source, output = map(Path, sys.argv[1:])
    try:
        from patch_aurora_imgui_texture import patch as texture_patch
        result = texture_patch(patch(source.read_text()))
    except ValueError as error:
        raise SystemExit(str(error))
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text() != result:
        output.write_text(result)
