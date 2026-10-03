#include "paths.hpp"
#include "imgui.h"
#include "panels.hpp"
#include <algorithm>
#include <cmath>
#include <petari/editor/paths_view.hpp>
#include <petari/editor_pick.hpp>

namespace PetariEditor::Ui {
    namespace {
        namespace P = Paths;
        namespace V = PathsView;
        namespace Pick = PetariNative::EditorPick;
        struct LinkDrag {
            RowId row;
            std::string field;
            bool active = false, moved = false;
            ImVec2 start;
            bool output = false;
        } link;
        struct PointDrag {
            bool active = false;
            V::Mark mark;
        } drag;
        RowId chosenWireRow;
        std::string chosenWireField, error;
        RowId targetRow;
        bool reactionPopup = false;
        bool captureClick = false;
        P::Socket sourceSocket;

        bool exists(const ModelView& m, RowId row) {
            return m.locate(row, nullptr, nullptr) && m.state(row) != RowState::Deleted;
        }
        void submit(Op op) {
            if (!op.sets.empty() || !op.adds.empty() || !op.deletes.empty())
                applyOp(std::move(op));
        }
        float distance(ImVec2 p, ImVec2 a, ImVec2 b, float* t = nullptr) {
            const float dx = b.x - a.x, dy = b.y - a.y;
            const float s = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / std::max(dx * dx + dy * dy, 0.0001f), 0.0f, 1.0f);
            if (t)
                *t = s;
            return std::hypot(p.x - a.x - s * dx, p.y - a.y - s * dy);
        }
        void startLink(RowId row, const std::string& field, ImVec2 at, bool output = false) {
            link = {row, field, true, false, at, output};
            V::setLinkingPath(field == "CommonPath_ID");
            chosenWireRow = {};
            error.clear();
        }
        void finishLink() {
            link.active = false;
            V::setLinkingPath(false);
        }
        const P::Rail* findRail(const std::vector< P::Rail >& rails, RowId id) {
            for (const auto& r : rails)
                if (r.row == id)
                    return &r;
            return nullptr;
        }
        void insertPoint(const P::Rail& rail, int index, float t) {
            const auto fresh = newRowId();
            auto op = P::insertPoint(rail, index, t, fresh, &error);
            if (op.adds.empty())
                return;
            submit(std::move(op));
            Pick::selectRow(fresh.v, Pick::Click::Replace);
        }
        void removePoint(const P::Rail& rail, RowId row) {
            auto op = P::deletePoint(rail, row, &error);
            if (op.deletes.empty())
                return;
            submit(std::move(op));
            Pick::selectRow(rail.row.v, Pick::Click::Replace);
        }
        void chooseTarget(const ModelView& m, RowId row) {
            const auto source = P::sockets(m, link.row);
            const auto it = std::find_if(source.begin(), source.end(), [](const P::Socket& s) {
                return s.field == link.field && (s.role == Catalog::SwitchRole::Write) == link.output;
            });
            if (it == source.end()) {
                error = "This object does not expose that switch socket.";
                return;
            }
            std::vector< P::Socket > choices;
            for (const auto& s : P::sockets(m, row))
                if (s.role != it->role)
                    choices.push_back(s);
            if (choices.empty()) {
                error = "This object has no compatible switch socket.";
                return;
            }
            if (choices.size() == 1)
                submit(P::linkSwitchOp(m, *it, choices.front(), &error));
            else {
                sourceSocket = *it;
                targetRow = row;
                reactionPopup = true;
            }
        }
        void wireLegend() {
            ImGui::TextDisabled("Wire colours follow their socket kinds");
            if (ImGui::BeginTable("wire-legend", 3, ImGuiTableFlags_SizingStretchSame)) {
                const char* fields[] = {"SW_APPEAR", "SW_DEAD", "SW_A", "SW_B", "SW_SLEEP", "other"};
                const char* labels[] = {"Appear", "Dead", "A", "B", "Sleep", "Other"};
                for (int i = 0; i < 6; ++i) {
                    ImGui::TableNextColumn();
                    const auto at = ImGui::GetCursorScreenPos();
                    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(at.x + 5, at.y + ImGui::GetTextLineHeight() / 2), 4,
                                                                P::switchColor(std::string(fields[i])));
                    ImGui::Dummy(ImVec2(12, ImGui::GetTextLineHeight()));
                    ImGui::SameLine(0, 2);
                    ImGui::TextUnformatted(labels[i]);
                }
                ImGui::EndTable();
            }
        }
        void socketButton(const ModelView& m, RowId row, const char* field, bool output) {
            ImGui::PushID(field);
            ImGui::PushID(output ? 1 : 0);
            const Cell* c = m.cell(row, columnHash(field));
            const int id = c ? c->i : -1;
            const ImVec2 at = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("socket", ImVec2(20, 20));
            const ImVec2 center(at.x + 10, at.y + 10);
            auto* list = ImGui::GetWindowDrawList();
            list->AddCircle(center, 6, P::switchColor(std::string(field)), 16, 2);
            if (output)
                list->AddCircleFilled(center, 3, P::switchColor(std::string(field)));
            if (ImGui::IsItemActivated())
                startLink(row, field, center, output);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Drag onto %s to link", std::string(field) == "CommonPath_ID" ? "a path" : "an object");
                helpText() = "Drag a socket to link; x unlinks in one undo step.";
            }
            ImGui::SameLine();
            ImGui::Text("%s  %s", field, id < 0 ? "Unlinked" : std::to_string(id).c_str());
            if (id >= 0) {
                ImGui::SameLine();
                if (ImGui::SmallButton("x"))
                    submit(P::unlink(row, field));
            }
            ImGui::PopID();
            ImGui::PopID();
        }
    }  // namespace
    void beginPathPick(RowId row) {
        startLink(row, "CommonPath_ID", ImGui::GetIO().MousePos);
    }
    void drawPathsToggle() {
        ImGui::SameLine();
        const char* labels[] = {"Links: selected", "Links: all", "Links: hidden"};
        if (ImGui::SmallButton(labels[static_cast< int >(V::visibility())]))
            V::setVisibility(static_cast< V::Visibility >((static_cast< int >(V::visibility()) + 1) % 3));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Paths and switch wires (5)");
    }
    bool pathsDelete() {
        const auto* m = model();
        if (!m)
            return false;
        if (chosenWireRow.valid()) {
            submit(P::unlink(chosenWireRow, chosenWireField));
            chosenWireRow = {};
            return true;
        }
        for (auto row : selectedRows())
            for (const auto& rail : P::collect(*m)) {
                for (const auto& point : rail.points)
                    if (point.row == row) {
                        removePoint(rail, row);
                        return true;
                    }
                if (rail.row == row) {
                    error = "Select a path point to delete it.";
                    return true;
                }
            }
        return false;
    }
    void drawPathsInspector(const ModelView& m, const std::vector< RowId >& rows) {
        if (rows.size() != 1) {
            for (RowId row : rows) {
                if (!P::sockets(m, row).empty()) {
                    drawSection("Switch wires", "");
                    wireLegend();
                    break;
                }
            }
            return;
        }
        const RowId row = rows.front();
        const auto rails = P::collect(m);
        for (const auto& rail : rails) {
            const auto it = std::find_if(rail.points.begin(), rail.points.end(), [&](const P::Point& p) { return p.row == row; });
            if (rail.row != row && it == rail.points.end())
                continue;
            drawSection("Path", ("#" + std::to_string(rail.id)).c_str());
            bool closed = rail.closed;
            if (ImGui::Checkbox("Closed loop", &closed)) {
                Op op;
                op.label = "Change path loop";
                op.sets.push_back({rail.row, columnHash("closed"), Cell{CellType::StringOffset, 0, 0, closed ? "CLOSE" : "OPEN"}});
                submit(std::move(op));
            }
            ImGui::Text("%d points", static_cast< int >(rail.points.size()));
            ImGui::TextWrapped("Drag points and handles. Option-click the curve to insert a point.");
            if (it != rail.points.end()) {
                const P::Point p = *it;
                for (int h = 0; h < 3; ++h) {
                    Vec3 value = h == 0 ? p.position : h == 1 ? p.incoming : p.outgoing;
                    float xyz[] = {value.x, value.y, value.z};
                    if (ImGui::InputFloat3(h == 0 ? "Point" : h == 1 ? "Incoming" : "Outgoing", xyz, "%.1f", ImGuiInputTextFlags_EnterReturnsTrue))
                        submit(h == 0 ? P::movePoint(p, {xyz[0], xyz[1], xyz[2]}) : P::moveHandle(p, h == 2, {xyz[0], xyz[1], xyz[2]}));
                }
                const int index = static_cast< int >(it - rail.points.begin());
                if ((rail.closed || index + 1 < static_cast< int >(rail.points.size())) && ImGui::Button("Insert after"))
                    insertPoint(rail, index, 0.5f);
                ImGui::SameLine();
                if (ImGui::Button("Delete point"))
                    removePoint(rail, row);
            }
            drawSection("Used by", "");
            for (int t = 0; t < m.tableCount(rail.zone); ++t)
                m.forEachRow(rail.zone, t, [&](RowId rider) {
                    const auto* c = m.cell(rider, columnHash("CommonPath_ID"));
                    if (exists(m, rider) && c && c->i == rail.id &&
                        ImGui::Selectable((displayName(m, rider) + "##rider" + std::to_string(rider.v)).c_str()))
                        Pick::selectRow(rider.v, Pick::Click::Replace);
                });
            if (!error.empty())
                ImGui::TextWrapped("%s", error.c_str());
            return;
        }
        const auto sockets = P::sockets(m, row);
        const char* name = m.objectName(row);
        const auto* info = name ? Catalog::find(name) : nullptr;
        bool path = m.cell(row, columnHash("CommonPath_ID")) != nullptr;
        if (info)
            for (const auto& l : info->links)
                path = path || l.kind == Catalog::LinkKind::Rail;
        if (sockets.empty() && !path)
            return;
        drawSection("Links", "drag sockets");
        if (!sockets.empty())
            wireLegend();
        for (const auto& s : sockets)
            socketButton(m, row, s.field.c_str(), s.role == Catalog::SwitchRole::Write);
        if (path)
            socketButton(m, row, "CommonPath_ID", false);
        if (!error.empty())
            ImGui::TextWrapped("%s", error.c_str());
    }
    bool drawPathsViewport(float x, float y, float width, float height) {
        const auto* m = model();
        if (!m || width <= 0 || height <= 0)
            return false;
        auto& io = ImGui::GetIO();
        if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_5, false))
            V::setVisibility(static_cast< V::Visibility >((static_cast< int >(V::visibility()) + 1) % 3));
        const auto& view = V::view();
        auto at = [&](float a, float b) { return ImVec2(x + (a + 1) * width * 0.5f, y + (1 - b) * height * 0.5f); };
        const bool inside = io.MousePos.x >= x && io.MousePos.y >= y && io.MousePos.x < x + width && io.MousePos.y < y + height &&
                            (!io.WantCaptureMouse || link.active || drag.active);
        bool consumed = link.active || drag.active || captureClick;
        auto* list = ImGui::GetForegroundDrawList();
        list->PushClipRect(ImVec2(x, y), ImVec2(x + width, y + height), true);
        const V::Mark* hit = nullptr;
        float nearest = 10;
        for (const auto& p : view.marks) {
            auto screen = at(p.x, p.y);
            if (p.handle)
                for (const auto& point : view.marks)
                    if (!point.handle && point.point == p.point) {
                        const auto center = at(point.x, point.y);
                        if (std::hypot(screen.x - center.x, screen.y - center.y) < 12) {
                            screen.x = center.x + (p.handle == 1 ? -24 : 24);
                            list->AddLine(center, screen, IM_COL32(100, 215, 255, 180), 1);
                        }
                        break;
                    }
            list->AddCircleFilled(screen, p.handle ? 4 : 5, p.handle ? IM_COL32(100, 215, 255, 240) : IM_COL32(250, 204, 56, 255));
            const float d = std::hypot(screen.x - io.MousePos.x, screen.y - io.MousePos.y);
            if (d < nearest) {
                nearest = d;
                hit = &p;
            }
        }
        RowId curveRail;
        int curveIndex = 0;
        float curveT = 0, curveDistance = 9;
        for (const auto& c : view.curves) {
            float t = 0;
            const float d = distance(io.MousePos, at(c.x0, c.y0), at(c.x1, c.y1), &t);
            if (d < curveDistance) {
                curveDistance = d;
                curveRail = c.rail;
                curveIndex = c.segment;
                curveT = c.t0 + (c.t1 - c.t0) * t;
            }
        }
        struct SocketGroup {
            RowId row;
            ImVec2 origin;
            int count = 0, index = 0;
        };
        std::vector< SocketGroup > groups;
        for (const auto& socket : view.sockets) {
            auto group = std::find_if(groups.begin(), groups.end(), [&](const SocketGroup& g) { return g.row == socket.row; });
            if (group == groups.end())
                groups.push_back({socket.row, at(socket.x, socket.y), 1, 0});
            else
                ++group->count;
        }
        for (std::size_t i = 0; i < groups.size(); ++i) {
            auto& group = groups[i];
            group.origin.x = std::clamp(group.origin.x + 35, x + 12, std::max(x + 12, x + width - 180));
            group.origin.y = std::clamp(group.origin.y, y + 12, std::max(y + 12, y + height - group.count * 20 - 12));
            for (std::size_t j = 0; j < i; ++j) {
                const auto& other = groups[j];
                if (std::fabs(group.origin.x - other.origin.x) < 180 && group.origin.y < other.origin.y + other.count * 20 &&
                    other.origin.y < group.origin.y + group.count * 20)
                    group.origin.x = other.origin.x > x + width / 2 ? other.origin.x - 185 : other.origin.x + 185;
            }
        }
        for (const auto& socket : view.sockets) {
            auto& group = *std::find_if(groups.begin(), groups.end(), [&](const SocketGroup& g) { return g.row == socket.row; });
            const ImVec2 p(group.origin.x, group.origin.y + group.index++ * 20);
            list->AddCircle(p, 6, socket.color, 16, 2);
            if (socket.output)
                list->AddCircleFilled(p, 3, socket.color);
            const std::string label = socket.field + (socket.output ? " out" : " in");
            list->AddText(ImVec2(p.x + 10, p.y - 7), socket.color, label.c_str());
            if (inside && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && std::hypot(p.x - io.MousePos.x, p.y - io.MousePos.y) < 10) {
                startLink(socket.row, socket.field, p, socket.output);
                consumed = true;
            }
        }
        if (std::none_of(view.wires.begin(), view.wires.end(),
                         [&](const V::WireMark& wire) { return wire.to.row == chosenWireRow && wire.to.field == chosenWireField; })) {
            chosenWireRow = {};
            chosenWireField.clear();
        }
        const V::WireMark* hoveredWire = nullptr;
        float wireDistance = 9;
        if (!link.active && !drag.active && inside && !hit && !io.KeyAlt) {
            for (const auto& wire : view.wires) {
                for (const auto& c : wire.segments) {
                    const float d = distance(io.MousePos, at(c.x0, c.y0), at(c.x1, c.y1));
                    if (d < wireDistance) {
                        wireDistance = d;
                        hoveredWire = &wire;
                    }
                }
            }
        }
        if (hoveredWire && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            chosenWireRow = hoveredWire->to.row;
            chosenWireField = hoveredWire->to.field;
            consumed = true;
        } else if (inside && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !link.active && !drag.active) {
            chosenWireRow = {};
            chosenWireField.clear();
        }
        if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            chosenWireRow = {};
        V::focusWires(hoveredWire ? hoveredWire->to.row : RowId{}, hoveredWire ? hoveredWire->to.field : "", chosenWireRow, chosenWireField);
        if (!link.active && !drag.active && inside && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !consumed) {
            if (hit && !io.KeyAlt) {
                drag = {true, *hit};
                Pick::selectRow(hit->point.v, Pick::Click::Replace);
                V::send(
                    {true, false, false, false, hit->point, hit->handle, (io.MousePos.x - x) / width * 2 - 1, 1 - (io.MousePos.y - y) / height * 2});
                consumed = true;
            } else if (curveRail.valid()) {
                if (io.KeyAlt) {
                    const auto rails = P::collect(*m);
                    if (const auto* r = findRail(rails, curveRail))
                        insertPoint(*r, curveIndex, curveT);
                } else
                    Pick::selectRow(curveRail.v, Pick::Click::Replace);
                consumed = true;
            }
        }
        if (drag.active) {
            const bool cancel = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
            const bool released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
            if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                V::send({false, true, released, cancel, drag.mark.point, drag.mark.handle, (io.MousePos.x - x) / width * 2 - 1,
                         1 - (io.MousePos.y - y) / height * 2});
            if (cancel || released)
                drag.active = false;
            consumed = true;
        }
        if (link.active) {
            list->AddLine(link.start, io.MousePos, IM_COL32(100, 215, 255, 255), 2);
            if (std::hypot(io.MousePos.x - link.start.x, io.MousePos.y - link.start.y) > 4)
                link.moved = true;
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                finishLink();
            else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                if (inside) {
                    if (link.field == "CommonPath_ID") {
                        const auto rails = P::collect(*m);
                        const auto* rail = findRail(rails, hit ? hit->rail : curveRail);
                        if (rail)
                            submit(P::linkPath(*m, link.row, *rail, &error));
                        else
                            error = "Drop the socket onto a path.";
                    } else {
                        Pick::Item hover;
                        if (Pick::hover(&hover) && hover.row)
                            chooseTarget(*m, {hover.row});
                        else
                            error = "Drop the socket onto an object.";
                    }
                }
                finishLink();
            }
            consumed = true;
        }
        if (chosenWireRow.valid())
            list->AddText(ImVec2(x + 12, y + 12), IM_COL32(250, 204, 56, 255), "Wire selected - Delete unlinks the receiver");
        if (!error.empty())
            list->AddText(ImVec2(x + 12, y + 34), IM_COL32(255, 180, 120, 255), error.c_str());
        list->PopClipRect();
        if (reactionPopup) {
            ImGui::OpenPopup("Choose switch reaction");
            reactionPopup = false;
        }
        if (ImGui::BeginPopup("Choose switch reaction")) {
            for (const auto& socket : P::sockets(*m, targetRow))
                if (socket.role != sourceSocket.role) {
                    if (ImGui::Selectable(socket.field.c_str())) {
                        submit(P::linkSwitchOp(*m, sourceSocket, socket, &error));
                        ImGui::CloseCurrentPopup();
                    }
                }
            ImGui::EndPopup();
            consumed = true;
        }
        if (consumed && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            captureClick = true;
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            captureClick = false;
        return consumed;
    }
    void drawPathsWireOverlay(float x, float y, float width, float height) {
        if (!model() || width <= 0 || height <= 0)
            return;
        const auto& view = V::view();
        auto at = [&](float a, float b) { return ImVec2(x + (a + 1) * width * 0.5f, y + (1 - b) * height * 0.5f); };
        auto* list = ImGui::GetForegroundDrawList();
        list->PushClipRect(ImVec2(x, y), ImVec2(x + width, y + height), true);
        for (const auto& wire : view.wires) {
            const bool emphasized = V::wireFocused(wire.to);
            auto dot = [&](float px, float py, const std::string& field) {
                const auto center = at(px, py);
                const float radius = emphasized ? 6 : 5;
                list->AddCircleFilled(center, radius + 2, IM_COL32(17, 25, 34, 245));
                list->AddCircleFilled(center, radius, P::switchColor(field));
                list->AddCircle(center, radius, IM_COL32(255, 255, 255, 230), 20, 1);
            };
            if (wire.fromVisible)
                dot(wire.fromX, wire.fromY, wire.from.field);
            if (wire.toVisible)
                dot(wire.toX, wire.toY, wire.to.field);
            if (emphasized && wire.midpointVisible) {
                const std::string label =
                    wire.from.field + " -> " + wire.to.field + "  #" + std::to_string(wire.from.id) + (wire.from.id >= 1000 ? " (galaxy)" : "");
                const auto size = ImGui::CalcTextSize(label.c_str());
                auto center = at(wire.x, wire.y);
                const ImVec2 origin(std::clamp(center.x - size.x / 2 - 7, x + 4, std::max(x + 4, x + width - size.x - 18)),
                                    std::clamp(center.y - size.y - 17, y + 4, std::max(y + 4, y + height - size.y - 14)));
                const ImVec2 end(origin.x + size.x + 14, origin.y + size.y + 10);
                list->AddRectFilled(origin, end, IM_COL32(17, 25, 34, 245), 5);
                list->AddRect(origin, end, P::switchColor(wire.to.field), 5);
                list->AddText(ImVec2(origin.x + 7, origin.y + 5), IM_COL32(245, 248, 255, 255), label.c_str());
            }
        }
        list->PopClipRect();
    }
}  // namespace PetariEditor::Ui
