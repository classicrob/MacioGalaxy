#include "host.hpp"
// The level editor's Browser (UX section 7), first version: every class placed in this galaxy (and the
// catalog's classes once it is filled), searchable, by category, with examples and Place.
#include "panels.hpp"

#include <petari/editor/ui.hpp>

#include <petari/editor/catalog.hpp>
#include <petari/editor_pick.hpp>
#include <petari/home_menu.hpp>

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
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
std::uint64_t gArmed = 0;       // stamp mode: what each click places (armedPlacement)

// Class placements (no example): the specs tokens point at, kept for the session.
struct PlaceSpec {
    std::string className;
    std::vector< std::pair< int, std::int32_t > > args;
};
std::vector< PlaceSpec > gSpecs;

// What placing a card means: a copy of its first example, else a new row of its class (with its preset).
std::uint64_t tokenFor(const BrowserEntry& entry) {
    if (entry.presetArgs.empty() && !entry.examples.empty()) return entry.examples.front().v;
    for (size_t i = 0; i < gSpecs.size(); i++) {
        if (gSpecs[i].className == entry.className && gSpecs[i].args == entry.presetArgs) return kClassTokenBit | i;
    }
    gSpecs.push_back({entry.className, entry.presetArgs});
    return kClassTokenBit | (gSpecs.size() - 1);
}

// Thumbnails: the game draws one requested class a frame (Game/Camera/EditorThumbs) into a copy the
// presenter reads back; the texture is kept for the session. A few in flight (the game's copies are a
// pool of 8); a class whose example cannot be drawn times out and keeps its initials.
struct Thumbnail {
    ImTextureID texture = 0;
    const void* copy = nullptr;  // published, being read back
    ImVec2 uv0{0, 0}, uv1{1, 1};  // the drawn part, squared
    int waited = 0;              // frames since requested
    int attempts = 1;
    bool failed = false;
    int retryAt = 0;             // gFrame: an empty drawing is tried again (pipelines compile on first use)
};
std::map< std::string, Thumbnail > gThumbnails;

std::vector< unsigned char > gRgba;
constexpr int kThumbnailsInFlight = 3;
constexpr int kThumbnailTimeout = 90;  // frames
constexpr int kThumbnailAttempts = 3;
int gFrame = 0;

void serviceThumbnails() {
    if (Host::readback() == nullptr)
        return;
    gFrame++;
    char name[64];
    const void* copy = nullptr;
    while (Pick::takeThumbnail(name, sizeof(name), &copy)) {
        Thumbnail& thumb = gThumbnails[name];
        thumb.copy = copy;
        thumb.failed = copy == nullptr;  // nothing of that class could be drawn
    }
    for (auto& [className, thumb] : gThumbnails) {
        if (thumb.texture != 0 || thumb.failed) continue;
        if (thumb.copy != nullptr) {
            unsigned width = 0, height = 0;
            int result = Host::readback()(thumb.copy, gRgba.data(), static_cast< unsigned >(gRgba.size()), &width, &height);
            if (result < 0) {
                gRgba.resize(static_cast< size_t >(width) * height * 4);
                result = Host::readback()(thumb.copy, gRgba.data(), static_cast< unsigned >(gRgba.size()), &width, &height);
            }
            if (result == 1) {
                thumb.copy = nullptr;
                float uv[4];
                if (!thumbnailCrop(gRgba.data(), width, height, uv)) {
                    // Drawn empty: its shaders may still have been compiling (Aurora skips those draws); try
                    // again in a second, a few times, before keeping the initials.
                    thumb.failed = true;
                    thumb.retryAt = thumb.attempts < kThumbnailAttempts ? gFrame + 60 : 0;
                    continue;
                }
                thumb.uv0 = ImVec2(uv[0], uv[1]);
                thumb.uv1 = ImVec2(uv[2], uv[3]);
                thumb.texture = static_cast< ImTextureID >(Host::makeTexture()(width, height, gRgba.data()));
                continue;
            }
        }
        if (++thumb.waited > kThumbnailTimeout) thumb.failed = true;
    }
}

// Asks for a card's thumbnail when it is on screen and few are in flight.
void wantThumbnail(const BrowserEntry& entry) {
    if (Host::readback() == nullptr || entry.examples.empty())
        return;
    auto existing = gThumbnails.find(entry.className);
    const bool retry = existing != gThumbnails.end() && existing->second.failed && existing->second.retryAt != 0 && gFrame >= existing->second.retryAt;
    if (existing != gThumbnails.end() && !retry) return;
    int inFlight = 0;
    for (const auto& [className, thumb] : gThumbnails) inFlight += thumb.texture == 0 && !thumb.failed;
    if (inFlight >= kThumbnailsInFlight) return;
    Thumbnail next;
    next.attempts = retry ? existing->second.attempts + 1 : 1;
    gThumbnails[entry.className] = next;
    std::uint64_t rows[Pick::kThumbnailExamples] = {};
    const int count = std::min(static_cast< int >(entry.examples.size()), Pick::kThumbnailExamples);
    for (int i = 0; i < count; i++) rows[i] = entry.examples[i].v;
    Pick::requestThumbnail(entry.className.c_str(), rows, count);
}

// The tile without a thumbnail: a category-coloured disc with the class's initials.
void drawInitials(ImDrawList* list, const BrowserEntry& entry, ImVec2 min, ImVec2 max, float size) {
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
}

// A card: the class's thumbnail (rendered from its placed example), else its initials tile; the name below.
bool card(const BrowserEntry& entry, bool selected, float size) {
    ImGui::PushID(entry.key.c_str());
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float lineHeight = ImGui::GetTextLineHeight();
    const bool clicked = ImGui::InvisibleButton("card", ImVec2(size, size + lineHeight * 2.0f + 4.0f));
    const bool hovered = ImGui::IsItemHovered();
    // Drag a card onto the viewport to place it (edit_mode.cpp places it where the button comes up).
    if (ImGui::BeginDragDropSource()) {
        const std::uint64_t token = tokenFor(entry);
        ImGui::SetDragDropPayload(kPlacePayload, &token, sizeof(token));
        ImGui::Text("Place %s", entry.displayName.c_str());
        ImGui::EndDragDropSource();
    }
    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImVec2 max(min.x + size, min.y + size);
    list->AddRectFilled(min, max, IM_COL32(8, 60, 90, 230), 10.0f);
    if (ImGui::IsItemVisible()) wantThumbnail(entry);
    auto thumb = gThumbnails.find(entry.className);
    if (thumb != gThumbnails.end() && thumb->second.texture != 0) {
        list->AddImageRounded(thumb->second.texture, ImVec2(min.x + 3.0f, min.y + 3.0f), ImVec2(max.x - 3.0f, max.y - 3.0f),
                              thumb->second.uv0, thumb->second.uv1, IM_COL32_WHITE, 8.0f);
    } else {
        drawInitials(list, entry, min, max, size);
    }
    list->AddRect(min, max, selected ? IM_COL32(250, 204, 56, 255) : hovered ? IM_COL32(102, 219, 255, 255) : IM_COL32(102, 219, 255, 80),
                  10.0f, 0, selected ? 2.5f : 1.5f);
    if (entry.count > 0) {
        char count[16];
        std::snprintf(count, sizeof(count), "%d", entry.count);
        const ImVec2 countSize = ImGui::CalcTextSize(count);
        const ImVec2 at(max.x - countSize.x - 6.0f, min.y + 4.0f);
        list->AddRectFilled(ImVec2(at.x - 4.0f, at.y), ImVec2(at.x + countSize.x + 4.0f, at.y + countSize.y), IM_COL32(8, 40, 64, 190),
                            countSize.y * 0.5f);
        list->AddText(at, IM_COL32(200, 230, 255, 230), count);
    }
    // The name below, on up to two lines (the tooltip has it whole).
    const std::vector< std::string > lines = cardLabelLines(entry.displayName, size + 6.0f, [](const std::string& text) {
        return ImGui::CalcTextSize(text.c_str()).x;
    });
    for (size_t i = 0; i < lines.size(); i++) {
        const float width = ImGui::CalcTextSize(lines[i].c_str()).x;
        list->AddText(ImVec2(min.x + (size - width) * 0.5f, max.y + 2.0f + lineHeight * static_cast< float >(i)), IM_COL32(230, 245, 255, 255),
                      lines[i].c_str());
    }
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
    for (auto& [name, entry] : byClass) {
        entry.key = entry.className;
        entries.push_back(std::move(entry));
    }
    // A card per catalog preset (Terrain's brush shapes...): always placed new, with the preset's args.
    for (const Catalog::ClassInfo& info : Catalog::classes()) {
        if (!info.placeable) continue;
        for (const Catalog::Preset& preset : info.presets) {
            BrowserEntry entry;
            entry.className = info.name;
            entry.key = info.name + "#" + preset.label;
            entry.displayName = (info.displayName.empty() ? info.name : info.displayName) + ": " + preset.label;
            entry.description = info.description;
            entry.category = Catalog::categoryName(info.category);
            entry.aliases = info.aliases;
            entry.presetArgs = preset.args;
            entries.push_back(std::move(entry));
        }
    }
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
    serviceThumbnails();
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
        if (card(*shown[i], shown[i]->key == gSelectedClass, size)) gSelectedClass = shown[i]->key;
        if (ImGui::IsItemHovered()) {
            const BrowserEntry& entry = *shown[i];
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(entry.displayName.c_str());
            if (entry.className != entry.displayName) ImGui::TextDisabled("%s", entry.className.c_str());
            if (!entry.description.empty()) {
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
                ImGui::TextUnformatted(entry.description.c_str());
                ImGui::PopTextWrapPos();
            }
            ImGui::EndTooltip();
        }
    }
    if (shown.empty()) ImGui::TextDisabled("Nothing matches.");
    ImGui::EndChild();

    const BrowserEntry* selected = nullptr;
    for (const BrowserEntry& entry : gEntries) {
        if (entry.key == gSelectedClass) selected = &entry;
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
        if (selected->examples.empty() || !selected->presetArgs.empty()) {
            // A new one from the catalog (EP-005): its table, defaults and preset; nothing to copy.
            ImGui::TextDisabled(selected->presetArgs.empty() ? "Not in this galaxy yet: Place adds a new one with the catalog's settings."
                                                             : "Place adds a new one with this preset.");
            const std::uint64_t token = tokenFor(*selected);
            const bool armed = gArmed == token;
            if (armed) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.98f, 0.80f, 0.22f, 0.55f));
            if (ImGui::Button(armed ? "Placing: click the ground (Esc stops)" : "Place")) gArmed = armed ? 0 : token;
            noteTestTarget("button:Place");
            if (armed) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Each click in the viewport places a new one. Or drag the card onto the viewport.");
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

std::vector< std::string > cardLabelLines(const std::string& text, float width, const std::function< float(const std::string&) >& measure) {
    if (measure(text) <= width) return {text};
    // Break points: after a space, or before a capital that follows a lower-case letter or digit ("Benefit|Item").
    std::vector< size_t > breaks;
    for (size_t i = 1; i < text.size(); i++) {
        const unsigned char c = static_cast< unsigned char >(text[i]), p = static_cast< unsigned char >(text[i - 1]);
        if (p == ' ' || (std::isupper(c) && (std::islower(p) || std::isdigit(p)))) breaks.push_back(i);
    }
    auto trim = [](std::string s) {
        while (!s.empty() && s.back() == ' ') s.pop_back();
        return s;
    };
    auto ellipsize = [&](std::string s) {
        if (measure(s) <= width) return s;
        while (!s.empty() && measure(s + "...") > width) s.pop_back();
        return trim(s) + "...";
    };
    // The longest first line that fits, broken at a break point; else at the last character that fits.
    size_t split = 0;
    for (size_t at : breaks) {
        if (measure(trim(text.substr(0, at))) <= width) split = at;
    }
    if (split == 0) {
        split = 1;
        while (split < text.size() && measure(text.substr(0, split + 1)) <= width) split++;
    }
    return {trim(text.substr(0, split)), ellipsize(text.substr(split))};
}

bool thumbnailCrop(const unsigned char* rgba, unsigned width, unsigned height, float uv[4]) {
    int x0 = static_cast< int >(width), y0 = static_cast< int >(height), x1 = -1, y1 = -1;
    for (int y = 0; y < static_cast< int >(height); y++) {
        for (int x = 0; x < static_cast< int >(width); x++) {
            if (rgba[(static_cast< size_t >(y) * width + x) * 4 + 3] == 0) continue;
            x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
        }
    }
    if (x1 < 0) return false;
    // A square around the drawn part, 6% padding, kept inside the image.
    const float side = std::max(x1 - x0, y1 - y0) + 1.0f;
    const float half = std::min(side * 0.56f, std::min(width, height) * 0.5f);
    const float cx = std::clamp((x0 + x1 + 1) * 0.5f, half, width - half), cy = std::clamp((y0 + y1 + 1) * 0.5f, half, height - half);
    uv[0] = (cx - half) / width, uv[1] = (cy - half) / height, uv[2] = (cx + half) / width, uv[3] = (cy + half) / height;
    return true;
}

void placeToken(std::uint64_t token) {
    if (!isClassToken(token)) {
        Pick::place(token);
        return;
    }
    const size_t index = static_cast< size_t >(token & ~kClassTokenBit);
    if (index >= gSpecs.size()) return;
    Pick::PlaceClass request;
    std::snprintf(request.name, sizeof(request.name), "%s", gSpecs[index].className.c_str());
    for (const auto& [arg, value] : gSpecs[index].args) {
        if (request.argCount >= 8) break;
        request.argIndex[request.argCount] = arg;
        request.argValue[request.argCount] = value;
        request.argCount++;
    }
    Pick::placeClass(request);
}

}  // namespace PetariEditor::Ui
