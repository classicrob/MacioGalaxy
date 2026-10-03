#include "galaxy_info.hpp"
#include "imgui.h"
#include "panels.hpp"
#include <algorithm>
#include <cstring>
#include <petari/editor/edit_api.hpp>

namespace PetariEditor::Ui {
    namespace {
        struct Draft {
            GalaxyInfo value, original;
            bool initialized = false, pending = false, restarting = false;
            std::uint64_t errorSerial = 0, noticeSerial = 0;
            std::string error;
        } gGalaxy;

        bool same(const GalaxyInfo& a, const GalaxyInfo& b) {
            return a.internalName == b.internalName && a.mod == b.mod && a.name == b.name && a.missions == b.missions && a.musicFrom == b.musicFrom;
        }

        void editText(const char* id, std::string& text) {
            std::vector< char > buffer(std::max< std::size_t >(4096, text.size() + 1024));
            std::memcpy(buffer.data(), text.c_str(), text.size() + 1);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText(id, buffer.data(), buffer.size()))
                text = buffer.data();
        }
    }  // namespace

    void drawGalaxyInfoPanel() {
        const auto current = modGalaxyInfo();
        if (!current) {
            gGalaxy = {};
            return;
        }
        if (!gGalaxy.initialized || gGalaxy.value.internalName != current->internalName || gGalaxy.value.mod != current->mod) {
            gGalaxy = {};
            gGalaxy.value = gGalaxy.original = *current;
            gGalaxy.initialized = true;
        }
        const auto& st = status();
        if (gGalaxy.pending && st.errorSerial != gGalaxy.errorSerial) {
            gGalaxy.pending = false;
            gGalaxy.error = st.lastError;
        }
        if (gGalaxy.pending && !gGalaxy.restarting && (same(*current, gGalaxy.value) || st.noticeSerial != gGalaxy.noticeSerial)) {
            gGalaxy.pending = false;
            gGalaxy.value = gGalaxy.original = *current;
        }
        if (!gGalaxy.pending && same(gGalaxy.value, gGalaxy.original))
            gGalaxy.value = gGalaxy.original = *current;
        auto& draft = gGalaxy.value;
        ImGui::PushID("galaxy-settings");
        ImGui::TextWrapped("%s", draft.internalName.c_str());
        ImGui::TextDisabled("Mod: %s", draft.mod.c_str());
        ImGui::BeginDisabled(gGalaxy.pending);
        drawSection("Galaxy name");
        editText("##galaxy-name", draft.name);
        noteTestTarget("field:Galaxy name");
        drawSection("Music");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##galaxy-music", draft.musicFrom.empty() ? "Default (Good Egg)" : draft.musicFrom.c_str())) {
            if (ImGui::Selectable("Default (Good Egg)", draft.musicFrom.empty()))
                draft.musicFrom.clear();
            for (const auto& name : newGalaxyChoices().galaxies)
                if (ImGui::Selectable(name.c_str(), draft.musicFrom == name))
                    draft.musicFrom = name;
            ImGui::EndCombo();
        }
        noteTestTarget("combo:Galaxy music");
        ImGui::TextWrapped("Music changes take effect on the next Play or reload.");
        drawSection("Missions", "1-8");
        for (std::size_t i = 0; i < draft.missions.size(); ++i) {
            ImGui::PushID(static_cast< int >(i));
            ImGui::Text("Mission %zu", i + 1);
            editText("##mission-name", draft.missions[i]);
            noteTestTarget(("field:Mission name " + std::to_string(i + 1)).c_str());
            ImGui::PopID();
        }
        ImGui::BeginDisabled(draft.missions.size() >= 8);
        if (ImGui::Button("Add mission"))
            draft.missions.push_back("Mission " + std::to_string(draft.missions.size() + 1));
        noteTestTarget("button:Add mission");
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(draft.missions.size() <= 1);
        if (ImGui::Button("Remove last"))
            draft.missions.pop_back();
        noteTestTarget("button:Remove mission");
        ImGui::EndDisabled();
        const bool restart = galaxyInfoNeedsRestart(draft);
        const bool dirty = !same(draft, gGalaxy.original);
        const std::string problem = checkGalaxyInfo(draft);
        auto send = [&](bool discard) {
            Command command{Command::Kind::SetGalaxyInfo};
            command.galaxyInfo = draft;
            command.galaxyInfo.discardUnsaved = discard;
            enqueue(std::move(command));
            gGalaxy.pending = true;
            gGalaxy.restarting = restart;
            gGalaxy.errorSerial = st.errorSerial;
            gGalaxy.noticeSerial = st.noticeSerial;
            gGalaxy.error.clear();
        };
        if (restart) {
            ImGui::TextWrapped("Changing the mission count restarts Petari into this galaxy in Edit.");
            if (draft.missions.size() < current->missions.size())
                ImGui::TextWrapped("The trailing missions will be removed. Objects used only by those missions stay in the layout but no longer "
                                   "appear. Adding the missions back shows them again.");
        }
        const bool unsaved = restart && st.unsaved;
        if (unsaved) {
            ImGui::TextWrapped("You have unsaved layout edits. Save them first, or discard them to apply these settings.");
            if (ImGui::Button("Save..."))
                requestSave(false);
            noteTestTarget("button:Save before galaxy settings");
            ImGui::SameLine();
            ImGui::BeginDisabled(!dirty || !problem.empty());
            if (ImGui::Button("Discard and apply"))
                send(true);
            noteTestTarget("button:Discard and apply galaxy settings");
            ImGui::EndDisabled();
        }
        ImGui::BeginDisabled(!dirty || !problem.empty() || unsaved);
        if (ImGui::Button(restart ? "Apply (restarts)" : "Apply"))
            send(false);
        noteTestTarget("button:Apply galaxy settings");
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!dirty);
        if (ImGui::Button("Cancel")) {
            gGalaxy.value = gGalaxy.original = *current;
            gGalaxy.error.clear();
        }
        noteTestTarget("button:Cancel galaxy settings");
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (!problem.empty()) {
            ImGui::TextWrapped("%s", problem.c_str());
            noteTestTarget("text:Galaxy settings validation");
        }
        if (!gGalaxy.error.empty())
            ImGui::TextWrapped("%s", gGalaxy.error.c_str());
        if (gGalaxy.pending)
            ImGui::TextWrapped("%s", gGalaxy.restarting ? "Updating galaxy... Petari will restart into Edit." : "Saving settings...");
        ImGui::PopID();
    }
}  // namespace PetariEditor::Ui
