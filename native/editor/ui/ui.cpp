// The level editor's frame: toolbar, dock and footer around the game (petari/editor/ui.hpp).
#include <petari/editor/ui.hpp>

#include "edit_mode.hpp"
#include "panels.hpp"

#include <petari/editor/edit_api.hpp>
#include <petari/editor_pick.hpp>

#include <petari/home_menu.hpp>

#include "imgui.h"
#include "imgui_internal.h"  // DockBuilder: the default layout

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

extern "C" void petari_present_set_content_area(float x, float y, float width, float height);
extern "C" void petari_present_set_free_aspect(int enabled);

namespace PetariEditor::Ui {
namespace {

constexpr float kToolbarHeight = 44.0f;
constexpr float kFooterHeight = 28.0f;
constexpr float kLeftWidth = 260.0f;   // UX section 4: 1440 x 900 window -> 260 / 300 docks
constexpr float kRightWidth = 300.0f;
constexpr const char* kOutliner = "Galaxy";  // Outliner, Changes, History
constexpr const char* kInspector = "Inspector";

bool gVisible = false;
bool gFocus = false;
bool gAreaSet = false;  // the presenter has a content area from us
ImFont* gUiFont = nullptr;
struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
};
Rect gViewport;
bool gViewportValid = false;

ImU32 rgba(float r, float g, float b, float a = 1.0f) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
}
ImVec4 vec(float r, float g, float b, float a = 1.0f) {
    return ImVec4(r, g, b, a);
}

// The Home menu's palette (native/HOME_MENU.md, imgui_overlay.cpp), tuned for dense panels.
void applyStyle(ImGuiStyle& s) {
    s = ImGuiStyle();
    s.WindowRounding = 0.0f;
    s.WindowBorderSize = 0.0f;
    s.ChildRounding = 6.0f;
    s.FrameRounding = 6.0f;
    s.PopupRounding = 8.0f;
    s.GrabRounding = 999.0f;
    s.TabRounding = 8.0f;
    s.ScrollbarRounding = 999.0f;
    s.FramePadding = ImVec2(6.0f, 3.0f);
    s.ItemSpacing = ImVec2(6.0f, 4.0f);
    s.WindowPadding = ImVec2(8.0f, 6.0f);
    s.IndentSpacing = 14.0f;
    s.ScrollbarSize = 10.0f;
    s.DockingSeparatorSize = 2.0f;
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = vec(0.90f, 0.96f, 1.00f);
    c[ImGuiCol_TextDisabled] = vec(0.50f, 0.66f, 0.79f);
    c[ImGuiCol_WindowBg] = vec(0.02f, 0.19f, 0.29f, 0.97f);       // between the panel gradient's ends
    c[ImGuiCol_ChildBg] = vec(0.0f, 0.06f, 0.12f, 0.35f);
    c[ImGuiCol_PopupBg] = vec(0.02f, 0.22f, 0.33f, 0.98f);
    c[ImGuiCol_Border] = vec(0.40f, 0.86f, 1.00f, 0.45f);          // the cyan rim
    c[ImGuiCol_FrameBg] = vec(0.0f, 0.06f, 0.12f, 0.55f);
    c[ImGuiCol_FrameBgHovered] = vec(0.0f, 0.10f, 0.18f, 0.75f);
    c[ImGuiCol_FrameBgActive] = vec(0.0f, 0.12f, 0.22f, 0.90f);
    c[ImGuiCol_TitleBg] = vec(0.03f, 0.34f, 0.45f);
    c[ImGuiCol_TitleBgActive] = vec(0.03f, 0.34f, 0.45f);
    c[ImGuiCol_MenuBarBg] = vec(0.03f, 0.30f, 0.40f);
    c[ImGuiCol_ScrollbarBg] = vec(0.0f, 0.05f, 0.12f, 0.45f);
    c[ImGuiCol_ScrollbarGrab] = vec(0.60f, 0.78f, 0.97f, 0.55f);
    c[ImGuiCol_ScrollbarGrabHovered] = vec(0.75f, 0.88f, 1.0f, 0.75f);
    c[ImGuiCol_ScrollbarGrabActive] = vec(0.98f, 0.80f, 0.22f);
    c[ImGuiCol_CheckMark] = vec(0.98f, 0.80f, 0.22f);
    c[ImGuiCol_SliderGrab] = vec(0.60f, 0.78f, 0.97f);
    c[ImGuiCol_SliderGrabActive] = vec(0.98f, 0.80f, 0.22f);
    c[ImGuiCol_Button] = vec(0.60f, 0.78f, 0.97f, 0.22f);
    c[ImGuiCol_ButtonHovered] = vec(0.60f, 0.85f, 1.0f, 0.38f);
    c[ImGuiCol_ButtonActive] = vec(0.98f, 0.80f, 0.22f, 0.55f);
    c[ImGuiCol_Header] = vec(0.98f, 0.80f, 0.22f, 0.16f);        // selection: gold
    c[ImGuiCol_HeaderHovered] = vec(1.0f, 1.0f, 1.0f, 0.07f);
    c[ImGuiCol_HeaderActive] = vec(0.98f, 0.80f, 0.22f, 0.28f);
    c[ImGuiCol_Separator] = vec(0.40f, 0.86f, 1.00f, 0.30f);
    c[ImGuiCol_SeparatorHovered] = vec(0.40f, 0.86f, 1.00f, 0.70f);
    c[ImGuiCol_SeparatorActive] = vec(0.98f, 0.80f, 0.22f);
    c[ImGuiCol_ResizeGrip] = vec(0.40f, 0.86f, 1.00f, 0.20f);
    c[ImGuiCol_ResizeGripHovered] = vec(0.40f, 0.86f, 1.00f, 0.60f);
    c[ImGuiCol_ResizeGripActive] = vec(0.98f, 0.80f, 0.22f);
    c[ImGuiCol_Tab] = vec(0.03f, 0.27f, 0.37f);
    c[ImGuiCol_TabHovered] = vec(0.20f, 0.55f, 0.72f);
    c[ImGuiCol_TabSelected] = vec(0.13f, 0.47f, 0.62f);           // the table header band
    c[ImGuiCol_TabSelectedOverline] = vec(0.98f, 0.80f, 0.22f);
    c[ImGuiCol_TabDimmed] = vec(0.03f, 0.24f, 0.33f);
    c[ImGuiCol_TabDimmedSelected] = vec(0.08f, 0.38f, 0.50f);
    c[ImGuiCol_DockingPreview] = vec(0.98f, 0.80f, 0.22f, 0.45f);
    c[ImGuiCol_DockingEmptyBg] = vec(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_TableHeaderBg] = vec(0.35f, 0.80f, 1.0f, 0.22f);
    c[ImGuiCol_TableBorderStrong] = vec(0.40f, 0.86f, 1.00f, 0.35f);
    c[ImGuiCol_TableBorderLight] = vec(0.40f, 0.86f, 1.00f, 0.15f);
    c[ImGuiCol_TableRowBgAlt] = vec(1.0f, 1.0f, 1.0f, 0.03f);
    c[ImGuiCol_TextSelectedBg] = vec(0.98f, 0.80f, 0.22f, 0.35f);
    c[ImGuiCol_NavHighlight] = vec(0.98f, 0.80f, 0.22f);
    c[ImGuiCol_ModalWindowDimBg] = vec(0.0f, 0.16f, 0.26f, 0.6f);
}

// The editor's look applies only while its windows are drawn: the Home menu and anything
// else drawn with ImGui keep the global style.
struct StyleScope {
    ImGuiStyle saved;
    bool font = false;
    StyleScope() {
        saved = ImGui::GetStyle();
        static ImGuiStyle editor = [] {
            ImGuiStyle s;
            applyStyle(s);
            return s;
        }();
        ImGui::GetStyle() = editor;
        if (gUiFont != nullptr) {
            ImGui::PushFont(gUiFont);
            font = true;
        }
    }
    ~StyleScope() {
        if (font) ImGui::PopFont();
        ImGui::GetStyle() = saved;
    }
};

// The game's white-to-blue capsule with a cyan rim; gold when primary (HOME_MENU.md "Style").
bool capsule(const char* id, const char* label, bool gold, float height) {
    ImFont* font = PetariNative::HomeMenu::galaxyTextFont();
    const float size = height * 0.62f;
    ImFont* f = font != nullptr ? font : ImGui::GetFont();
    const ImVec2 text = f->CalcTextSizeA(size, 1e9f, 0.0f, label);
    const ImVec2 dim(text.x + height * 1.1f, height);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, dim);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 max(min.x + dim.x, min.y + dim.y);
    ImDrawList* list = ImGui::GetWindowDrawList();
    const float r = height * 0.5f;
    if (gold) list->AddRectFilled(ImVec2(min.x - 3, min.y - 3), ImVec2(max.x + 3, max.y + 3), rgba(1.0f, 0.86f, 0.30f, 0.30f), r + 3);
    const ImU32 top = gold ? rgba(1.0f, 1.0f, 0.93f) : rgba(1.0f, 1.0f, 1.0f);
    const ImU32 bottom = gold ? rgba(1.0f, 0.84f, 0.36f) : rgba(0.60f, 0.78f, 0.97f);
    list->AddRectFilled(min, max, bottom, r);
    list->AddRectFilledMultiColor(ImVec2(min.x + r * 0.4f, min.y + 1), ImVec2(max.x - r * 0.4f, max.y - 1), top, top, bottom, bottom);
    list->AddRect(min, max, gold ? rgba(0.98f, 0.80f, 0.22f) : rgba(0.22f, 0.74f, 1.0f, hovered ? 1.0f : 0.9f), r, 0, 1.5f);
    const ImU32 ink = gold ? rgba(0.30f, 0.17f, 0.02f) : rgba(0.20f, 0.25f, 0.34f);
    list->AddText(f, size, ImVec2(min.x + (dim.x - text.x) * 0.5f, min.y + (dim.y - text.y) * 0.5f), ink, label);
    return pressed;
}

void galaxyText(const char* text, float size, ImU32 color) {
    ImFont* font = PetariNative::HomeMenu::galaxyTextFont();
    ImFont* f = font != nullptr ? font : ImGui::GetFont();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 dim = f->CalcTextSizeA(size, 1e9f, 0.0f, text);
    ImDrawList* list = ImGui::GetWindowDrawList();
    list->AddText(f, size, ImVec2(pos.x + 1, pos.y + 1.5f), rgba(0.0f, 0.05f, 0.12f, 0.6f), text);  // the menus' soft shadow
    list->AddText(f, size, pos, color, text);
    ImGui::Dummy(dim);
}

constexpr ImGuiWindowFlags kBandFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                        ImGuiWindowFlags_NoScrollWithMouse;

void bandBackground(ImVec2 min, ImVec2 max, bool top) {
    ImDrawList* list = ImGui::GetWindowDrawList();
    if (top) {
        list->AddRectFilledMultiColor(min, max, rgba(0.03f, 0.34f, 0.45f), rgba(0.03f, 0.34f, 0.45f), rgba(0.02f, 0.22f, 0.33f),
                                      rgba(0.02f, 0.22f, 0.33f));
        list->AddLine(ImVec2(min.x, max.y - 0.5f), ImVec2(max.x, max.y - 0.5f), rgba(0.40f, 0.86f, 1.0f, 0.6f));
    } else {
        // The Home menu's footer band: pause-screen teal with a light cyan line.
        list->AddRectFilledMultiColor(min, max, rgba(0.03f, 0.43f, 0.53f, 0.95f), rgba(0.03f, 0.43f, 0.53f, 0.95f),
                                      rgba(0.01f, 0.24f, 0.33f, 0.97f), rgba(0.01f, 0.24f, 0.33f, 0.97f));
        list->AddLine(ImVec2(min.x, min.y + 0.5f), ImVec2(max.x, min.y + 0.5f), rgba(0.55f, 0.92f, 1.0f, 0.9f));
    }
}

void drawToolbar(float width) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(width, kToolbarHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("##PetariEditorToolbar", nullptr, kBandFlags);
    bandBackground(ImVec2(0, 0), ImVec2(width, kToolbarHeight), true);
    ImGui::SetCursorPosY((kToolbarHeight - 18.0f) * 0.5f);
    galaxyText("Level Editor", 18.0f, rgba(1.0f, 1.0f, 1.0f));
    ImGui::SameLine(0.0f, 24.0f);
    // Undo / redo (Cmd+Z / Cmd+Shift+Z).
    ImGui::SetCursorPosY((kToolbarHeight - ImGui::GetFrameHeight()) * 0.5f);
    ImGui::BeginDisabled(!canUndo());
    if (ImGui::Button("Undo")) undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!canRedo());
    if (ImGui::Button("Redo")) redo();
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, 16.0f);
    const float buttonHeight = 26.0f;
    const Status& state = status();
    // Reload: rebuild the scene from the layout, applying pending changes (UX section 3).
    char reload[48];
    std::snprintf(reload, sizeof(reload), state.pendingCount > 0 ? "Reload  %d pending" : "Reload", state.pendingCount);
    const float right = width - 12.0f;
    ImGui::SetCursorPos(ImVec2(right - 96.0f - 8.0f - 230.0f, (kToolbarHeight - ImGui::GetFrameHeight()) * 0.5f));
    if (state.pendingCount > 0) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.66f, 1.0f, 0.45f));
    if (ImGui::Button(reload)) enqueue(Command{Command::Kind::Reload});
    if (state.pendingCount > 0) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Cmd+R: rebuild the world from your layout (enemies home, coins back, pending changes applied)");
    ImGui::SameLine();
    if (ImGui::Button("Resume")) {
        Command play{Command::Kind::Play};
        play.from = PlayFrom::Resume;
        enqueue(std::move(play));
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shift+Tab: continue the frozen moment as it is");
    ImGui::SameLine();
    if (ImGui::Button("From start")) {
        Command play{Command::Kind::Play};
        play.from = PlayFrom::MissionStart;
        enqueue(std::move(play));
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Option+Tab: play the mission from its start");
    ImGui::SetCursorPos(ImVec2(right - 96.0f, (kToolbarHeight - buttonHeight) * 0.5f));
    if (capsule("##play", "Play  Tab", true, buttonHeight)) {
        Command play{Command::Kind::Play};
        play.from = PlayFrom::Marker;
        enqueue(std::move(play));
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void drawFooter(float width, float height) {
    ImGui::SetNextWindowPos(ImVec2(0, height - kFooterHeight));
    ImGui::SetNextWindowSize(ImVec2(width, kFooterHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("##PetariEditorFooter", nullptr, kBandFlags);
    const ImVec2 min(0, height - kFooterHeight);
    bandBackground(min, ImVec2(width, height), false);
    ImGui::SetCursorPosY((kFooterHeight - ImGui::GetTextLineHeight()) * 0.5f);
    // The keys that work right now (the Home menu's footer convention), or the drag under way.
    const PetariNative::EditorGizmo::View gizmo = PetariNative::EditorGizmo::view();
    if (gizmo.dragging && gizmo.status[0] != '\0') {
        ImGui::TextUnformatted(gizmo.status);
    } else {
        ImGui::TextUnformatted("Tab play    M marker    Click select    W E R move/rotate/scale    End drop    F frame    Cmd+Z undo    Cmd+. focus");
    }
    // Right end: unsaved edits and the stage's memory (UX section 16, the memory meter).
    const Status& state = status();
    const float meter = 90.0f;
    float x = width - 12.0f - meter - 150.0f;
    if (state.unsaved) {
        ImGui::SameLine(x - 80.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.42f, 1.0f), "Unsaved");
    }
    if (state.heapTotal > 0) {
        const float used = 1.0f - static_cast<float>(state.heapFree) / static_cast<float>(state.heapTotal);
        ImGui::SameLine(x);
        ImGui::Text("Memory %d%%", static_cast<int>(used * 100.0f + 0.5f));
        ImGui::SameLine();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float barY = at.y + ImGui::GetTextLineHeight() * 0.5f - 4.0f;
        ImDrawList* list = ImGui::GetWindowDrawList();
        list->AddRectFilled(ImVec2(at.x, barY), ImVec2(at.x + meter, barY + 8.0f), rgba(0.0f, 0.06f, 0.12f, 0.6f), 4.0f);
        const ImU32 fill = used >= 1.0f ? rgba(1.0f, 0.35f, 0.35f) : used >= 0.9f ? rgba(1.0f, 0.55f, 0.25f) : rgba(0.35f, 0.85f, 0.45f);
        list->AddRectFilled(ImVec2(at.x, barY), ImVec2(at.x + meter * std::fmin(used, 1.0f), barY + 8.0f), fill, 4.0f);
        ImGui::Dummy(ImVec2(meter, 1.0f));
        if (ImGui::IsItemHovered()) {
            std::string capacities;
            for (const Capacity& c : state.capacities) {
                char line[96];
                std::snprintf(line, sizeof(line), "%s %d / %d\n", c.name, c.used, c.limit);
                capacities += line;
            }
            ImGui::SetTooltip("Stage memory: %u KB free of %u KB\n%s", state.heapFree / 1024, state.heapTotal / 1024, capacities.c_str());
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// The default layout (UX section 4): Outliner left, Inspector right, the game between.
void buildDefaultLayout(ImGuiID dock, ImVec2 size) {
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dock, size);
    ImGuiID left = 0, rest = 0, right = 0, center = 0;
    ImGui::DockBuilderSplitNode(dock, ImGuiDir_Left, std::clamp(kLeftWidth / size.x, 0.1f, 0.4f), &left, &rest);
    ImGui::DockBuilderSplitNode(rest, ImGuiDir_Right, std::clamp(kRightWidth / (size.x - kLeftWidth), 0.1f, 0.5f), &right, &center);
    ImGui::DockBuilderDockWindow(kOutliner, left);
    ImGui::DockBuilderDockWindow(kInspector, right);
    ImGui::DockBuilderFinish(dock);
}

// A toast at the top of the viewport for 4 s when a command failed (Runtime's status().lastError).
void drawToast() {
    static std::uint64_t sSerial = 0;
    static std::string sText;
    static double sUntil = 0.0;
    const Status& state = status();
    if (state.errorSerial != sSerial) {
        sSerial = state.errorSerial;
        sText = state.lastError;
        sUntil = ImGui::GetTime() + 4.0;
    }
    if (sText.empty() || ImGui::GetTime() > sUntil) return;
    ImDrawList* list = ImGui::GetForegroundDrawList();
    const ImVec2 size = ImGui::CalcTextSize(sText.c_str());
    const float cx = gViewport.x + gViewport.w * 0.5f, top = gViewport.y + 12.0f;
    const ImVec2 min(cx - size.x * 0.5f - 14.0f, top), max(cx + size.x * 0.5f + 14.0f, top + size.y + 10.0f);
    list->AddRectFilled(min, max, rgba(0.05f, 0.03f, 0.03f, 0.88f), (max.y - min.y) * 0.5f);
    list->AddRect(min, max, rgba(1.0f, 0.55f, 0.25f, 1.0f), (max.y - min.y) * 0.5f, 0, 1.5f);
    list->AddText(ImVec2(min.x + 14.0f, min.y + 5.0f), rgba(1, 1, 1, 1), sText.c_str());
}

void setArea(float x, float y, float w, float h) {
    petari_present_set_content_area(x, y, w, h);
    gAreaSet = w > 0.0f && h > 0.0f;
    // Docked, the game renders at the viewport's shape (the HUD is hidden while editing).
    petari_present_set_free_aspect(gAreaSet ? 1 : 0);
}

}  // namespace

bool installFonts(float displayScale) {
    if (ImGui::GetCurrentContext() == nullptr || gUiFont != nullptr) {
        return gUiFont != nullptr;
    }
    const float scale = displayScale > 0.0f ? displayScale : 1.0f;
    // macOS's UI font, read from the system at run time.
    static const char* const kCandidates[] = {"/System/Library/Fonts/SFNS.ttf", "/System/Library/Fonts/Helvetica.ttc"};
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    if (atlas->Fonts.Size == 0) {
        atlas->AddFontDefault();  // ImGui's own font stays the default
    }
    for (const char* path : kCandidates) {
        if (FILE* file = std::fopen(path, "rb")) {
            std::fclose(file);
            ImFontConfig config;
            config.OversampleH = 2;
            config.OversampleV = 1;
            std::snprintf(config.Name, sizeof(config.Name), "Editor UI (%s)", path);
            gUiFont = atlas->AddFontFromFileTTF(path, 13.0f * scale, &config, atlas->GetGlyphRangesDefault());
            if (gUiFont != nullptr) {
                gUiFont->Scale = 1.0f / scale;
                return true;
            }
        }
    }
    return false;
}

void setVisible(bool visible) {
    gVisible = visible;
}

bool visible() {
    return gVisible;
}

void setFocusMode(bool focus) {
    gFocus = focus;
}

bool focusMode() {
    return gFocus;
}

bool viewportRect(float* x, float* y, float* width, float* height) {
    if (!gViewportValid) return false;
    *x = gViewport.x;
    *y = gViewport.y;
    *width = gViewport.w;
    *height = gViewport.h;
    return true;
}

void drawFrame(float windowWidth, float windowHeight) {
    updateEditMode(windowWidth, windowHeight);  // uses last frame's viewport
    gViewportValid = false;
    if (!gVisible || gFocus || !(windowWidth > 0.0f) || !(windowHeight > kToolbarHeight + kFooterHeight + 50.0f)) {
        if (gAreaSet) setArea(0, 0, 0, 0);  // the game takes the whole window again
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    StyleScope style;

    drawToolbar(windowWidth);
    drawFooter(windowWidth, windowHeight);

    // The dock host between the bands; its central node is the game's viewport.
    const ImVec2 hostPos(0.0f, kToolbarHeight);
    const ImVec2 hostSize(windowWidth, windowHeight - kToolbarHeight - kFooterHeight);
    ImGui::SetNextWindowPos(hostPos);
    ImGui::SetNextWindowSize(hostSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##PetariEditorDockHost", nullptr,
                 kBandFlags | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNavFocus);
    ImGui::PopStyleVar(2);
    const ImGuiID dock = ImGui::GetID("PetariEditorDock");
    if (ImGuiDockNode* node = ImGui::DockBuilderGetNode(dock); node == nullptr || !node->IsSplitNode()) {
        buildDefaultLayout(dock, hostSize);
    }
    ImGui::DockSpace(dock, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingOverCentralNode);
    ImGui::End();

    drawGalaxyWindow(kOutliner);
    drawInspectorWindow(kInspector);

    if (ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dock); central != nullptr && central->Size.x > 8.0f &&
                                                                        central->Size.y > 8.0f) {
        gViewport = {central->Pos.x, central->Pos.y, central->Size.x, central->Size.y};
        gViewportValid = true;
        setArea(gViewport.x, gViewport.y, gViewport.w, gViewport.h);
        drawToast();
    }
}

}  // namespace PetariEditor::Ui
