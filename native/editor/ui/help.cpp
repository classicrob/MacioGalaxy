#include "help.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "panels.hpp"
#include <algorithm>
#include <petari/home_menu.hpp>

namespace PetariEditor::Ui {
    namespace {
        bool gHelpVisible = false;
        struct Shortcut {
            const char* keys;
            const char* action;
        };
        constexpr Shortcut shortcuts[] = {
            {"? (Shift+/)", "Open / close this guide"},
            {"Tab", "Edit / play from the marker"},
            {"Shift+Tab / Option+Tab", "Resume / play from mission start"},
            {"M", "Put the play marker on the ground under the cursor"},
            {"Right-drag", "Look around"},
            {"Right held + W A S D", "Fly forward / left / back / right"},
            {"Right held + Q / E", "Fly down / up; Shift flies faster"},
            {"Option-drag / middle-drag", "Orbit; add Shift to pan"},
            {"Mouse wheel / pinch", "Zoom"},
            {"Trackpad scroll", "Orbit; Shift pans; Option zooms"},
            {"Click / Shift-click / Cmd-click", "Select / add / toggle selection"},
            {"Q / W / E / R", "Select / move / rotate / scale"},
            {",", "Cycle gravity / local / world gizmo frame"},
            {"Cmd while dragging", "Temporarily suspend snapping"},
            {"End / Shift+End / Cmd+End", "Drop to ground / stand upright / both"},
            {"F", "Frame selection, or the whole open Array preview"},
            {"U / Cmd+.", "Toggle world-up camera / hide panels (Focus)"},
            {"Delete / Backspace", "Delete selection or selected path point/wire"},
            {"Cmd+D / Cmd+Shift+D", "Duplicate / arrange an Array"},
            {"Cmd+Z / Cmd+Shift+Z", "Undo / redo"},
            {"1 / 2 / 3 / 5", "Gravity / areas / cameras / paths and wires view"},
            {"Option-click a path", "Insert a path point"},
            {"Enter while laying a path", "Finish the new path"},
            {"Backspace while laying a path", "Remove the last draft point"},
            {"K", "Keep a supported object's played transform"},
            {"Cmd+R", "Reload the scene; apply pending edits"},
            {"Cmd+S / Cmd+Shift+S", "Save / Save as"},
            {"Cmd+Shift+E / Cmd+N", "Share / New galaxy"},
            {"Esc", "Close help; otherwise stop placing/cancel a tool/deselect"},
        };
    }  // namespace

    void toggleHelp() {
        gHelpVisible = !gHelpVisible;
    }

    bool helpVisible() {
        return gHelpVisible;
    }

    bool updateHelpInput() {
        const auto& io = ImGui::GetIO();
        if (io.WantTextInput || io.KeyCtrl || io.KeySuper || io.KeyAlt) {
            return false;
        }
        if (gHelpVisible && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            gHelpVisible = false;
            ImGui::SetKeyOwner(ImGuiKey_Escape, ImGui::GetID("editor-help"), ImGuiInputFlags_LockThisFrame);
            return true;
        }
        bool questionMark = io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Slash, false);
        for (ImWchar character : io.InputQueueCharacters) {
            questionMark = questionMark || character == '?';
        }
        if (questionMark) {
            toggleHelp();
        }
        return false;
    }

    void drawHelpOverlay() {
        if (!gHelpVisible || ImGui::GetTopMostPopupModal() != nullptr) {
            return;
        }
        const auto* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
        ImGui::SetNextWindowSize({std::min(780.f, viewport->Size.x - 24.f), std::min(700.f, viewport->Size.y - 40.f)}, ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.02f, 0.20f, 0.31f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_Border, {0.40f, 0.86f, 1.f, 0.85f});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16.f, 14.f});
        const bool open =
            ImGui::Begin("Keys and tips###editor-help", &gHelpVisible,
                         ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
        if (open) {
            auto* font = PetariNative::HomeMenu::galaxyTextFont();
            if (font) {
                ImGui::PushFont(font);
            }
            ImGui::TextUnformatted("Make a little change. Try it. Keep building.");
            if (font) {
                ImGui::PopFont();
            }
            ImGui::TextWrapped("? or Esc closes help. Drag this panel aside to use the camera behind it.");
            ImGui::Spacing();
            drawSection("Five useful tips");
            for (const auto* tip : {"Click a Browser card, then click the ground to place it. Esc stops placing.",
                                    "End drops your selection along gravity. F brings it back into view.",
                                    "Point at the ground, M, then Tab: play from there. Tab returns to Edit.",
                                    "Open Checks before saving: click a problem to find it; use a fix when offered.",
                                    "Cmd+S saves your mod (Patch is recommended). Share packs the saved version into a zip."}) {
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::TextWrapped("%s", tip);
            }
            ImGui::Spacing();
            drawSection("Shortcuts", "Mac keyboard");
            ImGui::TextWrapped("Except Tab and Save, shortcuts wait while you type. On a compact Mac keyboard, End is Fn+Right.");
            if (ImGui::BeginTable("help-shortcuts", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Key / gesture", ImGuiTableColumnFlags_WidthStretch, 0.43f);
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 0.57f);
                ImGui::TableHeadersRow();
                for (const auto& shortcut : shortcuts) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", shortcut.keys);
                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", shortcut.action);
                }
                ImGui::EndTable();
            }
            ImGui::TextWrapped("Full walkthrough: docs/EDITOR_GUIDE.md");
            if (ImGui::Button("Close help")) {
                gHelpVisible = false;
            }
            noteTestTarget("button:Close help");
        }
        ImGui::End();
    }
}  // namespace PetariEditor::Ui
