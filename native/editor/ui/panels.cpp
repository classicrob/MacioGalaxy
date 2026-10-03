#include "areas.hpp"
// The level editor's M1 panels (panels.hpp).
#include "panels.hpp"
#include "gravity.hpp"
#include "paths.hpp"

#include <petari/editor/ui.hpp>

#include <petari/editor/catalog.hpp>
#include <petari/editor_pick.hpp>
#include <petari/editor_settings.hpp>
#include <petari/home_menu.hpp>

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <map>

namespace PetariEditor::Ui {
namespace {

namespace Pick = PetariNative::EditorPick;
namespace Settings = PetariNative::EditorSettings;

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
    if (name != nullptr && name[0] != '\0') {
        // The catalog's name for the class ("Goomba" for Kuribo), else the placement name.
        const Catalog::ClassInfo* info = Catalog::find(name);
        return info != nullptr && !info->displayName.empty() ? info->displayName : std::string(name);
    }
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

// The kinds the Outliner groups rows into (UX section 4: by zone, then by kind; never raw tables).
enum class Kind { Objects, Planets, Platforms, Gravity, Areas, Cameras, Paths, Starts, Sounds, Cutscenes, Positions, Zones, Other, Count };
const char* const kKindNames[] = {"Objects", "Planets", "Platforms", "Gravity", "Areas", "Cameras", "Paths",
                                  "Starts", "Sounds", "Cutscenes", "Positions", "Zones", "Other"};

// The table's kind from its path ("jmp/placement/layera/areaobjinfo"); rail points are not a kind (they
// fold under their path), nor are the stage's lists.
bool kindOfTable(const char* path, Kind* kind) {
    auto has = [&](const char* part) { return std::strstr(path, part) != nullptr; };
    if (has("pointinfo") || has("jmp/list/") || has("jmp/debug/")) return false;
    if (has("planetobjinfo")) *kind = Kind::Gravity;
    else if (has("areaobjinfo")) *kind = Kind::Areas;
    else if (has("cameracubeinfo")) *kind = Kind::Cameras;
    else if (has("mappartsinfo")) *kind = Kind::Platforms;
    else if (has("commonpathinfo")) *kind = Kind::Paths;
    else if (has("startinfo")) *kind = Kind::Starts;
    else if (has("soundinfo")) *kind = Kind::Sounds;
    else if (has("demoobjinfo")) *kind = Kind::Cutscenes;
    else if (has("generalposinfo")) *kind = Kind::Positions;
    else if (has("stageobjinfo")) *kind = Kind::Zones;
    else if (has("objinfo")) *kind = Kind::Objects;  // objinfo, childobjinfo
    else *kind = Kind::Other;
    return true;
}

// "layera" in the path -> "A" (common rows have no tag).
const char* layerTag(const char* path) {
    const char* at = std::strstr(path, "/layer");
    static char tag[2] = {};
    if (at == nullptr || at[6] == '\0') return nullptr;
    tag[0] = static_cast< char >(std::toupper(static_cast< unsigned char >(at[6])));
    return tag;
}

bool gRawTables = false;  // Advanced: the archive's tables as they are (and the layers, UX section 12)

void outlinerRow(const ModelView& model, RowId row, const std::vector< RowId >& selected, const char* layer, const char* suffix = nullptr) {
    ImGui::PushID(static_cast< int >(row.v & 0x7FFFFFFF));
    const RowState state = model.state(row);
    ImGui::TextColored(stateColor(state), "%s", stateMark(state));
    ImGui::SameLine();
    std::string label = rowName(model, row);
    if (suffix != nullptr) label += suffix;
    const bool isSelected = std::find(selected.begin(), selected.end(), row) != selected.end();
    if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_AllowOverlap)) {
        Pick::selectRow(row.v, ImGui::GetIO().KeyShift ? Pick::Click::Add : Pick::Click::Replace);
    }
    if (layer != nullptr && gRawTables) {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 18.0f);
        ImGui::TextColored(kDim, "%s", layer);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("In layer %s (missions come with the mission chips)", layer);
    }
    ImGui::PopID();
}

// Paths, each with its points folded under it (commonpathpointinfo.<n> belongs to the n-th path).
void drawPaths(const ModelView& model, int zone, int pathTable, const std::vector< RowId >& selected) {
    int index = 0;
    model.forEachRow(zone, pathTable, [&](RowId path) {
        char pointsPath[64];
        std::snprintf(pointsPath, sizeof(pointsPath), "jmp/path/commonpathpointinfo.%d", index++);
        int pointsTable = -1, points = 0;
        for (int t = 0; t < model.tableCount(zone); t++) {
            if (std::strcmp(model.tablePath(zone, t), pointsPath) == 0) pointsTable = t;
        }
        if (pointsTable >= 0) model.forEachRow(zone, pointsTable, [&](RowId) { points++; });
        ImGui::PushID(static_cast< int >(path.v & 0x7FFFFFFF));
        char label[96];
        std::snprintf(label, sizeof(label), "%s  (%d points)", rowName(model, path).c_str(), points);
        const bool open = ImGui::TreeNodeEx("path", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", label);
        if (ImGui::IsItemClicked()) Pick::selectRow(path.v, Pick::Click::Replace);
        if (open) {
            if (pointsTable >= 0) {
                int n = 0;
                model.forEachRow(zone, pointsTable, [&](RowId point) {
                    char suffix[24];
                    std::snprintf(suffix, sizeof(suffix), " %d", n++);
                    outlinerRow(model, point, selected, nullptr, suffix);
                });
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    });
}


void drawRawTables(const ModelView& model, int zone, const std::vector< RowId >& selected) {
    for (int table = 0; table < model.tableCount(zone); table++) {
        int rows = 0;
        model.forEachRow(zone, table, [&](RowId) { rows++; });
        if (rows == 0) continue;
        ImGui::PushID(table);
        if (ImGui::TreeNode("table", "%s  (%d)", model.tablePath(zone, table), rows)) {
            model.forEachRow(zone, table, [&](RowId row) { outlinerRow(model, row, selected, nullptr); });
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

std::string gOutlinerQuery;

// The Outliner searched: every placed row whose name (display or class) contains the query, by zone.
void drawOutlinerMatches(const ModelView& model, const std::vector< RowId >& selected) {
    int shown = 0;
    for (int zone = 0; zone < model.zoneCount() && shown < 300; zone++) {
        if (!model.zonePlaced(zone)) continue;
        bool header = false;
        for (int table = 0; table < model.tableCount(zone) && shown < 300; table++) {
            const char* path = model.tablePath(zone, table);
            Kind kind;
            if (!kindOfTable(path, &kind) || kind == Kind::Paths) continue;
            model.forEachRow(zone, table, [&](RowId row) {
                const char* name = model.objectName(row);
                if (shown >= 300 || (!matchesQuery(rowName(model, row), gOutlinerQuery) && !matchesQuery(name != nullptr ? name : "", gOutlinerQuery))) return;
                if (!header) {
                    header = true;
                    ImGui::TextColored(kDim, "%s", model.zoneName(zone));
                }
                ImGui::PushID(zone);
                outlinerRow(model, row, selected, layerTag(path));
                ImGui::PopID();
                shown++;
            });
        }
    }
    if (shown == 0) ImGui::TextDisabled("Nothing matches.");
    if (shown >= 300) ImGui::TextDisabled("First 300 shown; type more to narrow it.");
}

void drawOutliner(const ModelView& model, const std::vector< RowId >& selected) {
    // Search (names and classes: "hard" finds the HardBlocks), then the tree in its own scroll region, so the
    // tabs above stay put.
    char query[64];
    std::snprintf(query, sizeof(query), "%s", gOutlinerQuery.c_str());
    ImGui::SetNextItemWidth(-90.0f);
    if (ImGui::InputTextWithHint("##find", "Find (name or class)", query, sizeof(query))) gOutlinerQuery = query;
    noteTestTarget("field:Find");
    ImGui::SameLine();
    ImGui::Checkbox("Raw", &gRawTables);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Advanced: the zone archives' tables as they are on the disc");
    ImGui::BeginChild("outliner-list", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
    if (!gOutlinerQuery.empty() && !gRawTables) {
        drawOutlinerMatches(model, selected);
        ImGui::EndChild();
        return;
    }
    for (int zone = 0; zone < model.zoneCount(); zone++) {
        if (!model.zonePlaced(zone)) continue;
        ImGui::PushID(zone);
        // Open the zone that holds the selection.
        bool holdsSelection = false;
        for (RowId row : selected) {
            int z = -1, t = 0;
            holdsSelection = holdsSelection || (model.locate(row, &z, &t) && z == zone);
        }
        if (holdsSelection) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        const bool open = ImGui::TreeNodeEx("zone", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", model.zoneName(zone));
        if (open) {
            if (gRawTables) {
                drawRawTables(model, zone, selected);
            } else {
                // Rows by kind, across the zone's layers.
                std::vector< std::pair< RowId, const char* > > byKind[static_cast< int >(Kind::Count)];
                int pathTable = -1;
                for (int table = 0; table < model.tableCount(zone); table++) {
                    const char* path = model.tablePath(zone, table);
                    Kind kind;
                    if (!kindOfTable(path, &kind)) continue;
                    if (kind == Kind::Paths) {
                        pathTable = table;
                        continue;
                    }
                    model.forEachRow(zone, table, [&](RowId row) {
                        Kind k = kind;
                        // Planets are objects whose name says so (the catalog's categories replace this).
                        const char* name = model.objectName(row);
                        if (k == Kind::Objects && name != nullptr && std::strstr(name, "Planet") != nullptr) k = Kind::Planets;
                        byKind[static_cast< int >(k)].push_back({row, layerTag(path)});
                    });
                }
                for (int k = 0; k < static_cast< int >(Kind::Count); k++) {
                    int count = static_cast< int >(byKind[k].size());
                    if (k == static_cast< int >(Kind::Paths) && pathTable >= 0) model.forEachRow(zone, pathTable, [&](RowId) { count++; });
                    if (count == 0) continue;
                    ImGui::PushID(k);
                    bool kindHoldsSelection = false;
                    for (const auto& [row, layer] : byKind[k]) {
                        kindHoldsSelection = kindHoldsSelection || std::find(selected.begin(), selected.end(), row) != selected.end();
                    }
                    if (kindHoldsSelection) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                    if (ImGui::TreeNodeEx("kind", ImGuiTreeNodeFlags_SpanAvailWidth, "%s  (%d)", kKindNames[k], count)) {
                        if (k == static_cast< int >(Kind::Paths)) {
                            drawPaths(model, zone, pathTable, selected);
                        } else {
                            std::stable_sort(byKind[k].begin(), byKind[k].end(), [&](const auto& a, const auto& b) {
                                return rowName(model, a.first) < rowName(model, b.first);
                            });
                            for (const auto& [row, layer] : byKind[k]) outlinerRow(model, row, selected, layer);
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
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

// ---- Checks (UX section 16) -----------------------------------------------------------------------

std::string gHelp;  // the Inspector's help strip: the hovered field
std::string gHint;  // a Checks fix's instructions, shown in the help strip for a while
int gHintFrames = 0;
std::string gOpenTab;          // a tab to bring forward next frame (the toolbar's checks badge)
std::uint64_t gFrameRow = 0;   // frame the camera on this row once the game has selected it
int gFrameWait = 0;

// Select a check's rows (the first one primary) and frame them when the selection lands.
// Logs the Galaxy window's tab when it changes (input tests read it; it also explains a session's log).
void noteTab(const char* name) {
    static std::string last;
    if (last == name) return;
    last = name;
    std::fprintf(stderr, "[editor] tab %s\n", name);
}

void showRows(const std::vector< RowId >& rows) {
    if (rows.empty()) return;
    for (size_t i = 0; i < rows.size(); i++) Pick::selectRow(rows[i].v, i == 0 ? Pick::Click::Replace : Pick::Click::Add);
    gFrameRow = rows.front().v;
    gFrameWait = 30;
}

void serviceFraming() {
    if (gFrameRow == 0) return;
    const std::vector< RowId > rows = selectedRows();
    if (!rows.empty() && rows.front().v == gFrameRow) {
        Settings::frame();
        gFrameRow = 0;
    } else if (--gFrameWait <= 0) {
        gFrameRow = 0;  // not an object in the scene (a path point, another mission's): selected in the lists only
    }
}

bool emptyOp(const Op& op) { return op.sets.empty() && op.adds.empty() && op.deletes.empty() && op.revertRows.empty(); }

void drawChecks(const ModelView& model) {
    const std::vector< Check >& all = checks();
    const CheckCounts counts = countChecks(all);
    if (all.empty()) {
        ImGui::TextColored(kDim, "No problems.");
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kDim, "Checks run as you edit: links to paths or switches that don't exist, objects that need a link, "
                                 "the game's limits. The galaxy as loaded is the baseline, so only problems your edits add show here.");
        ImGui::PopTextWrapPos();
        return;
    }
    ImGui::TextColored(kDim, "%d error%s  ·  %d warning%s", counts.errors, counts.errors == 1 ? "" : "s", counts.warnings, counts.warnings == 1 ? "" : "s");
    ImGui::Separator();
    for (const Check* check : orderChecks(all)) {
        ImGui::PushID(check->key.c_str());
        const bool error = check->severity == Severity::Error;
        const ImU32 color = error ? IM_COL32(255, 96, 80, 255) : IM_COL32(250, 196, 60, 255);
        // The mark: a red dot for an error, an amber triangle for a warning.
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetTextLineHeight();
        if (error) {
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(at.x + 6.0f, at.y + h * 0.5f), 4.5f, color);
        } else {
            ImGui::GetWindowDrawList()->AddTriangleFilled(ImVec2(at.x + 6.0f, at.y + 2.0f), ImVec2(at.x + 11.0f, at.y + h - 2.0f),
                                                          ImVec2(at.x + 1.0f, at.y + h - 2.0f), color);
        }
        ImGui::Dummy(ImVec2(16.0f, h));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(check->message.c_str());
        ImGui::PopTextWrapPos();
        const bool clicked = ImGui::IsItemClicked();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to show it");
        if (!check->fixLabel.empty()) {
            if (ImGui::SmallButton(check->fixLabel.c_str())) {
                if (!emptyOp(check->fix)) {
                    enqueueOp(check->fix);
                } else {
                    // A pick flow (fixKind): show the object; its Inspector has the link field to set.
                    showRows(check->rows);
                    gHintFrames = 600;
                    if (check->fixKind == "pick-path" && !check->rows.empty())
                        beginPathPick(check->rows.front());  // M3 paths: choose in the viewport.
                    gHint = check->fixKind == "pick-path"   ? "Click a path in the viewport to link it. Escape cancels." :
                            check->fixKind == "pick-switch" ? "Drag a switch socket from Links in the Inspector onto another object." :
                            check->fixKind == "pick-camera" ? "Choose a camera: set CameraSetId in the Inspector." :
                                                              "";
                }
            }
        }
        ImGui::EndGroup();
        if (clicked) showRows(check->rows);
        ImGui::Spacing();
        ImGui::PopID();
    }
}

// ---- Inspector ------------------------------------------------------------------------------------


void noteHelp(const Column& column, const ModelView& model, RowId row) {
    if (!ImGui::IsItemHovered()) return;
    const Cell* original = model.originalCell(row, column.hash);
    gHelp = columnLabel(column) + (original != nullptr ? "  (disc: " + cellText(*original) + ")" : "  (added)");
}

// One editable field; edits go to every selected row that has the column.
void field(const ModelView& model, const std::vector< RowId >& rows, const Column& column, const Catalog::ClassInfo* info = nullptr) {
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
    // What the class does with a switch or link field (the catalog's roles), after the raw name.
    if (info != nullptr && ImGui::IsItemHovered()) {
        for (const Catalog::Switch& sw : info->switches) {
            if (sw.field == label) gHelp += std::string(sw.role == Catalog::SwitchRole::Read ? "  · reads: " : "  · turns on: ") + sw.help;
        }
        for (const Catalog::Link& link : info->links) {
            if (link.field == label && link.required) gHelp += "  · required: the object needs this link";
        }
    }
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

// The class all the rows share (the named settings need one), else null.
const Catalog::ClassInfo* sharedClass(const ModelView& model, const std::vector< RowId >& rows) {
    const std::uint32_t nameColumn = columnHash("name");
    const Catalog::ClassInfo* shared = nullptr;
    for (RowId row : rows) {
        const Cell* name = model.cell(row, nameColumn);
        const Catalog::ClassInfo* info = name != nullptr ? Catalog::find(name->s.c_str()) : nullptr;
        if (info == nullptr || (shared != nullptr && info != shared)) return nullptr;
        shared = info;
    }
    return shared;
}

// One named setting (Obj_arg<n> from the catalog): a combo for enums, a checkbox for bools, else a number with
// its unit. -1 is "the class's default" and shows as such. Edits go to every selected row, as raw fields do.
void argField(const ModelView& model, const std::vector< RowId >& rows, const Catalog::Arg& arg) {
    char raw[16];
    std::snprintf(raw, sizeof(raw), "Obj_arg%d", arg.index);
    const std::uint32_t hash = columnHash(raw);
    const RowId primary = rows.front();
    const Cell* current = model.cell(primary, hash);
    if (current == nullptr) return;
    bool mixed = false;
    for (RowId row : rows) {
        const Cell* other = model.cell(row, hash);
        mixed = mixed || (other != nullptr && !sameCell(*other, *current));
    }
    const Cell* original = model.originalCell(primary, hash);
    const bool changed = original != nullptr && !sameCell(*original, *current);
    const std::string label = arg.name.empty() ? std::string(raw) : arg.name;
    const std::int32_t value = current->i;
    auto help = [&] {
        if (!ImGui::IsItemHovered()) return;
        gHelp = label + (arg.help.empty() ? "" : ": " + arg.help) + "  (" + raw + "; " + argValueText(arg, value) +
                (original != nullptr ? ", disc " + argValueText(arg, original->i) : "") + ")";
    };
    ImGui::PushID(static_cast< int >(hash));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label.c_str());
    help();
    ImGui::SameLine(120.0f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 18.0f);
    std::int32_t edited = value;
    bool commit = false;
    switch (arg.type) {
    case Catalog::ArgType::Enum: {
        const std::string preview = mixed ? "Mixed" : argValueText(arg, value);
        if (ImGui::BeginCombo("##v", preview.c_str())) {
            if (ImGui::Selectable(("Default (" + argValueText(arg, arg.defaultValue) + ")").c_str(), value == -1)) edited = -1, commit = true;
            for (const Catalog::EnumChoice& choice : arg.choices) {
                if (choice.value == -1) continue;  // listed as the default above
                if (ImGui::Selectable(choice.label.c_str(), !mixed && choice.value == value)) edited = choice.value, commit = true;
            }
            ImGui::EndCombo();
        }
        break;
    }
    case Catalog::ArgType::Bool: {
        bool on = (value == -1 ? arg.defaultValue : value) > 0;
        // Off is written as -1, never 0: the game's bool getter (MR::getJMapInfoArgNoInit, bool*) reads anything but -1
        // as on, and classes testing > 0 read -1 as off too.
        if (ImGui::Checkbox(mixed ? "Mixed" : value == -1 ? "(default)" : "##v", &on)) edited = on ? 1 : -1, commit = true;
        break;
    }
    default: {
        // -1 (the class's default) shows as that default, greyed: "6 (default)". Typing a number sets it.
        const bool isDefault = value == -1 && !mixed;
        std::int32_t shown = isDefault ? arg.defaultValue : value;
        if (isDefault) ImGui::PushStyleColor(ImGuiCol_Text, kDim);
        ImGui::InputScalar("##v", ImGuiDataType_S32, &shown, nullptr, nullptr, mixed ? "Mixed" : isDefault ? "%d (default)" : "%d");
        if (isDefault) ImGui::PopStyleColor();
        commit = ImGui::IsItemDeactivatedAfterEdit() && !(isDefault && shown == arg.defaultValue);
        edited = shown;
        if (!arg.unit.empty()) {
            // The unit inside the field's right end.
            const std::string& hint = arg.unit;
            const ImVec2 end = ImGui::GetItemRectMax();
            const ImVec2 size = ImGui::CalcTextSize(hint.c_str());
            ImGui::GetWindowDrawList()->AddText(ImVec2(end.x - size.x - 6.0f, end.y - ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.y),
                                                ImGui::ColorConvertFloat4ToU32(kDim), hint.c_str());
        }
        break;
    }
    }
    help();
    if (commit && (edited != value || mixed)) {
        Cell cell = *current;
        cell.i = edited;
        Op op = setCellOp(model, rows, hash, cell);
        op.label = "Set " + label + (rows.size() > 1 ? " on " + std::to_string(rows.size()) + " objects" : "");
        enqueueOp(std::move(op));
    }
    ImGui::SameLine();
    if (changed) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("revert", ImVec2(12.0f, ImGui::GetFrameHeight()))) enqueueOp(revertCellOp(model, rows, hash));
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(at.x + 6.0f, at.y + ImGui::GetFrameHeight() * 0.5f), 4.0f,
                                                    ImGui::ColorConvertFloat4ToU32(kGold));
        if (ImGui::IsItemHovered()) gHelp = label + ": changed from the disc's " + argValueText(arg, original->i) + ". Click to put it back.";
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
    // Scales are small numbers (a brush's 0.15 = 150 units): two decimals there, one for positions and degrees.
    const bool changedNow = ImGui::DragFloat3("##v", values, speed, 0.0f, 0.0f, std::strcmp(prefix, "scale_") == 0 ? "%.2f" : "%.1f");
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

CheckCounts countChecks(const std::vector< Check >& all) {
    CheckCounts counts;
    for (const Check& check : all) (check.severity == Severity::Error ? counts.errors : counts.warnings)++;
    return counts;
}

std::vector< const Check* > orderChecks(const std::vector< Check >& all) {
    std::vector< const Check* > ordered;
    for (const Check& check : all) ordered.push_back(&check);
    std::stable_sort(ordered.begin(), ordered.end(), [](const Check* a, const Check* b) {
        return a->severity == Severity::Error && b->severity != Severity::Error;
    });
    return ordered;
}

void openTab(const char* name) { gOpenTab = name; }

bool matchesQuery(const std::string& text, const std::string& query) {
    if (query.empty()) return true;
    auto lower = [](std::string v) {
        for (char& c : v) c = static_cast< char >(std::tolower(static_cast< unsigned char >(c)));
        return v;
    };
    return lower(text).find(lower(query)) != std::string::npos;
}

namespace {
struct TargetRect {
    float x, y, w, h;
};
std::map< std::string, TargetRect > gTargets;
}  // namespace

void noteTestTarget(const char* name) {
    const ImVec2 min = ImGui::GetItemRectMin(), size = ImGui::GetItemRectSize();
    gTargets[name] = {min.x, min.y, size.x, size.y};
}

bool testTargetRect(const char* name, float* x, float* y, float* width, float* height) {
    auto it = gTargets.find(name);
    if (it == gTargets.end()) return false;
    *x = it->second.x, *y = it->second.y, *width = it->second.w, *height = it->second.h;
    return true;
}

void drawSection(const char* title, const char* right) { section(title, right); }
void applyOp(Op op) { enqueueOp(std::move(op)); }
std::string& helpText() { return gHelp; }
std::string displayName(const ModelView& model, RowId row) { return rowName(model, row); }

std::string missionLabel(const ModelView& model, int mission) {
    const char* name = model.missionName(mission);
    return name != nullptr && name[0] != '\0' ? std::string(name) : "Mission " + std::to_string(mission);
}

std::string argValueText(const Catalog::Arg& arg, std::int32_t value) {
    for (const Catalog::EnumChoice& choice : arg.choices) {
        if (choice.value == value) return choice.label;
    }
    if (value == -1 && arg.type != Catalog::ArgType::Enum) return "default";
    if (arg.type == Catalog::ArgType::Bool) return value > 0 ? "on" : "off";
    if (arg.type == Catalog::ArgType::Enum) return value == -1 ? "default" : std::to_string(value) + " (unlisted)";
    return std::to_string(value) + (arg.unit.empty() ? "" : " " + arg.unit);
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
                // Added, then deleted: nothing changed (a deleted row with no disc values was never on the disc).
                if (state == RowState::Deleted && !columns.empty() && model.originalCell(row, columns.front().hash) == nullptr) return;
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

void deleteSelection() {
    const ModelView* model = PetariEditor::model();
    const std::vector< RowId > rows = selectedRows();
    if (model == nullptr || rows.empty()) return;
    Op op;
    op.deletes = rows;
    op.label = rows.size() == 1 ? "Deleted " + rowName(*model, rows.front()) : "Deleted " + std::to_string(rows.size()) + " objects";
    enqueueOp(std::move(op));
    Pick::clearSelection();
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
            // PETARI_EDITOR_TAB=<Outliner|Browser|Changes|History|Checks>: open that tab once (screenshots, tests).
            static const char* sOpen = std::getenv("PETARI_EDITOR_TAB");
            auto flags = [&](const char* name) {
                if (gOpenTab == name) {
                    gOpenTab.clear();
                    return ImGuiTabItemFlags_SetSelected;
                }
                if (sOpen == nullptr || std::strcmp(sOpen, name) != 0) return ImGuiTabItemFlags_None;
                sOpen = nullptr;
                return ImGuiTabItemFlags_SetSelected;
            };
            serviceFraming();
            const bool openOutliner = ImGui::BeginTabItem("Outliner", nullptr, flags("Outliner"));
            noteTestTarget("tab:Outliner");
            if (openOutliner) {
                noteTab("Outliner");
                ImGui::TextColored(kDim, "%s  ·  mission %d", model->galaxy(), model->scenario());
                drawOutliner(*model, selectedRows());
                ImGui::EndTabItem();
            }
            const bool openBrowser = ImGui::BeginTabItem("Browser", nullptr, flags("Browser"));
            noteTestTarget("tab:Browser");
            if (openBrowser) {
                noteTab("Browser");
                drawBrowser(*model);
                ImGui::EndTabItem();
            }
            const int changeCount = static_cast< int >(collectChanges(*model).size());
            char changesLabel[32];
            std::snprintf(changesLabel, sizeof(changesLabel), changeCount > 0 ? "Changes %d###changes" : "Changes###changes", changeCount);
            // Changes and History share a tab (UX section 16: History is a view of the same edits): a switch at the top.
            static bool sHistory = false;
            ImGuiTabItemFlags changesFlags = flags("Changes");
            if (const ImGuiTabItemFlags history = flags("History"); history != ImGuiTabItemFlags_None) {
                changesFlags = history;
                sHistory = true;
            }
            const bool openChanges = ImGui::BeginTabItem(changesLabel, nullptr, changesFlags);
            noteTestTarget("tab:Changes");
            if (openChanges) {
                noteTab("Changes");
                if (ImGui::RadioButton("Changes", !sHistory)) sHistory = false;
                ImGui::SameLine();
                if (ImGui::RadioButton("History", sHistory)) sHistory = true;
                ImGui::SameLine();
                ImGui::TextColored(kDim, "Cmd+Z / Cmd+Shift+Z");
                ImGui::BeginChild("changes-list", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
                if (sHistory) {
                    drawHistory(*model);
                } else {
                    drawChanges(*model);
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            const CheckCounts counts = countChecks(checks());
            char checksLabel[32];
            std::snprintf(checksLabel, sizeof(checksLabel), counts.errors + counts.warnings > 0 ? "Checks %d###checks" : "Checks###checks",
                          counts.errors + counts.warnings);
            if (counts.errors > 0) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.38f, 1.0f));
            const bool checksOpen = ImGui::BeginTabItem(checksLabel, nullptr, flags("Checks"));
            noteTestTarget("tab:Checks");
            if (counts.errors > 0) ImGui::PopStyleColor();
            if (checksOpen) {
                noteTab("Checks");
                ImGui::BeginChild("checks-list", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
                drawChecks(*model);
                ImGui::EndChild();
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
            const char* className = model->objectName(primary);
            if (className != nullptr && className[0] != '\0' && rowName(*model, primary) != className) {
                ImGui::TextColored(kDim, "%s  ·  %s  ·  %s", className, model->zoneName(zone), model->tablePath(zone, table));
            } else {
                ImGui::TextColored(kDim, "%s  ·  %s", model->zoneName(zone), model->tablePath(zone, table));
            }
            const RowState state = model->state(primary);
            if (state != RowState::Original) {
                ImGui::SameLine();
                ImGui::TextColored(stateColor(state), state == RowState::Added ? "added" : state == RowState::Deleted ? "deleted" : "changed");
            }
            // Appears in (UX section 12): the missions this object is part of.
            if (const int missions = model->missionCount(); missions > 0) {
                section("Appears in", "missions");
                const std::uint32_t bits = model->rowMissions(primary);
                for (int mission = 1; mission <= missions; mission++) {
                    if (mission > 1) ImGui::SameLine(0.0f, 4.0f);
                    const bool in = (bits & (1u << (mission - 1))) != 0;
                    ImGui::PushStyleColor(ImGuiCol_Button, in ? ImVec4(0.98f, 0.80f, 0.22f, 0.80f) : ImVec4(1.0f, 1.0f, 1.0f, 0.06f));
                    ImGui::PushStyleColor(ImGuiCol_Text, in ? ImVec4(0.30f, 0.17f, 0.02f, 1.0f) : kDim);
                    char label[16];
                    std::snprintf(label, sizeof(label), "%d##m%d", mission, mission);
                    ImGui::SmallButton(label);
                    ImGui::PopStyleColor(2);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s: %s", missionLabel(*model, mission).c_str(), in ? "appears" : "does not appear");
                        gHelp = "Which missions this object appears in (its layer). Changing it comes with mission editing.";
                    }
                }
                if (bits == 0) {
                    ImGui::SameLine();
                    ImGui::TextColored(kDim, "in no mission");
                }
            }
            if (model->hasTransform(primary)) {
                section("Transform", "row space");
                transformRow(*model, rows, "Position", "pos_", 1.0f);
                transformRow(*model, rows, "Rotation", "dir_", 0.5f);
                transformRow(*model, rows, "Scale", "scale_", 0.01f);
            }
            drawAreasInspector(*model, rows);    // M3 areas: dimensions and camera preview.
            drawGravityInspector(*model, rows);  // M3 gravity: shape, range and priority.
            drawPathsInspector(*model, rows);  // M3 paths: points and link sockets.
            // Settings (UX section 8): the class's named args from the catalog, then every raw field folded below.
            const Catalog::ClassInfo* info = sharedClass(*model, rows);
            bool named = false;
            if (info != nullptr && !info->args.empty()) {
                section("Settings", rows.size() > 1 ? "edits apply to all" : "named");
                for (const Catalog::Arg& arg : info->args) argField(*model, rows, arg);
                named = true;
            }
            if (named) ImGui::Spacing();
            if (!named || ImGui::CollapsingHeader("All fields")) {
                if (!named) section("Fields", rows.size() > 1 ? "edits apply to all" : "raw");
                for (const Column& column : model->columns(zone, table)) {
                    if (isTransformColumn(column.name)) continue;
                    field(*model, rows, column, info);
                }
            }
            if (gHelp.empty() && gHintFrames > 0) gHelp = gHint;
            if (gHelp.empty() && info != nullptr && !info->description.empty()) gHelp = info->displayName + ": " + info->description;
        }
        if (gHintFrames > 0) gHintFrames--;
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
