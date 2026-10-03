#include "array.hpp"
#include "imgui.h"
#include "panels.hpp"
#include <algorithm>
#include <cmath>
#include <petari/editor/array.hpp>

namespace PetariEditor::Ui {
    namespace A = Arrays;
    void drawArrayInspector(const ModelView& model, const std::vector< RowId >& rows) {
        if (rows.empty() || !model.hasTransform(rows.front()) || !model.cell(rows.front(), columnHash("pos_x")))
            return;
        drawSection("Arrange", "copies");
        if (!A::view().active) {
            if (ImGui::Button("Array...")) {
                A::Input in;
                in.begin = true;
                A::send(in);
            }
            if (ImGui::IsItemHovered())
                helpText() = "Cmd+Shift+D: line, arc, ring, grid or copies along a path.";
            if (!A::view().message.empty())
                ImGui::TextWrapped("%s", A::view().message.c_str());
            return;
        }
        auto p = A::parameters();
        bool changed = false;
        int pattern = static_cast< int >(p.pattern);
        changed |= ImGui::Combo("Pattern", &pattern, "Line\0Arc\0Ring\0Grid\0Along a path\0");
        p.pattern = static_cast< A::Pattern >(pattern);
        if (p.pattern == A::Pattern::Path) {
            const auto rails = Paths::collect(model);
            auto name = [&](const Paths::Rail& rail) { return std::string(model.zoneName(rail.zone)) + " / Path " + std::to_string(rail.id); };
            std::string selected = "Choose path...";
            for (const auto& rail : rails)
                if (rail.row == p.rail)
                    selected = name(rail);
            if (ImGui::BeginCombo("Path", selected.c_str())) {
                for (const auto& rail : rails) {
                    if (ImGui::Selectable(name(rail).c_str(), rail.row == p.rail)) {
                        p.rail = rail.row;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            if (rails.empty())
                ImGui::TextWrapped("Create a path first, then choose it here.");
            changed |= ImGui::Checkbox("Use spacing", &p.pathSpacing);
            changed |= ImGui::InputFloat("Start distance", &p.startDistance, 10, 100, "%.0f");
            ImGui::TextDisabled("Path length: %.0f", A::view().pathLength);
        }
        if (p.pattern == A::Pattern::Grid) {
            changed |= ImGui::InputInt("Rows", &p.rows);
            changed |= ImGui::InputInt("Columns", &p.columns);
        } else if (p.pattern != A::Pattern::Path || !p.pathSpacing)
            changed |= ImGui::InputInt("Copies", &p.count);
        if (p.pattern == A::Pattern::Line || p.pattern == A::Pattern::Grid || (p.pattern == A::Pattern::Path && p.pathSpacing))
            changed |= ImGui::InputFloat("Spacing", &p.spacing, 10, 100, "%.0f");
        if (p.pattern == A::Pattern::Arc || p.pattern == A::Pattern::Ring) {
            changed |= ImGui::InputFloat("Radius", &p.radius, 10, 100, "%.0f");
            if (p.pattern == A::Pattern::Arc)
                changed |= ImGui::SliderFloat("Sweep", &p.arcDegrees, 1, 359, "%.0f degrees");
            changed |= ImGui::Checkbox("Centre on play marker", &p.markerCenter);
        }
        int facing = static_cast< int >(p.facing);
        if (p.pattern == A::Pattern::Arc || p.pattern == A::Pattern::Ring)
            changed |= ImGui::Combo("Facing", &facing, "Keep heading\0Along curve\0Outward\0Inward\0");
        else {
            bool align = p.facing != A::Facing::Preserve;
            if (ImGui::Checkbox(p.pattern == A::Pattern::Path ? "Align to path" : "Align to line", &align)) {
                facing = static_cast< int >(align ? A::Facing::Along : A::Facing::Preserve);
                changed = true;
            }
        }
        p.facing = static_cast< A::Facing >(facing);
        changed |= ImGui::Checkbox("Snap each copy to ground", &p.groundSnap);
        if (ImGui::IsItemHovered())
            helpText() = "Follow gravity separately at every copy, keeping the source's height above the ground.";
        if (changed)
            A::configure(p);
        ImGui::TextWrapped("%zu new objects. The source stays in place.", A::view().copies.size());
        if (!A::view().message.empty())
            ImGui::TextWrapped("%s", A::view().message.c_str());
        if (p.pattern == A::Pattern::Line)
            ImGui::TextDisabled("Drag the end dot to change count.");
        if (p.pattern == A::Pattern::Arc || p.pattern == A::Pattern::Ring)
            ImGui::TextDisabled("Drag the dot to change radius.");
        ImGui::BeginDisabled(A::view().copies.empty());
        if (ImGui::Button("Apply array")) {
            A::Input in;
            in.apply = true;
            A::send(in);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            A::Input in;
            in.cancel = true;
            A::send(in);
        }
        ImGui::TextWrapped("One undo step. Selecting another object applies the preview; Esc cancels.");
    }
    bool drawArrayOverlay(float x, float y, float width, float height) {
        static bool dragging = false;
        const auto& view = A::view();
        auto& io = ImGui::GetIO();
        if (!view.active) {
            dragging = false;
            return false;
        }
        if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            A::Input in;
            in.cancel = true;
            A::send(in);
            dragging = false;
            return true;
        }
        const ImVec2 center{x + (view.handleX * .5f + .5f) * width, y + (.5f - view.handleY * .5f) * height};
        const auto mouse = io.MousePos;
        const bool inside = mouse.x >= x && mouse.x < x + width && mouse.y >= y && mouse.y < y + height;
        const bool hot = inside && !io.WantCaptureMouse && view.handleVisible && std::hypot(mouse.x - center.x, mouse.y - center.y) < 12;
        if (view.handleVisible) {
            auto* draw = ImGui::GetForegroundDrawList();
            draw->PushClipRect({x, y}, {x + width, y + height}, true);
            draw->AddCircleFilled(center, hot || dragging ? 8 : 6, IM_COL32(100, 235, 230, 255));
            draw->AddCircle(center, 10, IM_COL32(3, 35, 50, 255), 0, 2);
            draw->PopClipRect();
        }
        A::Input in;
        if (hot && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt) {
            dragging = true;
            in.dragBegin = true;
        }
        if (dragging) {
            in.x = (mouse.x - x) / std::max(width, 1.f) * 2 - 1;
            in.y = 1 - (mouse.y - y) / std::max(height, 1.f) * 2;
            in.dragMove = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            in.dragEnd = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
            A::send(in);
            if (in.dragEnd)
                dragging = false;
            return true;
        }
        return hot;
    }
}  // namespace PetariEditor::Ui
