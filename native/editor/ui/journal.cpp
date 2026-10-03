#include "journal.hpp"
#include "panels.hpp"
#include "imgui.h"
#include <petari/editor/edit_api.hpp>
#include <algorithm>
#include <ctime>

namespace PetariEditor::Ui {
std::string journalQuestion(int edits, std::int64_t saved, std::int64_t now) {
    const auto age = std::max<std::int64_t>(0, now - saved);
    std::string when = "just now";
    if (saved <= 0) when = "an earlier session";
    else if (age >= 86400) when = std::to_string(age / 86400) + (age < 172800 ? " day ago" : " days ago");
    else if (age >= 3600) when = std::to_string(age / 3600) + (age < 7200 ? " hour ago" : " hours ago");
    else if (age >= 60) when = std::to_string(age / 60) + (age < 120 ? " minute ago" : " minutes ago");
    return "Restore " + std::to_string(edits) + (edits == 1 ? " unsaved edit from " : " unsaved edits from ") + when + "?";
}

void drawJournalSheet() {
    static bool pending = false;
    static bool attempted = false;
    static std::uint64_t sentError = 0;
    static std::string offeredGalaxy;
    static std::int64_t offeredTime = 0;
    const auto& st = status();
    const auto* m = model();
    const bool offer = mode() == Mode::Editing && m && st.journalEdits > 0;
    if (offer && (offeredGalaxy != m->galaxy() || offeredTime != st.journalTime)) {
        offeredGalaxy = m->galaxy();
        offeredTime = st.journalTime;
        pending = false;
        attempted = false;
    }
    if (pending && st.errorSerial != sentError) pending = false;
    if (offer && !ImGui::IsPopupOpen("Unsaved edits")) ImGui::OpenPopup("Unsaved edits");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(.5f, .5f));
    ImGui::SetNextWindowSize(ImVec2(460, 0));
    if (!ImGui::BeginPopupModal("Unsaved edits", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize)) return;
    if (!offer) {
        pending = false;
        offeredGalaxy.clear();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    ImGui::TextWrapped("%s", journalQuestion(st.journalEdits, st.journalTime, std::time(nullptr)).c_str());
    ImGui::TextWrapped("Restore reloads this galaxy with those edits. They remain unsaved until you save. Discard removes the recovery copy.");
    if (attempted && st.errorSerial != sentError && !st.lastError.empty()) ImGui::TextWrapped("%s", st.lastError.c_str());
    auto send = [&](Command::Kind kind) {
        enqueue(Command{kind});
        pending = true;
        attempted = true;
        sentError = st.errorSerial;
    };
    ImGui::BeginDisabled(pending);
    if (ImGui::Button("Restore", ImVec2(120, 0))) send(Command::Kind::RestoreJournal);
    noteTestTarget("button:Restore journal");
    ImGui::SameLine();
    if (ImGui::Button("Discard", ImVec2(120, 0))) send(Command::Kind::DiscardJournal);
    noteTestTarget("button:Discard journal");
    ImGui::EndDisabled();
    if (pending) ImGui::TextUnformatted("Applying...");
    ImGui::EndPopup();
}
}
