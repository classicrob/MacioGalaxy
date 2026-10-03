#include "areas.hpp"
#include "host.hpp"
#include "imgui.h"
#include "panels.hpp"
#include <algorithm>
#include <map>
#include <petari/editor/areas.hpp>
#include <petari/editor/camera_preview.hpp>
#include <petari/editor_pick.hpp>

namespace PetariEditor::Ui {
    namespace {
        namespace A = Areas;
        namespace P = CameraPreview;
        P::Source previewSource = P::Source::Marker;
        bool previewEnabled = true;
        ImTextureID previewTexture = 0;
        RowId previewRow;
        double nextPreview = 0;
        void cameraParameters(const ModelView& m, const A::Volume& area) {
            const auto row = A::cameraRow(m, area);
            if (!row.valid()) {
                ImGui::TextWrapped("No camera parameters match this area ID.");
                return;
            }
            ImGui::Checkbox("Camera preview", &previewEnabled);
            int source = static_cast< int >(previewSource);
            if (ImGui::Combo("Target", &source, "Play marker / area centre\0Mario\0"))
                previewSource = static_cast< P::Source >(source);
            ImGui::TextWrapped("Preview shows scene models. Effects and postprocessing appear in Play.");
            const auto* type = m.cell(row, columnHash("camtype"));
            if (type)
                ImGui::TextWrapped("Camera type: %s", type->s.c_str());
            struct Draft {
                std::uint64_t revision;
                float value;
            };
            static std::map< std::pair< RowId, std::uint32_t >, Draft > drafts;
            for (const auto& field : std::initializer_list< std::pair< const char*, const char* > >{{"Distance", "dist"},
                                                                                                    {"Field of view", "fovy"},
                                                                                                    {"Angle A", "angleA"},
                                                                                                    {"Angle B", "angleB"},
                                                                                                    {"Roll", "roll"},
                                                                                                    {"Forward offset", "loffset"},
                                                                                                    {"Vertical offset", "loffsetv"}}) {
                auto hash = columnHash(field.second);
                const auto* cell = m.cell(row, hash);
                if (!cell || cell->type != CellType::Float)
                    continue;
                auto [it, added] = drafts.emplace(std::make_pair(row, hash), Draft{m.revision(), cell->f});
                if (it->second.revision != m.revision())
                    it->second = {m.revision(), cell->f};
                const bool defaultFov = hash == columnHash("fovy") && !m.cell(row, columnHash("flag.nofovy"));
                ImGui::BeginDisabled(defaultFov);
                ImGui::InputFloat(field.first, &it->second.value, 1, 10, "%.2f");
                ImGui::EndDisabled();
                if (defaultFov && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("This camera uses the galaxy's default field of view.");
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    Op op;
                    op.label = std::string("Edit camera ") + field.first;
                    Cell value = *cell;
                    value.f = it->second.value;
                    op.sets.push_back({row, hash, value});
                    if (hash == columnHash("fovy")) {
                        if (const auto* flag = m.cell(row, columnHash("flag.nofovy"))) {
                            Cell enabled = *flag;
                            enabled.i = 1;
                            op.sets.push_back({row, columnHash("flag.nofovy"), enabled});
                        }
                    }
                    applyOp(std::move(op));
                }
            }
        }
        void visibility(bool cameras) {
            const auto current = A::visibility(cameras);
            bool visible = current != A::Visibility::None;
            if (ImGui::Checkbox(cameras ? "Cameras" : "Areas", &visible)) {
                A::setVisibility(cameras, visible ? A::Visibility::All : A::Visibility::None);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s. %s cycles selection, all, or hidden. Click an outline to select.",
                                  current == A::Visibility::Selection ? "Showing selected volumes" :
                                  visible                             ? "Showing all volumes" :
                                                                        "Volumes hidden",
                                  cameras ? "3" : "2");
            }
        }
        void dimension(const ModelView& m, RowId row, const char* label, const char* field, float multiplier) {
            const auto hash = columnHash(field);
            const auto* cell = m.cell(row, hash);
            if (!cell)
                return;
            struct Draft {
                std::uint64_t revision;
                float value;
            };
            static std::map< std::pair< RowId, std::uint32_t >, Draft > drafts;
            auto [it, added] = drafts.emplace(std::make_pair(row, hash), Draft{m.revision(), cell->f * multiplier});
            if (it->second.revision != m.revision())
                it->second = {m.revision(), cell->f * multiplier};
            ImGui::PushID(field);
            ImGui::InputFloat(label, &it->second.value, 10, 100, "%.1f");
            if (ImGui::IsItemDeactivatedAfterEdit() && it->second.value > 0) {
                Op op;
                op.label = std::string("Resize ") + displayName(m, row);
                Cell value = *cell;
                value.f = it->second.value / multiplier;
                op.sets.push_back({row, hash, value});
                applyOp(std::move(op));
            }
            ImGui::PopID();
        }
    }  // namespace
    void drawAreasToggle(bool cameras) {
        visibility(cameras);
    }
    void drawAreasInspector(const ModelView& m, const std::vector< RowId >& rows) {
        if (rows.size() != 1)
            return;
        const auto v = A::describe(m, rows.front());
        if (!v)
            return;
        drawSection(v->definition->kind == A::Kind::Camera ? "Camera area" : "Area", A::shapeName(v->definition->shape));
        ImGui::TextWrapped("%s · %s", A::kindName(v->definition->kind), A::shapeName(v->definition->shape));
        const auto shape = v->definition->shape;
        if (shape == A::Shape::Box || shape == A::Shape::BaseBox) {
            dimension(m, v->row, "Width", "scale_x", 1000);
            dimension(m, v->row, "Height", "scale_y", 1000);
            dimension(m, v->row, "Depth", "scale_z", 1000);
        } else {
            dimension(m, v->row, "Radius", "scale_x", 500);
            if (shape == A::Shape::Cylinder)
                dimension(m, v->row, "Height", "scale_y", 500);
        }
        ImGui::TextWrapped("Use the move, rotate and scale gizmos to edit the outline.");
        if (v->definition->kind == A::Kind::Camera) {
            ImGui::TextWrapped("Camera changes apply to play after Reload. The outline shows your edits now.");
            cameraParameters(m, *v);
        }
        if (ImGui::Button("Show all areas of this kind"))
            A::setVisibility(v->definition->kind == A::Kind::Camera, A::Visibility::All);
    }
    bool drawAreasOverlay(float x, float y, float width, float height) {
        const auto& io = ImGui::GetIO();
        if (!io.WantTextInput && !io.KeyCtrl && !io.KeySuper && !io.KeyAlt)
            for (int i = 0; i < 2; ++i)
                if (ImGui::IsKeyPressed(i ? ImGuiKey_3 : ImGuiKey_2, false)) {
                    const bool cameras = i != 0;
                    A::setVisibility(cameras, A::visibility(cameras) == A::Visibility::All ? A::Visibility::Selection : A::Visibility::All);
                }
        RowId selected;
        const auto* m = model();
        const auto& selection = PetariNative::EditorPick::selection();
        if (m && selection.size() == 1) {
            const auto area = A::describe(*m, RowId{selection.front().row});
            if (area && area->definition->kind == A::Kind::Camera)
                selected = area->row;
        }
        const auto& frame = P::view();
        if (frame.copy && Host::readback()) {
            static std::vector< unsigned char > rgba(512 * 288 * 4);
            unsigned w = 0, h = 0;
            int ready = Host::readback()(frame.copy, rgba.data(), static_cast< unsigned >(rgba.size()), &w, &h);
            if (ready < 0) {
                rgba.resize(w * h * 4);
                ready = Host::readback()(frame.copy, rgba.data(), static_cast< unsigned >(rgba.size()), &w, &h);
            }
            if (ready > 0) {
                if (!previewTexture && Host::makeTexture())
                    previewTexture = static_cast< ImTextureID >(Host::makeTexture()(w, h, rgba.data()));
                else if (Host::updateTexture())
                    Host::updateTexture()(static_cast< unsigned long long >(previewTexture), w, h, rgba.data());
                previewRow = frame.area;
                P::acknowledge(frame.copy);
            }
        }
        if (!selected.valid() || !previewEnabled) {
            if (P::current().area.valid())
                P::request({}, previewSource);
            return false;
        }
        if (ImGui::GetTime() >= nextPreview || P::current().area != selected || P::current().source != previewSource) {
            P::request(selected, previewSource);
            nextPreview = ImGui::GetTime() + 0.1;
        }
        const float w = std::min(360.f, width * 0.45f);
        ImGui::SetNextWindowPos({x + width - w - 12, y + 12});
        ImGui::SetNextWindowSize({w, 0});
        ImGui::SetNextWindowBgAlpha(0.95f);
        ImGui::Begin("Camera preview##area-pip", nullptr,
                     ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
        if (previewTexture && previewRow == selected && frame.area == selected)
            ImGui::Image(previewTexture, {w - 16, (w - 16) * 270 / 480});
        if (frame.area == selected)
            ImGui::TextWrapped("%s", frame.message.c_str());
        else
            ImGui::TextUnformatted("Preparing preview...");
        ImGui::TextDisabled("Camera edits: Reload to apply in Play");
        const bool consumed = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        ImGui::End();
        return consumed;
    }
}  // namespace PetariEditor::Ui
