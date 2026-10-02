// Edit mode, UI side (docs/dev/level-editor/UX.md sections 3 and 5): the panels follow the game's Edit
// nerve, and mouse, trackpad and keys over the viewport drive the editor camera.
#include "edit_mode.hpp"

#include <petari/editor/edit_api.hpp>
#include <petari/editor/ui.hpp>
#include <petari/editor_pick.hpp>
#include <petari/editor_settings.hpp>

#include "imgui.h"

#include <chrono>
#include <cmath>
#include <vector>

namespace PetariEditor::Ui {
namespace {

namespace Settings = PetariNative::EditorSettings;
namespace Pick = PetariNative::EditorPick;

constexpr float kLookDegreesPerPoint = 0.15f;   // right-drag (fly), as photo mode
constexpr float kOrbitDegreesPerPoint = 0.30f;  // middle- or Option-drag
constexpr float kScrollOrbitDegrees = 2.0f;     // per trackpad scroll unit
constexpr float kPreciseZoomSteps = 0.25f;      // per trackpad scroll unit with Option
constexpr auto kGestureGap = std::chrono::milliseconds(250);  // a new scroll gesture picks a new pivot

enum class Drag { None, Look, Orbit, Pan };

bool gWasActive = false;
bool gCallbacksSet = false;
bool gForcedVisible = false;  // shown by PETARI_EDITOR_DOCK before Edit mode existed
Drag gDrag = Drag::None;
std::chrono::steady_clock::time_point gLastScroll{};

void leaveEdit() {
    Settings::requestLeave();
}
void resumeEdit() {
    Settings::requestEnter();
}
// A rebuilt scene: every actor is new. The selection's actors are dropped (rows come back with
// Runtime's binding).
void sceneRebuilt() {
    Pick::clearSelection();
}

struct Area {
    float x, y, w, h;
    bool contains(ImVec2 p) const { return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h; }
};

Area cameraArea(float windowWidth, float windowHeight) {
    Area area{0.0f, 0.0f, windowWidth, windowHeight};
    viewportRect(&area.x, &area.y, &area.w, &area.h);  // the docked viewport, or the whole window (Focus)
    return area;
}

void cameraInput(const Area& area) {
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    const bool inside = area.contains(mouse) && !io.WantCaptureMouse;
    if (inside && area.w > 0.0f && area.h > 0.0f) {
        Settings::cursor((mouse.x - area.x) / area.w * 2.0f - 1.0f, 1.0f - (mouse.y - area.y) / area.h * 2.0f);
    }

    // Drags start over the viewport and keep going wherever the pointer goes.
    if (gDrag == Drag::None && inside) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            gDrag = Drag::Look;
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && io.KeyAlt)) {
            gDrag = io.KeyShift ? Drag::Pan : Drag::Orbit;
            if (gDrag == Drag::Orbit) Settings::repivot();
        }
    }
    const bool held = (gDrag == Drag::Look && ImGui::IsMouseDown(ImGuiMouseButton_Right)) ||
                      ((gDrag == Drag::Orbit || gDrag == Drag::Pan) &&
                       (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || ImGui::IsMouseDown(ImGuiMouseButton_Left)));
    if (!held) gDrag = Drag::None;
    const ImVec2 delta = io.MouseDelta;
    switch (gDrag) {
    case Drag::Look: Settings::look(delta.x * kLookDegreesPerPoint, -delta.y * kLookDegreesPerPoint); break;
    case Drag::Orbit: Settings::orbit(delta.x * kOrbitDegreesPerPoint, -delta.y * kOrbitDegreesPerPoint); break;
    case Drag::Pan: Settings::pan(delta.x / area.h, -delta.y / area.h); break;
    case Drag::None: break;
    }

    // Fly with the right button held: W A S D, Q down, E up; Shift 4x (UX section 5).
    if (gDrag == Drag::Look) {
        auto axis = [](ImGuiKey plus, ImGuiKey minus) {
            return (ImGui::IsKeyDown(plus) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(minus) ? 1.0f : 0.0f);
        };
        Settings::move(axis(ImGuiKey_D, ImGuiKey_A), axis(ImGuiKey_W, ImGuiKey_S), axis(ImGuiKey_E, ImGuiKey_Q));
        Settings::speed(io.KeyShift ? 4.0f : 1.0f);
    } else {
        Settings::move(0.0f, 0.0f, 0.0f);
    }

    // Scrolling over the viewport. Trackpads (and the Magic Mouse) scroll precisely: orbit, with Shift
    // pan, with Option zoom. Wheel clicks arrive as whole notches: zoom. Pinch zooms (wpad_host.cpp).
    const float wheelX = io.MouseWheelH, wheelY = io.MouseWheel;
    if (inside && (wheelX != 0.0f || wheelY != 0.0f)) {
        const bool precise = wheelX != 0.0f || wheelY != std::round(wheelY);
        const auto now = std::chrono::steady_clock::now();
        const bool newGesture = now - gLastScroll > kGestureGap;
        gLastScroll = now;
        if (!precise || io.KeyAlt) {
            Settings::dolly(precise ? wheelY * kPreciseZoomSteps : wheelY);
        } else if (io.KeyShift) {
            Settings::pan(-wheelX * 0.01f, wheelY * 0.01f);
        } else {
            if (newGesture) Settings::repivot();
            Settings::orbit(-wheelX * kScrollOrbitDegrees, wheelY * kScrollOrbitDegrees);
        }
    }

    // Picking: hover while the pointer rests over the viewport; a click (not an Option-drag) selects.
    // Shift adds, Cmd toggles (Figma, UX section 6). Esc deselects.
    // (Set on changes only, so a test driver's hover is not undone by a pointer outside the window.)
    static bool sHover = false;
    const bool hover = inside && gDrag == Drag::None;
    if (hover != sHover) {
        sHover = hover;
        Pick::setHoverWanted(hover);
    }
    if (inside && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !io.KeyAlt &&
        ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).x * ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).x +
                ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).y * ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).y <
            16.0f) {
        Pick::click(io.KeyShift ? Pick::Click::Add : (io.KeyCtrl || io.KeySuper) ? Pick::Click::Toggle : Pick::Click::Replace);
    }
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) Pick::clearSelection();

    // Keys, unless a text field has them.
    if (!io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !io.KeyCtrl && !io.KeySuper) Settings::frame();
        if (ImGui::IsKeyPressed(ImGuiKey_U, false) && !io.KeyCtrl && !io.KeySuper) Settings::setWorldUp(!Settings::worldUp());
    }
}

ImU32 rgba(float r, float g, float b, float a) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
}

// Until the selection outline draws in the scene (S5): rings around the hovered and selected objects'
// bounds, in the selection's gold and the hover's cyan (UX section 4 "The look"), with their names.
void drawPickMarks(const Area& area) {
    ImDrawList* list = ImGui::GetForegroundDrawList();
    list->PushClipRect(ImVec2(area.x, area.y), ImVec2(area.x + area.w, area.y + area.h), true);
    auto toScreen = [&](const Pick::Item& item, ImVec2* center, float* radius) {
        center->x = area.x + (item.screenX * 0.5f + 0.5f) * area.w;
        center->y = area.y + (0.5f - item.screenY * 0.5f) * area.h;
        *radius = std::fmax(item.screenRadius * area.h, 10.0f);
        return item.onScreen;
    };
    auto label = [&](ImVec2 at, const char* text, ImU32 border) {
        const ImVec2 size = ImGui::CalcTextSize(text);
        const ImVec2 min(at.x - size.x * 0.5f - 8.0f, at.y), max(at.x + size.x * 0.5f + 8.0f, at.y + size.y + 6.0f);
        list->AddRectFilled(min, max, rgba(0.01f, 0.08f, 0.14f, 0.86f), (max.y - min.y) * 0.5f);
        list->AddRect(min, max, border, (max.y - min.y) * 0.5f, 0, 1.5f);
        list->AddText(ImVec2(min.x + 8.0f, min.y + 3.0f), rgba(1, 1, 1, 1), text);
    };
    const std::vector<Pick::Item> selection = Pick::selection();
    for (const Pick::Item& item : selection) {
        ImVec2 c;
        float r;
        if (!toScreen(item, &c, &r)) continue;
        list->AddCircle(c, r + 3.0f, rgba(0.98f, 0.80f, 0.22f, 0.35f), 0, 7.0f);
        list->AddCircle(c, r, rgba(0.98f, 0.80f, 0.22f, 1.0f), 0, 2.5f);
        label(ImVec2(c.x, c.y + r + 6.0f), item.name, rgba(0.98f, 0.80f, 0.22f, 1.0f));
    }
    Pick::Item hover;
    if (Pick::hover(&hover)) {
        bool selected = false;
        for (const Pick::Item& item : selection) selected = selected || item.actor == hover.actor;
        ImVec2 c;
        float r;
        if (!selected && toScreen(hover, &c, &r)) {
            list->AddCircle(c, r, rgba(0.40f, 0.86f, 1.0f, 0.95f), 0, 2.0f);
            label(ImVec2(c.x, c.y - r - ImGui::GetTextLineHeight() - 12.0f), hover.name, rgba(0.40f, 0.86f, 1.0f, 1.0f));
        }
    }
    list->PopClipRect();
}

}  // namespace

void updateEditMode(float windowWidth, float windowHeight) {
    if (!gCallbacksSet) {
        gCallbacksSet = true;
        UiCallbacks callbacks;
        callbacks.leaveEdit = leaveEdit;
        callbacks.resumeEdit = resumeEdit;
        callbacks.sceneRebuilt = sceneRebuilt;
        setUiCallbacks(callbacks);
        gForcedVisible = visible();
    }
    const bool active = Settings::active();
    if (active != gWasActive) {
        gWasActive = active;
        gDrag = Drag::None;
        if (!gForcedVisible) setVisible(active);
        if (!active) Pick::setHoverWanted(false);  // (Runtime is told by the game's Edit nerve)
    }
    if (!active) {
        return;
    }
    // Cmd+. : Focus mode (the game fills the window; panels away).
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Period)) {
        setFocusMode(!focusMode());
    }
    const Area area = cameraArea(windowWidth, windowHeight);
    cameraInput(area);
    drawPickMarks(area);
}

}  // namespace PetariEditor::Ui
