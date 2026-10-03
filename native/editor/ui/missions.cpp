#include "missions.hpp"
#include "imgui.h"
#include "panels.hpp"
#include <petari/editor/model_ops.hpp>
#include <petari/editor_pick.hpp>

#include <algorithm>
#include <array>
#include <cstdio>

namespace PetariEditor::Ui {
    namespace {
        struct Draft {
            const ModelView* model = nullptr;
            std::uint64_t revision = 0;
            std::vector< RowId > rows;
            std::uint32_t mask = 0, original = 0, touched = 0;
            std::array< size_t, 32 > members{};
            bool changed = false, mixed = false;
            std::string error;
        } gDraft;
    }  // namespace

    void drawMissionsInspector(const ModelView& model, const std::vector< RowId >& rows) {
        const int count = std::min(32, model.missionCount());
        if (rows.empty() || count <= 0)
            return;
        if (gDraft.model != &model || gDraft.revision != model.revision() || gDraft.rows != rows) {
            gDraft = {};
            gDraft.model = &model;
            gDraft.revision = model.revision();
            gDraft.rows = rows;
            const auto first = ModelOps::objectMissions(model, rows.front());
            for (const auto row : rows) {
                const auto mask = ModelOps::objectMissions(model, row);
                gDraft.mask |= mask;
                for (int bit = 0; bit < count; ++bit)
                    gDraft.members[bit] += (mask & (1u << bit)) != 0;
                gDraft.mixed |= mask != first;
            }
            gDraft.original = gDraft.mask;
        }
        drawSection("Appears in", gDraft.changed ? "apply to commit" : "missions");
        ImGui::PushID("mission-membership");
        const float width = ImGui::GetContentRegionAvail().x;
        float used = 0;
        for (int mission = 1; mission <= count; ++mission) {
            const std::uint32_t bit = 1u << (mission - 1);
            const bool included = (gDraft.mask & bit) != 0;
            const bool partial = gDraft.members[mission - 1] > 0 && gDraft.members[mission - 1] < rows.size();
            const bool showPartial = partial && !(gDraft.touched & bit);
            const float chipWidth = ImGui::CalcTextSize(std::to_string(mission).c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
            if (mission > 1 && used + chipWidth + 4 <= width)
                ImGui::SameLine(0, 4);
            else
                used = 0;
            used += chipWidth + 4;
            ImGui::PushStyleColor(ImGuiCol_Button, showPartial ? ImVec4(.98f, .80f, .22f, .30f) :
                                                   included    ? ImVec4(.98f, .80f, .22f, .80f) :
                                                                 ImVec4(1, 1, 1, .06f));
            ImGui::PushStyleColor(ImGuiCol_Text, included && !showPartial ? ImVec4(.30f, .17f, .02f, 1) : ImVec4(.65f, .78f, .83f, 1));
            char label[32];
            std::snprintf(label, sizeof(label), "%d##mission", mission);
            ImGui::PushID(mission);
            if (ImGui::SmallButton(label)) {
                gDraft.mask ^= bit;
                gDraft.touched |= bit;
                gDraft.changed = gDraft.mask != gDraft.original || gDraft.mixed;
                gDraft.error.clear();
            }
            ImGui::PopStyleColor(2);
            if (((gDraft.mask ^ gDraft.original) & bit) || (partial && (gDraft.touched & bit)))
                ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(250, 204, 56, 255),
                                                    ImGui::GetStyle().FrameRounding, 0, 1.0f);
            char target[40];
            std::snprintf(target, sizeof(target), "mission-chip:%d", mission);
            noteTestTarget(target);
            if (ImGui::IsItemHovered()) {
                if (rows.size() > 1)
                    ImGui::SetTooltip("%s: currently in %zu of %zu selected objects.\nApply will %s this mission for all selected objects.",
                                      missionLabel(model, mission).c_str(), gDraft.members[mission - 1], rows.size(),
                                      included ? "include" : "exclude");
                else
                    ImGui::SetTooltip("%s: %s. Apply commits the whole membership change.", missionLabel(model, mission).c_str(),
                                      included ? "included" : "excluded");
                helpText() = "Choose missions, then Apply. Matching copies in other missions change together, in one undo step.";
            }
            ImGui::PopID();
        }
        if (gDraft.mixed)
            ImGui::TextWrapped("Mixed membership. Apply gives all selected objects these missions.");
        if (!gDraft.mask)
            ImGui::TextDisabled("In no mission");
        auto commit = [&](std::uint32_t mask) {
            auto result = ModelOps::changeMissions(model, rows, mask);
            gDraft.error = result.error;
            if (!result.error.empty())
                return;
            if (!result.op.adds.empty() || !result.op.sets.empty() || !result.op.deletes.empty() || !result.op.revertRows.empty())
                applyOp(std::move(result.op));
            for (size_t i = 0; i < result.rows.size(); ++i)
                PetariNative::EditorPick::selectRow(result.rows[i].v,
                                                    i ? PetariNative::EditorPick::Click::Add : PetariNative::EditorPick::Click::Replace);
            gDraft.mask = mask;
            gDraft.changed = false;
        };
        const int current = model.scenario();
        ImGui::BeginDisabled(current < 1 || current > count);
        if (ImGui::SmallButton("Only this mission") && current >= 1 && current <= count)
            commit(1u << (current - 1));
        noteTestTarget("button:Only this mission");
        if (ImGui::IsItemHovered())
            helpText() = "Put these objects, including matching mission copies, only in this mission. One undo restores the previous membership.";
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!gDraft.changed && !gDraft.mixed);
        const std::string applyLabel = rows.size() > 1 ? "Apply to " + std::to_string(rows.size()) + " selected objects" : "Apply";
        if (ImGui::SmallButton(applyLabel.c_str()))
            commit(gDraft.mask);
        noteTestTarget("button:Apply missions");
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel")) {
            gDraft.mask = gDraft.original;
            gDraft.touched = 0;
            gDraft.changed = false;
            gDraft.error.clear();
        }
        noteTestTarget("button:Cancel missions");
        ImGui::EndDisabled();
        if (!gDraft.error.empty()) {
            ImGui::TextWrapped("%s", gDraft.error.c_str());
            helpText() = gDraft.error;
        }
        ImGui::PopID();
    }
}  // namespace PetariEditor::Ui
