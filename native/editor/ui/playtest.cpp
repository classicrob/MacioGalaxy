#include "playtest.hpp"
#include "panels.hpp"
#include <imgui.h>
#include <petari/editor/playtest.hpp>
#include <petari/editor_pick.hpp>

namespace PetariEditor::Ui {
    namespace {
        std::string feedback;
        double feedbackUntil = 0;
        void keep() {
            const auto view = Playtest::view();
            std::vector< RowId > rows;
            for (const auto& item : PetariNative::EditorPick::selection())
                if (item.row)
                    rows.push_back({item.row});
            feedback = rows == view.keepRows ? view.keepReason : std::string{};
            if (rows.empty())
                feedback = "Select a moved object to Keep.";
            feedbackUntil = ImGui::GetTime() + 4;
            if (feedback.empty())
                Playtest::requestKeep(std::move(rows));
            else
                helpText() = feedback;
        }
    }  // namespace
    void updatePlaytestInput() {
        const auto& io = ImGui::GetIO();
        if (!io.WantTextInput && !io.KeyCtrl && !io.KeySuper && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_K, false))
            keep();
    }
    void drawPlaytestToggle() {
        ImGui::SameLine(0, 10);
        bool visible = Playtest::visible();
        if (ImGui::Checkbox("Show trail", &visible))
            Playtest::setVisible(visible);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Last 60 seconds: green ground, blue air, orange dive, purple cap bounce. Gold marks jump peaks.");
    }
    void drawPlaytestInspector(const std::vector< RowId >& rows) {
        const auto view = Playtest::view();
        if (ImGui::GetTime() < feedbackUntil && !feedback.empty())
            helpText() = feedback;
        if (!view.active || rows.empty())
            return;
        drawSection("Play-test", Playtest::elapsed(view.frames).c_str());
        const bool stale = rows != view.keepRows;
        ImGui::BeginDisabled(stale || !view.keepReason.empty());
        if (ImGui::Button("Keep played transform  K"))
            keep();
        ImGui::EndDisabled();
        if (!view.keepReason.empty())
            ImGui::TextWrapped("%s", view.keepReason.c_str());
        if (!view.keepResult.empty())
            ImGui::TextWrapped("%s", view.keepResult.c_str());
        if (ImGui::IsItemHovered() || ImGui::GetTime() < feedbackUntil)
            helpText() = feedback.empty() ? "Keep copies the live position, rotation and scale into the layout as one undo step." : feedback;
    }
    bool drawPlaytestFooter() {
        const auto view = Playtest::view();
        if (ImGui::GetTime() < feedbackUntil && !feedback.empty()) {
            ImGui::TextUnformatted(feedback.c_str());
            return true;
        }
        if (!view.active)
            return false;
        if (!view.keepResult.empty())
            ImGui::TextUnformatted(view.keepResult.c_str());
        else if (!view.keepRows.empty() && !view.keepReason.empty())
            ImGui::Text("K unavailable: %s", view.keepReason.c_str());
        else
            ImGui::Text("%s%s  |  Trail: %zu samples, %zu peaks  |  K keep   Tab play   Cmd+Z undo", view.cleared ? "Cleared in " : "Play-test ",
                        Playtest::elapsed(view.frames).c_str(), view.samples, view.peaks);
        return true;
    }
}  // namespace PetariEditor::Ui
