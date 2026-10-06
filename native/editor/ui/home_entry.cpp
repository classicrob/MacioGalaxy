#include "home_entry.hpp"
#include "imgui.h"
#include "panels.hpp"
#include <petari/editor/edit_api.hpp>
#include <petari/editor_settings.hpp>
#include <petari/player_launch.hpp>

namespace PetariEditor::Ui {
    void serviceHomeNewGalaxy() {
        namespace Settings = PetariNative::EditorSettings;
        static bool waiting = false;
        static double since = 0;
        if (PetariNative::PlayerLaunch::newGalaxyPending.exchange(false) && Settings::enabled()) {
            waiting = true;
            since = ImGui::GetTime();
        }
        if (waiting) {
            if (!Settings::enabled())
                waiting = false;
            else if (Settings::active()) {
                if (status().journalEdits == 0) {
                    openNewGalaxySheet();
                    waiting = false;
                }
            } else if (ImGui::GetTime() - since < 3) {
                Settings::requestEnter();
            } else {
                waiting = false;
                ImGui::OpenPopup("New galaxy unavailable");
            }
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(.5f, .5f));
        ImGui::SetNextWindowSize(ImVec2(460, 0));
        if (ImGui::BeginPopupModal("New galaxy unavailable", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize)) {
            ImGui::TextWrapped(
                "The editor cannot open in this scene yet. Wait until Mario can move, then choose New galaxy again from Home > Mods > Level Select.");
            if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape))
                ImGui::CloseCurrentPopup();
            noteTestTarget("button:Close new galaxy unavailable");
            ImGui::EndPopup();
        }
    }
}  // namespace PetariEditor::Ui
