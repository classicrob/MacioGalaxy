// The level editor's sheets (docs/dev/level-editor/UX.md section 17): Save, as a modal over the editor.
// Save writes the session's edits as a mod through Runtime (Command::SaveMod): a patch by default (row edits
// against the player's disc, no game data, fine to share), or Full archives for other tools.
#include "choice_labels.hpp"
#include "host.hpp"
#include "panels.hpp"

#include <petari/editor/ui.hpp>

#include <petari/editor/edit_api.hpp>
#include <petari/editor/share.hpp>
#include <petari/editor/catalog.hpp>

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <vector>
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
bool gSubmitShare = false;  // the test driver's "Export" press
std::string gSettingsFile;  // <user>/editor/ui-settings.txt: the last mod per galaxy (Save sheet defaults)

// "mod.<galaxy>=<name>|<0 patch, 1 full>" lines.
std::string readSetting(const std::string& key) {
    std::ifstream in(gSettingsFile);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind(key + "=", 0) == 0) return line.substr(key.size() + 1);
    }
    return "";
}

void writeSetting(const std::string& key, const std::string& value) {
    if (gSettingsFile.empty()) return;
    std::vector< std::string > lines;
    {
        std::ifstream in(gSettingsFile);
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind(key + "=", 0) != 0 && !line.empty()) lines.push_back(line);
        }
    }
    lines.push_back(key + "=" + value);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(gSettingsFile).parent_path(), ec);
    std::ofstream out(gSettingsFile, std::ios::trunc);
    for (const std::string& line : lines) out << line << "\n";
}

void save(const std::string& name, bool fullArchives) {
    Command command{Command::Kind::SaveMod};
    command.modName = name;
    command.fullArchives = fullArchives;
    enqueue(std::move(command));
    gModName = name;
    gFullArchives = fullArchives;
    if (const ModelView* model = PetariEditor::model()) {
        writeSetting(std::string("mod.") + model->galaxy(), name + "|" + (fullArchives ? "1" : "0"));
    }
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

void setSettingsFile(const char* path) { gSettingsFile = path != nullptr ? path : ""; }

void drawSaveSheet() {
    const ModelView* model = PetariEditor::model();
    if (gOpenSave) {
        gOpenSave = false;
        // The session's mod, else the mod this galaxy was loaded from (friction #11: re-saving a course must
        // update it, not start a second one), else the last one saved for this galaxy, else one named after it.
        std::string name = gModName;
        bool full = gFullArchives;
        if (name.empty()) name = status().loadedMod;
        if (name.empty() && model != nullptr) {
            const std::string remembered = readSetting(std::string("mod.") + model->galaxy());
            const size_t bar = remembered.rfind('|');
            if (!remembered.empty()) {
                name = remembered.substr(0, bar);
                full = bar != std::string::npos && remembered.substr(bar + 1) == "1";
            }
        }
        if (name.empty()) name = defaultModName(model != nullptr ? model->galaxy() : nullptr);
        std::snprintf(gNameField, sizeof(gNameField), "%s", name.c_str());
        gFullField = full;
        ImGui::OpenPopup("Save mod");
    }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f));
    // Opaque, like the Home menu's panels (nothing of the scene behind the text), with a cyan rim; the scene
    // around it dimmed.
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.02f, 0.20f, 0.31f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.40f, 0.86f, 1.0f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.06f, 0.12f, 0.72f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
    const bool open = ImGui::BeginPopupModal("Save mod", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);
    if (!open) return;
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
    const bool cancel = ImGui::Button("Cancel", ImVec2(110.0f, 0.0f));
    noteTestTarget("button:Cancel");
    if (cancel || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        std::fputs("[editor] save sheet cancelled\n", stderr);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    ImGui::TextColored(kDim, "Cmd+S saves again");
    ImGui::EndPopup();
}

std::string bumpVersion(const std::string& version) {
    // "1.0" -> "1.1", "2.9" -> "2.10", "" -> "1.0"; anything else gets ".1" added.
    if (version.empty()) return "1.0";
    const size_t dot = version.rfind('.');
    const std::string tail = dot == std::string::npos ? version : version.substr(dot + 1);
    if (tail.empty() || tail.find_first_not_of("0123456789") != std::string::npos) return version + ".1";
    return (dot == std::string::npos ? std::string() : version.substr(0, dot + 1)) + std::to_string(std::stoi(tail) + 1);
}

std::string zipFileName(const std::string& title, const std::string& version) {
    std::string name = modNameFrom(title);
    for (char& c : name) {
        if (c == ' ') c = '-';
    }
    return (name.empty() ? std::string("mod") : name) + "-" + (version.empty() ? std::string("1.0") : version) + ".zip";
}

// ---- Share (UX section 17): the session's mod as a zip, with its name, author, version and description ----

namespace {
bool gOpenShare = false;
struct ShareFields {
    char title[64] = {}, author[64] = {}, version[16] = {}, description[256] = {}, folder[512] = {};
    Share::Preview preview;  // what the mod folder holds now
    std::string modName;
    std::string result;      // the last export: the zip's path, or the error
    std::string exported;    // the zip written
    bool ok = false;
    bool panel = false;      // a native save panel is up
} gShare;

// The save panel's answer (it may come back on another thread).
std::mutex gDialogMutex;
bool gDialogAnswered = false;
std::string gDialogPath;
void dialogDone(void*, const char* path) {
    std::lock_guard< std::mutex > lock(gDialogMutex);
    gDialogAnswered = true;
    gDialogPath = path != nullptr ? path : "";
}

void exportTo(const std::string& zipPath) {
    Share::Info info;
    info.title = gShare.title;
    info.author = gShare.author;
    info.version = gShare.version;
    info.description = gShare.description;
    std::string error;
    gShare.ok = shareMod(gShare.modName, info, zipPath, &error);
    gShare.result = gShare.ok ? "Exported " + zipPath : "Couldn't export: " + error;
    gShare.exported = gShare.ok ? zipPath : "";
    std::fprintf(stderr, "[editor] share: %s\n", gShare.result.c_str());
}

std::filesystem::path userDirectory() {
    // <user>/editor/ui-settings.txt -> <user>
    return gSettingsFile.empty() ? std::filesystem::path() : std::filesystem::path(gSettingsFile).parent_path().parent_path();
}
}  // namespace

void openShareSheet() { gOpenShare = true; }

void drawShareSheet() {
    const ModelView* model = PetariEditor::model();
    if (gOpenShare) {
        gOpenShare = false;
        gShare = ShareFields{};
        gShare.modName = gModName;
        if (gShare.modName.empty()) gShare.modName = status().loadedMod;
        if (gShare.modName.empty() && model != nullptr) {
            const std::string remembered = readSetting(std::string("mod.") + model->galaxy());
            gShare.modName = remembered.substr(0, remembered.rfind('|'));
        }
        if (!gShare.modName.empty() && !userDirectory().empty()) {
            gShare.preview = Share::describeFolder(userDirectory() / "mods" / gShare.modName);
        }
        std::snprintf(gShare.title, sizeof(gShare.title), "%s", !gShare.preview.title.empty() ? gShare.preview.title.c_str() : gShare.modName.c_str());
        std::snprintf(gShare.author, sizeof(gShare.author), "%s", gShare.preview.author.c_str());
        std::snprintf(gShare.version, sizeof(gShare.version), "%s", bumpVersion(gShare.preview.version).c_str());
        std::snprintf(gShare.description, sizeof(gShare.description), "%s", gShare.preview.description.c_str());
        // ~/Downloads (PETARI_EDITOR_SHARE_DIR for tests, so they never write to the player's folders).
        const char* home = std::getenv("HOME");
        if (const char* dir = std::getenv("PETARI_EDITOR_SHARE_DIR"); dir != nullptr && dir[0] != '\0') {
            std::snprintf(gShare.folder, sizeof(gShare.folder), "%s", dir);
        } else {
            std::snprintf(gShare.folder, sizeof(gShare.folder), "%s/Downloads", home != nullptr ? home : "");
        }
        ImGui::OpenPopup("Share mod");
    }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.02f, 0.20f, 0.31f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.40f, 0.86f, 1.0f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.06f, 0.12f, 0.72f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
    const bool open = ImGui::BeginPopupModal("Share mod", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);
    if (!open) return;
    if (gShare.modName.empty()) {
        // Nothing saved yet: Share packs the saved mod.
        ImGui::TextWrapped("Save your edits as a mod first (Cmd+S); Share packs the saved mod into a zip.");
        if (ImGui::Button("Save...", ImVec2(110.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
            requestSave(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    auto field = [](const char* label, char* buffer, size_t size, const char* hint) {
        ImGui::TextUnformatted(label);
        ImGui::SameLine(110.0f);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::PushID(label);
        ImGui::InputTextWithHint("##v", hint, buffer, size);
        ImGui::PopID();
    };
    field("Name", gShare.title, sizeof(gShare.title), gShare.modName.c_str());
    field("Author", gShare.author, sizeof(gShare.author), "your name");
    field("Version", gShare.version, sizeof(gShare.version), "1.0");
    field("Description", gShare.description, sizeof(gShare.description), "a line about it");
    ImGui::Spacing();
    // What it changes, where it plays, and what to fix first.
    ImGui::TextColored(kDim, "mods/%s", gShare.modName.c_str());
    if (!gShare.preview.galaxies.empty()) {
        std::string changes;
        for (const std::string& galaxy : gShare.preview.galaxies) changes += (changes.empty() ? "" : ", ") + galaxy;
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kDim, "Changes: %s (%zu archive%s)", changes.c_str(), gShare.preview.archives.size(),
                           gShare.preview.archives.size() == 1 ? "" : "s");
        ImGui::PopTextWrapPos();
    }
    if (!gShare.preview.newGalaxies.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kWarn, "New galaxy: it plays only in Petari. The original game has no way to add galaxies without code patches; "
                                  "edits to existing galaxies play anywhere the mod is installed.");
        ImGui::PopTextWrapPos();
    }
    if (gShare.preview.fullArchives && !gShare.preview.newGalaxyFilesOnly) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kWarn, "This mod holds full archives (the game's own data). Don't share it publicly: save it as a patch (Cmd+Shift+S) and share that.");
        ImGui::PopTextWrapPos();
    }
    int errors = 0;
    for (const Check& check : checks()) errors += check.severity == Severity::Error;
    if (errors > 0) {
        ImGui::TextColored(kWarn, "%d error%s in Checks: fix them first, or the level may crash for whoever plays it.", errors, errors == 1 ? "" : "s");
        ImGui::SameLine();
        if (ImGui::SmallButton("Show")) {
            ImGui::CloseCurrentPopup();
            openTab("Checks");
        }
    }
    ImGui::Separator();
    // Where: the native save panel (the folder field only without one: tests set PETARI_EDITOR_SHARE_DIR).
    const FileDialog dialog = std::getenv("PETARI_EDITOR_SHARE_DIR") != nullptr ? nullptr : Host::fileDialog();
    const std::string zipName = zipFileName(gShare.title, gShare.version);
    if (dialog == nullptr) field("Save to", gShare.folder, sizeof(gShare.folder), "~/Downloads");
    ImGui::TextColored(kDim, "%s", zipName.c_str());
    {
        std::lock_guard< std::mutex > lock(gDialogMutex);
        if (gDialogAnswered) {
            gDialogAnswered = false;
            gShare.panel = false;
            if (!gDialogPath.empty()) exportTo(gDialogPath);
        }
    }
    if (!gShare.result.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(gShare.ok ? ImVec4(0.45f, 0.95f, 0.55f, 1.0f) : kWarn, "%s", gShare.result.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::BeginDisabled(gShare.panel);
    const bool exportPressed = ImGui::Button(dialog != nullptr ? "Export .zip..." : "Export .zip", ImVec2(130.0f, 0.0f)) || gSubmitShare;
    ImGui::EndDisabled();
    if (exportPressed) {
        gSubmitShare = false;
        if (dialog != nullptr) {
            gShare.panel = true;
            const std::string suggested = (std::filesystem::path(gShare.folder) / zipName).string();
            dialog(true, suggested.c_str(), "Mod zip", "zip", &dialogDone, nullptr);
        } else {
            exportTo((std::filesystem::path(gShare.folder) / zipName).string());
        }
    }
    noteTestTarget("button:Export");
    ImGui::SameLine();
    if (gShare.ok && ImGui::Button("Show in Finder", ImVec2(130.0f, 0.0f))) {
        const std::string command = "open -R \"" + gShare.exported + "\"";
        std::system(command.c_str());
    }
    if (gShare.ok) ImGui::SameLine();
    if (ImGui::Button(gShare.ok ? "Done" : "Cancel", ImVec2(110.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void submitShareSheet() { gSubmitShare = true; }

void openShareSheetForTest() { openShareSheet(); }

// ---- Install (UX section 17): a mod zip dropped on the window ----

namespace {
struct InstallState {
    bool open = false;
    std::string path;
    std::optional< Share::Preview > preview;
    std::string error;          // inspecting or installing
    bool askReplace = false;    // the mod is installed already: replace it?
    std::string installed;      // the folder, once installed
} gInstall;
bool gOpenInstall = false;
bool gSubmitInstall = false;
}  // namespace

void openInstallSheet(const char* zipPath) {
    gInstall = InstallState{};
    gInstall.path = zipPath != nullptr ? zipPath : "";
    gInstall.preview = inspectModZip(gInstall.path, &gInstall.error);
    gOpenInstall = true;
    std::fprintf(stderr, "[editor] install: %s %s\n", gInstall.path.c_str(), gInstall.preview ? "inspected" : gInstall.error.c_str());
}

void submitInstallSheet() { gSubmitInstall = true; }

void drawInstallSheet() {
    if (gOpenInstall) {
        gOpenInstall = false;
        ImGui::OpenPopup("Install mod");
    }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.02f, 0.20f, 0.31f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.40f, 0.86f, 1.0f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.06f, 0.12f, 0.72f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
    const bool open = ImGui::BeginPopupModal("Install mod", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);
    if (!open) return;
    const std::string file = std::filesystem::path(gInstall.path).filename().string();
    if (!gInstall.preview) {
        ImGui::TextWrapped("%s isn't a Petari mod: %s", file.c_str(), gInstall.error.c_str());
        if (ImGui::Button("Close", ImVec2(110.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const Share::Preview& p = *gInstall.preview;
    ImGui::Text("%s", p.title.empty() ? p.folder.c_str() : p.title.c_str());
    if (!p.version.empty() || !p.author.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(kDim, "%s%s%s", p.version.empty() ? "" : ("v" + p.version).c_str(), (!p.version.empty() && !p.author.empty()) ? "  ·  by " : (p.author.empty() ? "" : "by "),
                           p.author.c_str());
    }
    if (!p.description.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(p.description.c_str());
        ImGui::PopTextWrapPos();
    }
    if (!p.galaxies.empty()) {
        std::string changes;
        for (const std::string& galaxy : p.galaxies) changes += (changes.empty() ? "" : ", ") + galaxy;
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kDim, "Changes: %s", changes.c_str());
        ImGui::PopTextWrapPos();
    }
    if (!p.needs.empty()) {
        std::string needs;
        for (const std::string& need : p.needs) needs += (needs.empty() ? "" : ", ") + need;
        ImGui::TextColored(kWarn, "Needs: %s", needs.c_str());
    }
    // What kind of mod: level edits (a patch on the player's own game) or whole files for other tools.
    if (p.fullArchives) {
        ImGui::TextColored(kWarn, "Full archives: replaces %zu game file%s (made for other tools).", p.archives.size(), p.archives.size() == 1 ? "" : "s");
    } else if (!p.galaxies.empty()) {
        ImGui::TextColored(kDim, "Level edits (a patch on your own game): %zu archive%s in %zu galax%s.", p.archives.size(),
                           p.archives.size() == 1 ? "" : "s", p.galaxies.size(), p.galaxies.size() == 1 ? "y" : "ies");
    } else {
        ImGui::TextColored(kDim, "No level edits yet.");
    }
    ImGui::TextColored(kDim, "%s%s", file.c_str(), p.editorMod ? "  ·  made in the level editor" : "");
    ImGui::Separator();
    if (!gInstall.installed.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1.0f), "Installed to mods/%s. It's on from the next start.", gInstall.installed.c_str());
        if (ImGui::Button("Done", ImVec2(110.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    if (!gInstall.error.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kWarn, "%s", gInstall.error.c_str());
        ImGui::PopTextWrapPos();
    }
    const char* label = gInstall.askReplace ? "Replace it" : "Install";
    if (ImGui::Button(label, ImVec2(120.0f, 0.0f)) || gSubmitInstall) {
        gSubmitInstall = false;
        std::string folder, error;
        if (installModZip(gInstall.path, gInstall.askReplace, &folder, &error)) {
            gInstall.installed = folder;
            gInstall.error.clear();
        } else {
            gInstall.error = error;
            // Runtime refuses an installed folder unless asked to replace it.
            gInstall.askReplace = error.find("already installed") != std::string::npos;
        }
        std::fprintf(stderr, "[editor] install: %s\n", gInstall.installed.empty() ? error.c_str() : ("installed " + folder).c_str());
    }
    noteTestTarget("button:Install");
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ---- New galaxy (UX section 14; docs/dev/level-editor/new-galaxy-flow.md) ----

namespace {
bool gOpenNewGalaxy = false;
bool gSubmitNewGalaxy = false;
struct NewGalaxyFields {
    char name[48] = {};
    char internalName[64] = {};
    bool internalEdited = false;
    int from = 1;               // NewGalaxyRequest::From
    char planetFilter[48] = {};
    std::string planet;
    std::string copyOf;
    std::string music;
    std::string sky;
    int missions = 1;
    char missionNames[8][48] = {};
    std::string sent;           // after Create: the name it was sent for
} gNew;

// A searchable list of choices; returns the chosen one (or keeps `current`).
void choiceList(const char* id, const std::vector< std::string >& items, const char* filter, std::string* current, float height,
                bool galaxies = false) {
    ImGui::BeginChild(id, ImVec2(-1.0f, height), ImGuiChildFlags_Borders);
    int shown = 0;
    for (const std::string& item : items) {
        if (filter != nullptr && !matchesQuery(item, filter)) continue;
        ImGui::PushID(item.c_str());
        const std::string label = galaxies ? galaxyChoiceLabel(item) : item;
        if (ImGui::Selectable(label.c_str(), *current == item))
            *current = item;
        ImGui::PopID();
        if (++shown > 400) break;
    }
    if (shown == 0) ImGui::TextDisabled(items.empty() ? "(the disc's list isn't ready yet)" : "Nothing matches.");
    ImGui::EndChild();
}
}  // namespace

void openNewGalaxySheet() { gOpenNewGalaxy = true; }
void openNewGalaxySheetForTest() { openNewGalaxySheet(); }
void submitNewGalaxySheet() { gSubmitNewGalaxy = true; }

void drawNewGalaxySheet() {
    if (gOpenNewGalaxy) {
        gOpenNewGalaxy = false;
        gNew = NewGalaxyFields{};
        std::snprintf(gNew.name, sizeof(gNew.name), "%s", "My Galaxy");
        const NewGalaxyChoices& choices = newGalaxyChoices();
        if (!choices.planets.empty()) gNew.planet = choices.planets.front();
        gNew.copyOf = std::find(choices.galaxies.begin(), choices.galaxies.end(), "EggStarGalaxy") != choices.galaxies.end()
                          ? "EggStarGalaxy" : (choices.galaxies.empty() ? "" : choices.galaxies.front());
        gNew.music = gNew.copyOf;
        for (int m = 0; m < 8; m++) std::snprintf(gNew.missionNames[m], sizeof(gNew.missionNames[m]), "Mission %d", m + 1);
        ImGui::OpenPopup("New galaxy");
    }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.02f, 0.20f, 0.31f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.40f, 0.86f, 1.0f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.06f, 0.12f, 0.72f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
    const bool open = ImGui::BeginPopupModal("New galaxy", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);
    if (!open) return;
    const NewGalaxyChoices& choices = newGalaxyChoices();
    if (!gNew.sent.empty()) {
        ImGui::TextWrapped("Creating %s... Petari restarts into it, in Edit, in about half a minute.", gNew.sent.c_str());
        if (ImGui::Button("OK", ImVec2(110.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    // 1. Start from.
    drawSection("1  Start from");
    static const char* const kFrom[4] = {"Empty space", "One planet", "Brush skeleton", "Copy a galaxy"};
    static const char* const kFromHelp[4] = {"A start point and nothing else.", "A planet from the game, with its gravity.",
                                             "A checker box to stand on and a Power Star, ready for brushes.",
                                             "A galaxy from the game, as your own copy (the original is untouched)."};
    for (int i = 0; i < 4; i++) {
        if (i > 0) ImGui::SameLine();
        if (ImGui::RadioButton(kFrom[i], gNew.from == i)) gNew.from = i;
        noteTestTarget((std::string("template:") + kFrom[i]).c_str());
    }
    ImGui::TextColored(kDim, "%s", kFromHelp[gNew.from]);
    if (gNew.from == 1) {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##planet-filter", "Find a planet", gNew.planetFilter, sizeof(gNew.planetFilter));
        choiceList("planets", choices.planets, gNew.planetFilter, &gNew.planet, 120.0f);
    } else if (gNew.from == 3) {
        choiceList("galaxies", choices.galaxies, nullptr, &gNew.copyOf, 120.0f, true);
    }
    // 2. Look and sound.
    drawSection("2  Sky and music");
    ImGui::TextUnformatted("Sky");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##sky", gNew.sky.empty() ? "Template's" : skyChoiceLabel(gNew.sky).c_str())) {
        if (ImGui::Selectable("Template's", gNew.sky.empty())) gNew.sky.clear();
        noteTestTarget("sky:template");
        for (const auto& sky : choices.skies) {
            ImGui::PushID(sky.c_str());
            if (ImGui::Selectable(skyChoiceLabel(sky).c_str(), gNew.sky == sky))
                gNew.sky = sky;
            noteTestTarget(("sky:" + sky).c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", sky.c_str());
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    noteTestTarget("combo:New galaxy sky");
    ImGui::TextUnformatted("Music");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##music", gNew.music.empty() ? "Good Egg's" : galaxyChoiceLabel(gNew.music).c_str())) {
        for (const std::string& galaxy : choices.galaxies) {
            ImGui::PushID(galaxy.c_str());
            if (ImGui::Selectable(galaxyChoiceLabel(galaxy).c_str(), gNew.music == galaxy))
                gNew.music = galaxy;
            noteTestTarget(("music:" + galaxy).c_str());
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    noteTestTarget("combo:New galaxy music");
    ImGui::TextColored(kDim, "Music and sound banks borrowed from that galaxy.");
    // 3. Name and missions.
    drawSection("3  Name and missions");
    ImGui::TextUnformatted("Name");
    ImGui::SameLine(110.0f);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##name", gNew.name, sizeof(gNew.name));
    if (!gNew.internalEdited) std::snprintf(gNew.internalName, sizeof(gNew.internalName), "%s", newGalaxyInternalName(gNew.name).c_str());
    const std::string problem = checkNewGalaxyName(gNew.internalName);
    if (ImGui::TreeNode("Advanced")) {
        ImGui::TextUnformatted("Internal name");
        ImGui::SameLine(110.0f);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##internal", gNew.internalName, sizeof(gNew.internalName))) gNew.internalEdited = true;
        ImGui::TreePop();
    }
    ImGui::TextColored(problem.empty() ? kDim : kWarn, "%s", problem.empty() ? ("Stage " + std::string(gNew.internalName) + ", in Level Select's Mod galaxies").c_str() : problem.c_str());
    ImGui::TextUnformatted("Missions");
    ImGui::SameLine(110.0f);
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderInt("##missions", &gNew.missions, 1, 8);
    for (int m = 0; m < gNew.missions; m++) {
        ImGui::PushID(m);
        ImGui::SetCursorPosX(110.0f);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##mission", gNew.missionNames[m], sizeof(gNew.missionNames[m]));
        ImGui::PopID();
    }
    ImGui::TextColored(kDim, "Stars collected there are kept in the mod's own save, never in your game's save slots.");
    ImGui::Separator();
    // Create: the current galaxy's unsaved edits first.
    const bool unsaved = status().unsaved;
    bool discard = false;
    if (unsaved) {
        ImGui::TextColored(kWarn, "You have unsaved edits here. Save them first, or discard them to create.");
        if (ImGui::Button("Save...", ImVec2(110.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
            requestSave(false);
        }
        ImGui::SameLine();
        discard = ImGui::Button("Discard and create", ImVec2(160.0f, 0.0f));
        ImGui::SameLine();
    }
    const bool ready = problem.empty() && gNew.name[0] != '\0' && (gNew.from != 1 || !gNew.planet.empty()) && (gNew.from != 3 || !gNew.copyOf.empty());
    ImGui::BeginDisabled(!ready || unsaved);
    const bool create = ImGui::Button("Create", ImVec2(110.0f, 0.0f));
    ImGui::EndDisabled();
    noteTestTarget("button:Create");
    if (ready && (create || discard || gSubmitNewGalaxy)) {
        gSubmitNewGalaxy = false;
        Command command{Command::Kind::NewGalaxy};
        command.newGalaxy.name = gNew.name;
        command.newGalaxy.internalName = gNew.internalName;
        command.newGalaxy.from = static_cast< NewGalaxyRequest::From >(gNew.from);
        command.newGalaxy.planet = gNew.planet;
        command.newGalaxy.copyOf = gNew.copyOf;
        command.newGalaxy.musicFrom = gNew.music;
        command.newGalaxy.sky = gNew.sky;  // M4: empty retains the template sky.
        for (int m = 0; m < gNew.missions; m++) command.newGalaxy.missions.push_back(gNew.missionNames[m]);
        command.newGalaxy.discardUnsaved = discard || (gSubmitNewGalaxy && unsaved);
        enqueue(std::move(command));
        gNew.sent = gNew.name;
        std::fprintf(stderr, "[editor] new galaxy: %s (%s, from %s)\n", gNew.name, gNew.internalName, kFrom[gNew.from]);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

}  // namespace PetariEditor::Ui
