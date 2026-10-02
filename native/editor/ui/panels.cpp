// The level editor's M1 panels (panels.hpp).
#include "panels.hpp"

#include <petari/editor_pick.hpp>
#include <petari/home_menu.hpp>

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace PetariEditor::Ui {
namespace {

namespace Pick = PetariNative::EditorPick;

const ImVec4 kGold(0.98f, 0.80f, 0.22f, 1.0f);
const ImVec4 kGreen(0.35f, 0.85f, 0.45f, 1.0f);
const ImVec4 kRed(1.0f, 0.35f, 0.35f, 1.0f);
const ImVec4 kDim(0.50f, 0.66f, 0.79f, 1.0f);
const ImVec4 kHeadInk(0.60f, 0.92f, 1.0f, 1.0f);

bool isTransformColumn(const char* name) {
    if (name == nullptr) return false;
    return std::strncmp(name, "pos_", 4) == 0 || std::strncmp(name, "dir_", 4) == 0 || std::strncmp(name, "scale_", 6) == 0;
}

const char* stateMark(RowState state) {
    switch (state) {
    case RowState::Added: return "+";
    case RowState::Changed: return "*";
    case RowState::Deleted: return "-";
    default: return " ";
    }
}

ImVec4 stateColor(RowState state) {
    switch (state) {
    case RowState::Added: return kGreen;
    case RowState::Changed: return kGold;
    case RowState::Deleted: return kRed;
    default: return kDim;
    }
}

// A section header: the Home menu table header band (UX section 4 "The look").
void section(const char* title, const char* right = nullptr) {
    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetTextLineHeight() + 6.0f;
    list->AddRectFilled(ImVec2(min.x - 8.0f, min.y), ImVec2(min.x + width + 8.0f, min.y + height),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(0.35f, 0.80f, 1.0f, 0.22f)));
    ImGui::SetCursorScreenPos(ImVec2(min.x, min.y + 3.0f));
    ImGui::TextColored(kHeadInk, "%s", title);
    if (right != nullptr) {
        ImGui::SameLine(width - ImGui::CalcTextSize(right).x);
        ImGui::TextColored(kDim, "%s", right);
    }
    ImGui::SetCursorScreenPos(ImVec2(min.x, min.y + height + 3.0f));
}

std::string rowName(const ModelView& model, RowId row) {
    const char* name = model.objectName(row);
    if (name != nullptr && name[0] != '\0') return name;
    int zone = 0, table = 0;
    if (model.locate(row, &zone, &table)) {
        const char* path = model.tablePath(zone, table);
        const char* last = std::strrchr(path, '/');
        return last != nullptr ? last + 1 : path;
    }
    return "row";
}

void enqueueOp(Op op) {
    if (op.sets.empty() && op.revertRows.empty()) return;
    Command command{Command::Kind::Apply};
    command.op = std::move(op);
    enqueue(std::move(command));
}

// ---- Outliner -------------------------------------------------------------------------------------

void drawOutliner(const ModelView& model, const std::vector< RowId >& selected) {
    for (int zone = 0; zone < model.zoneCount(); zone++) {
        if (!model.zonePlaced(zone)) continue;
        ImGui::PushID(zone);
        const bool open = ImGui::TreeNodeEx("zone", 0, "%s", model.zoneName(zone));
        if (open) {
            for (int table = 0; table < model.tableCount(zone); table++) {
                // Rail points belong to their rail (a later Paths view), not to the object list.
                if (std::strstr(model.tablePath(zone, table), "pointinfo") != nullptr) continue;
                int rows = 0;
                model.forEachRow(zone, table, [&](RowId) { rows++; });
                if (rows == 0) continue;
                ImGui::PushID(table);
                const char* path = model.tablePath(zone, table);
                if (ImGui::TreeNode("table", "%s  (%d)", path, rows)) {
                    model.forEachRow(zone, table, [&](RowId row) {
                        ImGui::PushID(static_cast< int >(row.v & 0x7FFFFFFF));
                        const bool isSelected = std::find(selected.begin(), selected.end(), row) != selected.end();
                        const RowState state = model.state(row);
                        ImGui::TextColored(stateColor(state), "%s", stateMark(state));
                        ImGui::SameLine();
                        if (ImGui::Selectable(rowName(model, row).c_str(), isSelected)) {
                            Pick::selectRow(row.v, ImGui::GetIO().KeyShift ? Pick::Click::Add : Pick::Click::Replace);
                        }
                        ImGui::PopID();
                    });
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

// ---- Changes --------------------------------------------------------------------------------------

void drawChanges(const ModelView& model) {
    const std::vector< Change > changes = collectChanges(model);
    if (changes.empty()) {
        ImGui::TextDisabled("No changes: the galaxy is as on the disc.");
        return;
    }
    char count[48];
    std::snprintf(count, sizeof(count), "%d change%s", static_cast< int >(changes.size()), changes.size() == 1 ? "" : "s");
    int zone = -1;
    for (const Change& change : changes) {
        if (change.zone != zone) {
            zone = change.zone;
            section(model.zoneName(zone), zone == changes.front().zone ? count : nullptr);
        }
        ImGui::PushID(static_cast< int >(change.row.v & 0x7FFFFFFF));
        ImGui::TextColored(stateColor(change.state), "%s", stateMark(change.state));
        ImGui::SameLine();
        if (ImGui::Selectable(change.name.c_str(), false, ImGuiSelectableFlags_AllowOverlap, ImVec2(ImGui::GetContentRegionAvail().x - 60.0f, 0))) {
            Pick::selectRow(change.row.v, Pick::Click::Replace);
        }
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 56.0f);
        if (ImGui::SmallButton("Revert")) enqueueOp(revertRowsOp(model, {change.row}));
        if (!change.columns.empty()) {
            std::string fields;
            for (const std::string& c : change.columns) fields += (fields.empty() ? "" : ", ") + c;
            ImGui::Indent(18.0f);
            ImGui::TextColored(kDim, "%s", fields.c_str());
            ImGui::Unindent(18.0f);
        }
        ImGui::PopID();
    }
    ImGui::Spacing();
    if (ImGui::Button("Revert all")) {
        std::vector< RowId > rows;
        for (const Change& change : changes) rows.push_back(change.row);
        enqueueOp(revertRowsOp(model, rows));
    }
}

// ---- History --------------------------------------------------------------------------------------

void drawHistory(const ModelView& model) {
    const int size = model.historySize(), cursor = model.historyCursor();
    ImGui::BeginDisabled(!canUndo());
    if (ImGui::Button("Undo")) undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!canRedo());
    if (ImGui::Button("Redo")) redo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextColored(kDim, "Cmd+Z / Cmd+Shift+Z");
    ImGui::Separator();
    if (ImGui::Selectable("(as loaded)", cursor == 0)) {
        Command command{Command::Kind::JumpHistory};
        command.index = 0;
        enqueue(std::move(command));
    }
    for (int i = 0; i < size; i++) {
        ImGui::PushID(i);
        const bool undone = i >= cursor;
        if (undone) ImGui::PushStyleColor(ImGuiCol_Text, kDim);
        if (ImGui::Selectable(model.historyLabel(i), i == cursor - 1)) {
            Command command{Command::Kind::JumpHistory};
            command.index = i + 1;  // the cursor after this step
            enqueue(std::move(command));
        }
        if (undone) ImGui::PopStyleColor();
        ImGui::PopID();
    }
}

// ---- Inspector ------------------------------------------------------------------------------------

std::string gHelp;  // the help strip: the hovered field

void noteHelp(const Column& column, const ModelView& model, RowId row) {
    if (!ImGui::IsItemHovered()) return;
    const Cell* original = model.originalCell(row, column.hash);
    gHelp = columnLabel(column) + (original != nullptr ? "  (disc: " + cellText(*original) + ")" : "  (added)");
}

// One editable field; edits go to every selected row that has the column.
void field(const ModelView& model, const std::vector< RowId >& rows, const Column& column) {
    const RowId primary = rows.front();
    const Cell* current = model.cell(primary, column.hash);
    if (current == nullptr) return;
    bool mixed = false;
    for (RowId row : rows) {
        const Cell* other = model.cell(row, column.hash);
        mixed = mixed || (other != nullptr && !sameCell(*other, *current));
    }
    const Cell* original = model.originalCell(primary, column.hash);
    const bool changed = original != nullptr && !sameCell(*original, *current);
    const std::string label = columnLabel(column);
    ImGui::PushID(static_cast< int >(column.hash));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label.c_str());
    noteHelp(column, model, primary);
    ImGui::SameLine(120.0f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 18.0f);
    Cell edited = *current;
    bool commit = false;
    switch (current->type) {
    case CellType::Float:
        ImGui::InputFloat("##v", &edited.f, 0.0f, 0.0f, mixed ? "Mixed" : "%.3f");
        commit = ImGui::IsItemDeactivatedAfterEdit();
        break;
    case CellType::String:
    case CellType::StringOffset: {
        char buffer[128];
        std::snprintf(buffer, sizeof(buffer), "%s", current->s.c_str());
        ImGui::InputText("##v", buffer, sizeof(buffer));
        commit = ImGui::IsItemDeactivatedAfterEdit();
        edited.s = buffer;
        break;
    }
    default:
        ImGui::InputInt("##v", &edited.i, 0, 0);
        commit = ImGui::IsItemDeactivatedAfterEdit();
        break;
    }
    noteHelp(column, model, primary);
    if (commit && (!sameCell(edited, *current) || mixed)) enqueueOp(setCellOp(model, rows, column.hash, edited));
    ImGui::SameLine();
    if (changed) {
        // The gold dot: changed from the disc; click to put it back (UX section 8).
        const ImVec2 at = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("revert", ImVec2(12.0f, ImGui::GetFrameHeight()))) enqueueOp(revertCellOp(model, rows, column.hash));
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(at.x + 6.0f, at.y + ImGui::GetFrameHeight() * 0.5f), 4.0f,
                                                    ImGui::ColorConvertFloat4ToU32(kGold));
        if (ImGui::IsItemHovered()) gHelp = label + ": changed from the disc's " + cellText(*original) + ". Click to put it back.";
    } else {
        ImGui::Dummy(ImVec2(12.0f, 1.0f));
    }
    ImGui::PopID();
}

// Position, rotation and scale as three-number rows: dragging previews, letting go records one op.
void transformRow(const ModelView& model, const std::vector< RowId >& rows, const char* label, const char* prefix, float speed) {
    const RowId primary = rows.front();
    float values[3];
    std::uint32_t hashes[3];
    const char axes[3] = {'x', 'y', 'z'};
    for (int i = 0; i < 3; i++) {
        char name[16];
        std::snprintf(name, sizeof(name), "%s%c", prefix, axes[i]);
        hashes[i] = columnHash(name);
        const Cell* cell = model.cell(primary, hashes[i]);
        values[i] = cell != nullptr ? cell->f : 0.0f;
    }
    ImGui::PushID(prefix);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(120.0f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 18.0f);
    const float before[3] = {values[0], values[1], values[2]};
    const bool changedNow = ImGui::DragFloat3("##v", values, speed, 0.0f, 0.0f, "%.1f");
    if (changedNow && rows.size() == 1 && model.hasTransform(primary)) {
        // Live preview while scrubbing (not recorded).
        RowTransform t = model.transform(primary);
        Vec3* target = prefix[0] == 'p' ? &t.pos : prefix[0] == 'd' ? &t.dirDeg : &t.scale;
        target->x = values[0];
        target->y = values[1];
        target->z = values[2];
        Command command{Command::Kind::Preview};
        command.rows = {primary};
        command.previews = {t};
        enqueue(std::move(command));
    }
    static float sStart[3];
    if (ImGui::IsItemActivated()) std::memcpy(sStart, before, sizeof(sStart));
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        Op op;
        for (RowId row : rows) {
            for (int i = 0; i < 3; i++) {
                const Cell* cell = model.cell(row, hashes[i]);
                const float delta = values[i] - sStart[i];
                Cell value;
                value.type = CellType::Float;
                // One object: the typed value; several: the same change applied to each.
                value.f = rows.size() == 1 ? values[i] : (cell != nullptr ? cell->f : 0.0f) + delta;
                op.sets.push_back({row, hashes[i], value});
            }
        }
        char text[96];
        std::snprintf(text, sizeof(text), "Set %s of %s", label, rows.size() == 1 ? rowName(model, primary).c_str() : "several objects");
        op.label = text;
        enqueueOp(std::move(op));
    }
    if (ImGui::IsItemHovered()) gHelp = std::string(label) + ": " + prefix + "x, " + prefix + "y, " + prefix + "z (drag to scrub, double-click to type)";
    ImGui::PopID();
}

}  // namespace

// ---- logic ----------------------------------------------------------------------------------------

bool sameCell(const Cell& a, const Cell& b) {
    if (a.type != b.type) return false;
    switch (a.type) {
    case CellType::Float: return a.f == b.f;
    case CellType::String:
    case CellType::StringOffset: return a.s == b.s;
    default: return a.i == b.i;
    }
}

std::string cellText(const Cell& cell) {
    char text[64];
    switch (cell.type) {
    case CellType::Float: std::snprintf(text, sizeof(text), "%g", cell.f); return text;
    case CellType::String:
    case CellType::StringOffset: return "\"" + cell.s + "\"";
    default: std::snprintf(text, sizeof(text), "%d", cell.i); return text;
    }
}

std::string columnLabel(const Column& column) {
    if (column.name != nullptr && column.name[0] != '\0') return column.name;
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08x", column.hash);
    return text;
}

Op setCellOp(const ModelView& model, const std::vector< RowId >& rows, std::uint32_t column, const Cell& value) {
    Op op;
    for (RowId row : rows) {
        if (model.cell(row, column) != nullptr) op.sets.push_back({row, column, value});
    }
    std::string name = "field";
    if (!rows.empty()) {
        int zone = 0, table = 0;
        if (model.locate(rows.front(), &zone, &table)) {
            for (const Column& c : model.columns(zone, table)) {
                if (c.hash == column) name = columnLabel(c);
            }
        }
    }
    op.label = "Set " + name + (op.sets.size() == 1 ? " of " + rowName(model, rows.front()) : " on " + std::to_string(op.sets.size()) + " objects");
    return op;
}

Op revertCellOp(const ModelView& model, const std::vector< RowId >& rows, std::uint32_t column) {
    Op op;
    for (RowId row : rows) {
        const Cell* original = model.originalCell(row, column);
        if (original != nullptr) op.sets.push_back({row, column, *original});
    }
    op.label = op.sets.size() == 1 ? "Reverted a field of " + rowName(model, op.sets.front().row) : "Reverted a field";
    return op;
}

Op revertRowsOp(const ModelView& model, const std::vector< RowId >& rows) {
    Op op;
    op.revertRows = rows;
    op.label = rows.size() == 1 ? "Reverted " + rowName(model, rows.front()) : "Reverted " + std::to_string(rows.size()) + " objects";
    return op;
}

std::vector< Change > collectChanges(const ModelView& model) {
    std::vector< Change > changes;
    for (int zone = 0; zone < model.zoneCount(); zone++) {
        for (int table = 0; table < model.tableCount(zone); table++) {
            const std::vector< Column >& columns = model.columns(zone, table);
            model.forEachRow(zone, table, [&](RowId row) {
                const RowState state = model.state(row);
                if (state == RowState::Original) return;
                Change change;
                change.row = row;
                change.state = state;
                change.zone = zone;
                change.name = rowName(model, row);
                if (state == RowState::Changed) {
                    bool position = false, rotation = false, scale = false;
                    for (const Column& column : columns) {
                        const Cell* now = model.cell(row, column.hash);
                        const Cell* was = model.originalCell(row, column.hash);
                        if (now == nullptr || (was != nullptr && sameCell(*now, *was))) continue;
                        // pos_x/y/z read as "Position", and so on.
                        if (column.name != nullptr && std::strncmp(column.name, "pos_", 4) == 0) {
                            if (!position) change.columns.push_back("Position");
                            position = true;
                        } else if (column.name != nullptr && std::strncmp(column.name, "dir_", 4) == 0) {
                            if (!rotation) change.columns.push_back("Rotation");
                            rotation = true;
                        } else if (column.name != nullptr && std::strncmp(column.name, "scale_", 6) == 0) {
                            if (!scale) change.columns.push_back("Scale");
                            scale = true;
                        } else {
                            change.columns.push_back(columnLabel(column));
                        }
                    }
                }
                changes.push_back(std::move(change));
            });
        }
    }
    return changes;
}

std::vector< RowId > selectedRows() {
    std::vector< RowId > rows;
    for (const Pick::Item& item : Pick::selection()) {
        if (item.row != 0) rows.push_back(RowId{item.row});
    }
    return rows;
}

bool canUndo() {
    const ModelView* model = PetariEditor::model();
    return model != nullptr && model->historyCursor() > 0;
}

bool canRedo() {
    const ModelView* model = PetariEditor::model();
    return model != nullptr && model->historyCursor() < model->historySize();
}

void undo() {
    if (canUndo()) enqueue(Command{Command::Kind::Undo});
}

void redo() {
    if (canRedo()) enqueue(Command{Command::Kind::Redo});
}

// ---- windows --------------------------------------------------------------------------------------

void drawGalaxyWindow(const char* title) {
    if (ImGui::Begin(title)) {
        const ModelView* model = PetariEditor::model();
        if (model == nullptr) {
            ImGui::TextDisabled("Loading the galaxy...");
        } else if (ImGui::BeginTabBar("galaxy")) {
            if (ImGui::BeginTabItem("Outliner")) {
                ImGui::TextColored(kDim, "%s  ·  mission %d", model->galaxy(), model->scenario());
                drawOutliner(*model, selectedRows());
                ImGui::EndTabItem();
            }
            const int changeCount = static_cast< int >(collectChanges(*model).size());
            char changesLabel[32];
            std::snprintf(changesLabel, sizeof(changesLabel), changeCount > 0 ? "Changes (%d)###changes" : "Changes###changes", changeCount);
            if (ImGui::BeginTabItem(changesLabel)) {
                drawChanges(*model);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("History")) {
                drawHistory(*model);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void drawInspectorWindow(const char* title) {
    if (ImGui::Begin(title)) {
        gHelp.clear();
        const ModelView* model = PetariEditor::model();
        const std::vector< Pick::Item > picked = Pick::selection();
        const std::vector< RowId > rows = selectedRows();
        if (picked.empty()) {
            ImGui::TextDisabled("Select something to see its settings.");
        } else if (model == nullptr || rows.empty()) {
            ImGui::Text("%s", picked.front().name);
            ImGui::TextWrapped("This object is not in the galaxy's tables (another object made it), so it has no settings of its own.");
        } else {
            const RowId primary = rows.front();
            int zone = 0, table = 0;
            model->locate(primary, &zone, &table);
            // Header: name, where it lives, what state it is in.
            if (ImFont* galaxy = PetariNative::HomeMenu::galaxyTextFont()) {
                ImGui::PushFont(galaxy);
                ImGui::SetWindowFontScale(17.0f / galaxy->FontSize);
            }
            if (rows.size() == 1) {
                ImGui::TextUnformatted(rowName(*model, primary).c_str());
            } else {
                ImGui::Text("%d objects", static_cast< int >(rows.size()));
            }
            if (PetariNative::HomeMenu::galaxyTextFont() != nullptr) {
                ImGui::SetWindowFontScale(1.0f);
                ImGui::PopFont();
            }
            ImGui::TextColored(kDim, "%s  ·  %s", model->zoneName(zone), model->tablePath(zone, table));
            const RowState state = model->state(primary);
            if (state != RowState::Original) {
                ImGui::SameLine();
                ImGui::TextColored(stateColor(state), state == RowState::Added ? "added" : state == RowState::Deleted ? "deleted" : "changed");
            }
            if (model->hasTransform(primary)) {
                section("Transform", "row space");
                transformRow(*model, rows, "Position", "pos_", 1.0f);
                transformRow(*model, rows, "Rotation", "dir_", 0.5f);
                transformRow(*model, rows, "Scale", "scale_", 0.01f);
            }
            section("Fields", rows.size() > 1 ? "edits apply to all" : "raw");
            for (const Column& column : model->columns(zone, table)) {
                if (isTransformColumn(column.name)) continue;
                field(*model, rows, column);
            }
        }
        // The help strip at the foot: the hovered field (UX section 8).
        const float strip = ImGui::GetTextLineHeightWithSpacing() * 2.5f;
        if (ImGui::GetContentRegionAvail().y > strip) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetContentRegionAvail().y - strip);
        ImGui::Separator();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kDim, "%s", gHelp.empty() ? "Hover a field for its raw name and disc value. Gold dots mark changes; click one to put it back." : gHelp.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
}

}  // namespace PetariEditor::Ui
