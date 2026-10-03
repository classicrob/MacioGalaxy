#include "gravity.hpp"
#include "imgui.h"
#include "panels.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <petari/editor/gravity.hpp>
#include <petari/editor_pick.hpp>
#include <petari/editor_settings.hpp>

namespace PetariEditor::Ui {
    namespace {
        void apply(Op op) {
            if (!op.sets.empty())
                applyOp(std::move(op));
        }
        float number(const ModelView& m, RowId r, const char* key, float fallback) {
            const Cell* c = m.cell(r, columnHash(key));
            const float value = !c ? fallback : c->type == CellType::Float ? c->f : static_cast< float >(c->i);
            return value < 0 && std::string(key) != "Range" ? fallback : value;
        }
        void numeric(const ModelView& m, const std::vector< RowId >& rows, const char* label, const char* key, float fallback, bool integer = false) {
            struct Draft {
                float value;
                std::uint64_t revision;
            };
            static std::map< std::pair< std::uint64_t, std::string >, Draft > drafts;
            auto draftKey = std::make_pair(rows.front().v, std::string(key));
            auto [it, inserted] = drafts.emplace(draftKey, Draft{number(m, rows.front(), key, fallback), m.revision()});
            if (it->second.revision != m.revision())
                it->second = {number(m, rows.front(), key, fallback), m.revision()};
            float& value = it->second.value;
            ImGui::PushID(key);
            ImGui::TextUnformatted(label);
            ImGui::SetNextItemWidth(-1);
            if (integer) {
                int v = static_cast< int >(value);
                ImGui::InputInt("##value", &v);
                value = static_cast< float >(v);
                if (ImGui::IsItemDeactivatedAfterEdit())
                    apply(Gravity::numericOp(m, rows, key, static_cast< float >(v), true));
            } else {
                ImGui::InputFloat("##value", &value, 10, 100, "%.1f");
                if (ImGui::IsItemDeactivatedAfterEdit())
                    apply(Gravity::numericOp(m, rows, key, value));
            }
            ImGui::PopID();
        }
    }  // namespace
    void drawGravityInspector(const ModelView& m, const std::vector< RowId >& rows) {
        if (rows.empty() || Gravity::shapeForPlacement(m.objectName(rows.front())) == Gravity::Shape::Unknown)
            return;
        drawSection("Gravity", "live");
        const auto shape = Gravity::shapeForPlacement(m.objectName(rows.front()));
        if (rows.size() == 1) {
            if (ImGui::BeginCombo("Shape", Gravity::shapeName(shape))) {
                for (int i = 0; i < 8; i++) {
                    auto s = static_cast< Gravity::Shape >(i);
                    bool noPath = s == Gravity::Shape::Wire && number(m, rows.front(), "CommonPath_ID", -1) < 0;
                    ImGui::BeginDisabled(noPath);
                    if (ImGui::Selectable(Gravity::shapeName(s), s == shape))
                        apply(Gravity::shapeOp(m, rows.front(), s));
                    ImGui::EndDisabled();
                    if (noPath && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("Assign a path in Raw fields before choosing Wire.");
                }
                ImGui::EndCombo();
            }
            if (shape == Gravity::Shape::Parallel) {
                const char* name = m.objectName(rows.front());
                int b = std::string(name) == "GlobalPlaneGravityInBox" ? 1 : std::string(name) == "GlobalPlaneGravityInCylinder" ? 2 : 0;
                if (ImGui::Combo("Boundary", &b, "Sphere / infinite\0Box\0Cylinder\0"))
                    apply(Gravity::shapeOp(m, rows.front(), shape, static_cast< Gravity::Bound >(b)));
            }
        }
        const std::string className = m.objectName(rows.front());
        const bool boundedParallel = className == "GlobalPlaneGravityInBox" || className == "GlobalPlaneGravityInCylinder";
        ImGui::BeginDisabled(boundedParallel);
        numeric(m, rows, "Range (-1 = everywhere)", "Range", -1);
        ImGui::EndDisabled();
        if (boundedParallel)
            ImGui::TextWrapped("This field reaches the box or cylinder boundary. Scale its bounds to change the reach.");
        numeric(m, rows, "Distant", "Distant", shape == Gravity::Shape::Point ? m.transform(rows.front()).scale.x * 500 : 0);
        numeric(m, rows, "Priority (equal priorities blend)", "Priority", 0, true);
        bool inverse = number(m, rows.front(), "Inverse", 0) > 0;
        if (ImGui::Checkbox("Inverse: pull outward", &inverse))
            apply(Gravity::numericOp(m, rows, "Inverse", inverse ? 1 : 0, true));
        if (shape == Gravity::Shape::Torus)
            numeric(m, rows, "Ring width", "Obj_arg2", 0, true);
        if (shape == Gravity::Shape::Cone)
            numeric(m, rows, "Top cut (0 to 1000)", "Obj_arg1", 0, true);
        if (shape == Gravity::Shape::Disk)
            numeric(m, rows, "Arc degrees", "Obj_arg2", 360, true);
        if (shape == Gravity::Shape::Wire)
            ImGui::TextWrapped("The wire follows its linked path. Edit the path handles to change its shape; Range sets its reach.");
        else
            ImGui::TextWrapped("Move, rotate and scale with the gizmo. Transform scale sets the field shape; Range sets its reach.");
        if (ImGui::Button("Show gravity view"))
            Gravity::setVisible(true);
    }
    bool drawGravityOverlay(float x, float y, float width, float height) {
        auto& io = ImGui::GetIO();
        if (!io.WantTextInput && !io.KeyCtrl && !io.KeySuper && ImGui::IsKeyPressed(ImGuiKey_1, false))
            Gravity::setVisible(!Gravity::visible());
        Gravity::setAirProbe(io.KeyAlt);
        if (!Gravity::visible())
            return false;
        auto v = Gravity::view();
        auto* draw = ImGui::GetForegroundDrawList();
        draw->PushClipRect({x, y}, {x + width, y + height}, true);
        if (v.probe.valid) {
            ImVec2 p{x + (v.probeX + 1) * width / 2, y + (1 - v.probeY) * height / 2};
            std::string label = v.probe.covered ? v.probe.name + " | priority " + std::to_string(v.probe.priority) : "No gravity";
            if (v.probe.blended > 1)
                label += " | blends " + std::to_string(v.probe.blended);
            if (v.probe.beaten > 0)
                label += " | beats " + std::to_string(v.probe.beaten);
            if (!v.probe.onGround)
                label += " (in air)";
            ImVec2 text = ImGui::CalcTextSize(label.c_str());
            p.x = std::clamp(p.x + 16, x + 8, std::max(x + 8, x + width - text.x - 12));
            p.y = std::clamp(p.y + 18, y + 8, std::max(y + 8, y + height - text.y - 12));
            draw->AddRectFilled({p.x - 5, p.y - 4}, {p.x + text.x + 5, p.y + text.y + 4}, 0xe022272a, 4);
            draw->AddText(p, v.probe.covered ? 0xffffffff : 0xff7777ff, label.c_str());
        }
        draw->AddText({x + 12, y + 12}, 0xffeee0b0, "Gravity view | 1 to hide | Option: probe in air");
        draw->PopClipRect();
        return false;
    }
}  // namespace PetariEditor::Ui
