// The level editor's sheets (docs/dev/level-editor/UX.md section 17): Save, as a modal over the editor.
// Save writes the session's edits as a mod through Runtime (Command::SaveMod): a patch by default (row edits
// against the player's disc, no game data, fine to share), or Full archives for other tools.
#include "panels.hpp"

#include <petari/editor/ui.hpp>

#include <petari/editor/edit_api.hpp>

#include "imgui.h"

#include <cctype>
#include <cstdio>
#include <string>

namespace PetariEditor::Ui {
namespace {

const ImVec4 kDim(0.50f, 0.66f, 0.79f, 1.0f);
const ImVec4 kWarn(1.0f, 0.72f, 0.35f, 1.0f);

bool gOpenSave = false;     // open the sheet next frame
std::string gModName;       // the session's mod (empty until the first save)
bool gFullArchives = false; // the session's format
char gNameField[64] = {};
bool gFullField = false;
bool gSubmit = false;       // the test driver's "Save" press

void save(const std::string& name, bool fullArchives) {
    Command command{Command::Kind::SaveMod};
    command.modName = name;
    command.fullArchives = fullArchives;
    enqueue(std::move(command));
    gModName = name;
    gFullArchives = fullArchives;
}

}  // namespace

std::string modNameFrom(const std::string& text) {
    // Folder-safe: letters, digits, spaces, '-' and '_'; trimmed; at most 40 characters.
    std::string name;
    for (char c : text) {
        const unsigned char u = static_cast< unsigned char >(c);
        if (std::isalnum(u) || c == ' ' || c == '-' || c == '_') name += c;
    }
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());
    while (!name.empty() && name.back() == ' ') name.pop_back();
    if (name.size() > 40) name.resize(40);
    return name;
}

std::string defaultModName(const char* galaxy) {
    // "CosmosGardenGalaxy" -> "CosmosGarden edit".
    std::string base = galaxy != nullptr ? galaxy : "";
    const std::string suffix = "Galaxy";
    if (base.size() > suffix.size() && base.compare(base.size() - suffix.size(), suffix.size(), suffix) == 0) base.resize(base.size() - suffix.size());
    return modNameFrom(base.empty() ? std::string("My edit") : base + " edit");
}

void requestSave(bool chooseAgain) {
    if (!chooseAgain && !gModName.empty()) {
        save(gModName, gFullArchives);  // Cmd+S after the first save: same mod, same format
        return;
    }
    gOpenSave = true;
}

const std::string& sessionModName() { return gModName; }

void submitSaveSheet() { gSubmit = true; }

void openSaveSheet() { requestSave(true); }

void drawSaveSheet() {
    const ModelView* model = PetariEditor::model();
    if (gOpenSave) {
        gOpenSave = false;
        const std::string name = !gModName.empty() ? gModName : defaultModName(model != nullptr ? model->galaxy() : nullptr);
        std::snprintf(gNameField, sizeof(gNameField), "%s", name.c_str());
        gFullField = gFullArchives;
        ImGui::OpenPopup("Save mod");
    }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f));
    if (!ImGui::BeginPopupModal("Save mod", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted("Name");
    ImGui::SameLine(90.0f);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputText("##name", gNameField, sizeof(gNameField));
    const std::string name = modNameFrom(gNameField);
    ImGui::Spacing();
    ImGui::TextUnformatted("Save as");
    ImGui::SameLine(90.0f);
    ImGui::BeginGroup();
    if (ImGui::RadioButton("Patch (recommended)", !gFullField)) gFullField = false;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 350.0f);
    ImGui::TextColored(kDim, "Your edits only, applied to the player's own game when it starts. No game files inside: fine to share.");
    ImGui::PopTextWrapPos();
    if (ImGui::RadioButton("Full archives (Whitehole, Dolphin)", gFullField)) gFullField = true;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 350.0f);
    ImGui::TextColored(gFullField ? kWarn : kDim, "The edited .arc files, for other tools. They contain the game's data: keep them to yourself.");
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::Spacing();
    // Where it goes (Runtime's layout: a patch per galaxy, or the archives under files/).
    const char* galaxy = model != nullptr ? model->galaxy() : "Galaxy";
    if (name.empty()) {
        ImGui::TextColored(kWarn, "Give the mod a name (letters, digits, spaces, - and _).");
    } else if (gFullField) {
        ImGui::TextColored(kDim, "mods/%s/files/StageData/%s/...  (on from the next start)", name.c_str(), galaxy);
    } else {
        ImGui::TextColored(kDim, "mods/%s/edits/%s.json  (on from the next start)", name.c_str(), galaxy);
    }
    ImGui::Separator();
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
    ImGui::BeginDisabled(name.empty());
    const bool clicked = ImGui::Button("Save", ImVec2(110.0f, 0.0f));
    ImGui::EndDisabled();
    if ((clicked || enter || gSubmit) && !name.empty()) {
        gSubmit = false;
        save(name, gFullField);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    ImGui::TextColored(kDim, "Cmd+S saves again");
    ImGui::EndPopup();
}

}  // namespace PetariEditor::Ui
