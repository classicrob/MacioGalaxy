// Edit mode, UI side (docs/dev/level-editor/UX.md sections 3 and 5): the panels follow the game's Edit
// nerve, and mouse, trackpad and keys over the viewport drive the editor camera.
#include "edit_mode.hpp"

#include <petari/editor/edit_api.hpp>
#include <petari/editor/ui.hpp>
#include <petari/editor_settings.hpp>

#include "imgui.h"

#include <chrono>
#include <cmath>

namespace PetariEditor::Ui {
namespace {

namespace Settings = PetariNative::EditorSettings;

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

    // Keys, unless a text field has them.
    if (!io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !io.KeyCtrl && !io.KeySuper) Settings::frame();
        if (ImGui::IsKeyPressed(ImGuiKey_U, false) && !io.KeyCtrl && !io.KeySuper) Settings::setWorldUp(!Settings::worldUp());
    }
}

}  // namespace

void updateEditMode(float windowWidth, float windowHeight) {
    if (!gCallbacksSet) {
        gCallbacksSet = true;
        UiCallbacks callbacks;
        callbacks.leaveEdit = leaveEdit;
        callbacks.resumeEdit = resumeEdit;
        setUiCallbacks(callbacks);
        gForcedVisible = visible();
    }
    const bool active = Settings::active();
    if (active != gWasActive) {
        gWasActive = active;
        gDrag = Drag::None;
        if (!gForcedVisible) setVisible(active);
        if (active) {
            notifyEditEntered();
        } else {
            notifyEditLeft();
        }
    }
    if (!active) {
        return;
    }
    // Cmd+. : Focus mode (the game fills the window; panels away).
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Period)) {
        setFocusMode(!focusMode());
    }
    cameraInput(cameraArea(windowWidth, windowHeight));
}

}  // namespace PetariEditor::Ui
