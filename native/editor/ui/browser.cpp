// The level editor's Browser (UX section 7), first version: every class placed in this galaxy (and the
// catalog's classes once it is filled), searchable, by category, with examples and Place.
#include "panels.hpp"

#include <petari/editor/catalog.hpp>
#include <petari/editor_pick.hpp>
#include <petari/home_menu.hpp>

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>

namespace PetariEditor::Ui {
namespace {

namespace Pick = PetariNative::EditorPick;

std::string lower(std::string text) {
    for (char& c : text) c = static_cast< char >(std::tolower(static_cast< unsigned char >(c)));
    return text;
}

// The category of a placed row without a catalog entry, from its table.
const char* categoryOfTable(const char* path, const char* name) {
    if (std::strstr(path, "planetobjinfo") != nullptr) return "Gravity";
    if (std::strstr(path, "areaobjinfo") != nullptr) return "Areas";
    if (std::strstr(path, "cameracubeinfo") != nullptr) return "Cameras";
    if (std::strstr(path, "mappartsinfo") != nullptr) return "Platforms";
    if (name != nullptr && std::strstr(name, "Planet") != nullptr) return "Planets";
    return "Objects";
}

ImU32 categoryColor(const std::string& category) {
    // Distinct, readable on the panel teal; gold is reserved for the selection.
    static const std::map< std::string, ImU32 > kColors = {
        {"Enemies", IM_COL32(230, 96, 80, 255)},   {"Items", IM_COL32(240, 190, 60, 255)},  {"Platforms", IM_COL32(120, 160, 220, 255)},
        {"Planets", IM_COL32(90, 190, 90, 255)},   {"Gravity", IM_COL32(102, 219, 255, 255)}, {"Areas", IM_COL32(150, 150, 210, 255)},
        {"Cameras", IM_COL32(220, 220, 220, 255)}, {"NPCs", IM_COL32(240, 150, 200, 255)},   {"Launchers", IM_COL32(250, 230, 120, 255)},
        {"Objects", IM_COL32(160, 200, 230, 255)},
    };
    auto it = kColors.find(category);
    return it != kColors.end() ? it->second : IM_COL32(170, 190, 210, 255);
}

std::vector< BrowserEntry > gEntries;
std::uint64_t gRevision = ~0ull;
std::string gQuery;
std::string gCategory;          // empty: all
std::string gSelectedClass;     // the card shown in detail
std::uint64_t gArmed = 0;       // stamp mode: the example row each click places

// A card: a category-coloured tile with the class's initials (thumbnails rendered from the game's
// models replace the tile when they exist), the name below.
bool card(const BrowserEntry& entry, bool selected, float size) {
    ImGui::PushID(entry.className.c_str());
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("card", ImVec2(size, size + ImGui::GetTextLineHeight() + 6.0f));
    const bool hovered = ImGui::IsItemHovered();
    // Drag a card onto the viewport to place it (edit_mode.cpp places it where the button comes up).
    if (!entry.examples.empty() && ImGui::BeginDragDropSource()) {
        const std::uint64_t row = entry.examples.front().v;
        ImGui::SetDragDropPayload(kPlacePayload, &row, sizeof(row));
        ImGui::Text("Place %s", entry.displayName.c_str());
        ImGui::EndDragDropSource();
    }
    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImVec2 max(min.x + size, min.y + size);
    list->AddRectFilled(min, max, IM_COL32(8, 60, 90, 230), 10.0f);
    const ImU32 color = categoryColor(entry.category);
    list->AddCircleFilled(ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f), size * 0.30f, color);
    char initials[3] = {entry.displayName.empty() ? '?' : entry.displayName[0], 0, 0};
    for (size_t i = 1; i < entry.displayName.size(); i++) {
        if (std::isupper(static_cast< unsigned char >(entry.displayName[i]))) {
            initials[1] = entry.displayName[i];
            break;
        }
    }
    ImFont* galaxy = PetariNative::HomeMenu::galaxyTextFont();
    ImFont* font = galaxy != nullptr ? galaxy : ImGui::GetFont();
    const float textSize = size * 0.28f;
    const ImVec2 text = font->CalcTextSizeA(textSize, 1e9f, 0.0f, initials);
    list->AddText(font, textSize, ImVec2((min.x + max.x - text.x) * 0.5f, (min.y + max.y - text.y) * 0.5f), IM_COL32(20, 30, 45, 255), initials);
    list->AddRect(min, max, selected ? IM_COL32(250, 204, 56, 255) : hovered ? IM_COL32(102, 219, 255, 255) : IM_COL32(102, 219, 255, 80),
                  10.0f, 0, selected ? 2.5f : 1.5f);
    if (entry.count > 0) {
        char count[16];
        std::snprintf(count, sizeof(count), "%d", entry.count);
        list->AddText(ImVec2(max.x - ImGui::CalcTextSize(count).x - 6.0f, min.y + 4.0f), IM_COL32(200, 230, 255, 220), count);
    }
    // The name, clipped to the card.
    list->PushClipRect(ImVec2(min.x, max.y), ImVec2(max.x, max.y + ImGui::GetTextLineHeight() + 6.0f), true);
    const ImVec2 nameSize = ImGui::CalcTextSize(entry.displayName.c_str());
    list->AddText(ImVec2(min.x + std::max(0.0f, (size - nameSize.x) * 0.5f), max.y + 3.0f), IM_COL32(230, 245, 255, 255),
                  entry.displayName.c_str());
    list->PopClipRect();
    ImGui::PopID();
    return clicked;
}

}  // namespace

const char* const kPlacePayload = "PETARI_EDITOR_PLACE";

std::vector< BrowserEntry > collectBrowserEntries(const ModelView& model) {
    std::map< std::string, BrowserEntry > byClass;
    for (int zone = 0; zone < model.zoneCount(); zone++) {
        for (int table = 0; table < model.tableCount(zone); table++) {
            const char* path = model.tablePath(zone, table);
            // Placeable things only: objects, platforms, gravity, areas, cameras (not rail points, lists, starts).
            if (std::strstr(path, "jmp/placement/") == nullptr && std::strstr(path, "jmp/mapparts/") == nullptr) continue;
            if (std::strstr(path, "stageobjinfo") != nullptr) continue;
            model.forEachRow(zone, table, [&](RowId row) {
                if (model.state(row) == RowState::Deleted) return;
                const char* name = model.objectName(row);
                if (name == nullptr || name[0] == '\0') return;
                BrowserEntry& entry = byClass[name];
                if (entry.className.empty()) {
                    entry.className = name;
                    entry.displayName = name;
                    entry.category = categoryOfTable(path, name);
                }
                entry.count++;
                if (entry.examples.size() < 8) entry.examples.push_back(row);
            });
        }
    }
    // The catalog's words where it has them, and its other placeable classes (no example yet).
    for (const Catalog::ClassInfo& info : Catalog::classes()) {
        if (!info.placeable) continue;
        BrowserEntry& entry = byClass[info.name];
        if (entry.className.empty()) entry.className = info.name;
        entry.displayName = info.displayName.empty() ? info.name : info.displayName;
        entry.description = info.description;
        entry.category = Catalog::categoryName(info.category);
        entry.aliases = info.aliases;
    }
    std::vector< BrowserEntry > entries;
    for (auto& [name, entry] : byClass) entries.push_back(std::move(entry));
    std::sort(entries.begin(), entries.end(), [](const BrowserEntry& a, const BrowserEntry& b) { return lower(a.displayName) < lower(b.displayName); });
    return entries;
}

std::vector< const BrowserEntry* > filterEntries(const std::vector< BrowserEntry >& entries, const std::string& query, const std::string& category) {
    std::vector< std::pair< int, const BrowserEntry* > > ranked;
    const std::string q = lower(query);
    for (const BrowserEntry& entry : entries) {
        if (!category.empty() && entry.category != category) continue;
        int rank = 0;  // lower is better: in this galaxy first, then name starts with, then contains
        if (!q.empty()) {
            const std::string display = lower(entry.displayName), name = lower(entry.className);
            int best = 99;
            if (display.rfind(q, 0) == 0 || name.rfind(q, 0) == 0) best = 0;
            else if (display.find(q) != std::string::npos || name.find(q) != std::string::npos) best = 1;
            for (const std::string& alias : entry.aliases) {
                const std::string a = lower(alias);
                if (a.rfind(q, 0) == 0) best = std::min(best, 0);
                else if (a.find(q) != std::string::npos) best = std::min(best, 1);
            }
            if (best == 99) continue;
            rank = best;
        }
        ranked.push_back({rank * 2 + (entry.count > 0 ? 0 : 1), &entry});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector< const BrowserEntry* > out;
    for (const auto& [rank, entry] : ranked) out.push_back(entry);
    return out;
}

std::uint64_t armedPlacement() {
    return gArmed;
}

void disarmPlacement() {
    gArmed = 0;
}

void drawBrowser(const ModelView& model) {
    if (gRevision != model.revision()) {
        gRevision = model.revision();
        gEntries = collectBrowserEntries(model);
    }
    // Search: names, classes and the catalog's aliases ("goomba" finds Kuribo).
    char query[64];
    std::snprintf(query, sizeof(query), "%s", gQuery.c_str());
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint("##search", "Search objects (goomba, launch, coin...)", query, sizeof(query))) gQuery = query;
    // Categories present.
    std::vector< std::string > categories;
    for (const BrowserEntry& entry : gEntries) {
        if (std::find(categories.begin(), categories.end(), entry.category) == categories.end()) categories.push_back(entry.category);
    }
    std::sort(categories.begin(), categories.end());
    if (ImGui::SmallButton(gCategory.empty() ? "[All]" : "All")) gCategory.clear();
    for (const std::string& category : categories) {
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(category.c_str()).x + 16.0f) ImGui::NewLine();
        const std::string label = gCategory == category ? "[" + category + "]" : category;
        if (ImGui::SmallButton(label.c_str())) gCategory = gCategory == category ? "" : category;
    }
    if (Catalog::classes().empty()) ImGui::TextDisabled("Showing what this galaxy uses; the catalog adds the rest.");

    const std::vector< const BrowserEntry* > shown = filterEntries(gEntries, gQuery, gCategory);
    // The grid, then the detail of the chosen card below it.
    const float detailHeight = gSelectedClass.empty() ? 0.0f : 190.0f;
    ImGui::BeginChild("cards", ImVec2(0, -detailHeight), ImGuiChildFlags_None);
    const float size = 66.0f, gap = 8.0f;
    const int perRow = std::max(1, static_cast< int >((ImGui::GetContentRegionAvail().x + gap) / (size + gap)));
    for (size_t i = 0; i < shown.size(); i++) {
        if (i % perRow != 0) ImGui::SameLine(0.0f, gap);
        if (card(*shown[i], shown[i]->className == gSelectedClass, size)) gSelectedClass = shown[i]->className;
        if (ImGui::IsItemHovered() && !shown[i]->description.empty()) ImGui::SetTooltip("%s", shown[i]->description.c_str());
    }
    if (shown.empty()) ImGui::TextDisabled("Nothing matches.");
    ImGui::EndChild();

    const BrowserEntry* selected = nullptr;
    for (const BrowserEntry& entry : gEntries) {
        if (entry.className == gSelectedClass) selected = &entry;
    }
    if (selected != nullptr) {
        ImGui::Separator();
        ImGui::Text("%s", selected->displayName.c_str());
        if (selected->displayName != selected->className) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", selected->className.c_str());
        }
        if (!selected->description.empty()) ImGui::TextWrapped("%s", selected->description.c_str());
        ImGui::TextDisabled("%s  ·  %d in this galaxy", selected->category.c_str(), selected->count);
        if (selected->examples.empty()) {
            ImGui::TextDisabled("Not used in this galaxy yet (placing classes from elsewhere comes with the catalog).");
        } else {
            // See an example: select one of the game's own placements (and its settings come along when placing).
            ImGui::TextDisabled("Examples (click to select):");
            for (size_t i = 0; i < selected->examples.size() && i < 4; i++) {
                int zone = 0, table = 0;
                model.locate(selected->examples[i], &zone, &table);
                char label[96];
                std::snprintf(label, sizeof(label), "%s #%d##ex%d", model.zoneName(zone), static_cast< int >(i + 1), static_cast< int >(i));
                if (ImGui::Selectable(label, false)) Pick::selectRow(selected->examples[i].v, Pick::Click::Replace);
            }
            const bool armed = gArmed == selected->examples.front().v;
            if (armed) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.98f, 0.80f, 0.22f, 0.55f));
            if (ImGui::Button(armed ? "Placing: click the ground (Esc stops)" : "Place")) gArmed = armed ? 0 : selected->examples.front().v;
            if (armed) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Each click in the viewport places a copy of the first example, settings included. Or drag the card onto the viewport.");
        }
    }
}

}  // namespace PetariEditor::Ui
