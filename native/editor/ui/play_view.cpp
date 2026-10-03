#include "play_view.hpp"

#include "areas.hpp"
#include "host.hpp"
#include "imgui.h"
#include "panels.hpp"

#include <petari/editor/camera_preview.hpp>
#include <petari/editor_pick.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace PetariEditor::Ui {
namespace {

namespace P = CameraPreview;

bool gEnabled = std::getenv("PETARI_EDITOR_TEST_PLAYVIEW") != nullptr;  // tests start with it on
ImTextureID gTexture = 0;
std::uint64_t gTextureSerial = 0;  // the request the picture answers
double gNext = 0;                  // the next refresh (10 a second)
const void* gWaiting = nullptr;    // a copy whose readback hasn't come yet, and since when
double gWaitingSince = 0;
std::string gLogged;  // PETARI_EDITOR_TEST_PLAYVIEW: the last line written

// The game's picture of the latest request: into the texture once the GPU has read it back.
void takePicture() {
    const auto& frame = P::playView();
    if (!frame.copy || !Host::readback()) return;
    if (frame.copy != gWaiting) {
        gWaiting = frame.copy;
        gWaitingSince = ImGui::GetTime();
    }
    static std::vector< unsigned char > rgba(512 * 288 * 4);
    unsigned w = 0, h = 0;
    int ready = Host::readback()(frame.copy, rgba.data(), static_cast< unsigned >(rgba.size()), &w, &h);
    if (ready < 0) {
        rgba.resize(static_cast< std::size_t >(w) * h * 4);
        ready = Host::readback()(frame.copy, rgba.data(), static_cast< unsigned >(rgba.size()), &w, &h);
    }
    if (ready > 0) {
        if (!gTexture && Host::makeTexture())
            gTexture = static_cast< ImTextureID >(Host::makeTexture()(w, h, rgba.data()));
        else if (gTexture && Host::updateTexture())
            Host::updateTexture()(static_cast< unsigned long long >(gTexture), w, h, rgba.data());
        gTextureSerial = frame.serial;
    }
    // Lost readbacks (a frame the presenter dropped) don't stall the view: ask again after a while.
    if (ready > 0 || ImGui::GetTime() - gWaitingSince > 2.0) {
        P::acknowledge(frame.copy);
        gWaiting = nullptr;
    }
}

void logForTests(const P::View& frame) {
    static const bool enabled = std::getenv("PETARI_EDITOR_TEST_PLAYVIEW") != nullptr;
    if (!enabled || frame.serial == 0) return;
    // One line per camera choice, and one more when its first picture arrives (not one per refresh).
    char line[512];
    std::snprintf(line, sizeof(line), "[editor-playview] camera=\"%s\" id=\"%s\" type=%s area=%llu message=\"%s\"", frame.camera.c_str(),
                  frame.cameraId.c_str(), frame.cameraType.c_str(), static_cast< unsigned long long >(frame.area.v), frame.message.c_str());
    static bool pictured = false;
    const bool picture = gTexture && gTextureSerial == frame.serial;
    if (gLogged == line && (pictured || !picture)) return;
    if (gLogged != line) pictured = false;
    gLogged = line;
    pictured = pictured || picture;
    std::fprintf(stderr, "%s picture=%d\n", line, picture ? 1 : 0);
}

}  // namespace

void drawPlayViewToggle() {
    ImGui::Checkbox("Play view (4)", &gEnabled);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A corner picture of what Play shows from the play marker, with the camera it uses");
}

bool drawPlayViewOverlay(float x, float y, float width, float height) {
    const auto& io = ImGui::GetIO();
    if (!io.WantTextInput && !io.KeyCtrl && !io.KeySuper && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_4, false)) gEnabled = !gEnabled;
    takePicture();
    if (!gEnabled) {
        if (P::currentPlay().enabled) P::requestPlay(false);
        return false;
    }
    if (ImGui::GetTime() >= gNext || !P::currentPlay().enabled) {
        P::requestPlay(true);
        gNext = ImGui::GetTime() + 0.1;
    }
    const auto& frame = P::playView();
    logForTests(frame);

    const float w = std::clamp(width * 0.28f, 200.0f, 320.0f);
    const float imageW = w - 16.0f, imageH = imageW * 9.0f / 16.0f;
    // Bottom right at first; draggable, and kept inside the viewport.
    ImGui::SetNextWindowPos({x + width - w - 12.0f, y + height - imageH - 96.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({w, 0});
    ImGui::SetNextWindowBgAlpha(0.95f);
    ImGui::Begin("Play view##play-pip", nullptr,
                 ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoSavedSettings);
    {
        const ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        const ImVec2 clamped{std::clamp(pos.x, x, std::max(x, x + width - size.x)), std::clamp(pos.y, y, std::max(y, y + height - size.y))};
        if (clamped.x != pos.x || clamped.y != pos.y) ImGui::SetWindowPos(clamped);
    }
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImGui::Dummy({imageW, imageH});
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scene models only: the sky, effects and lighting appear in Play.");
    auto* list = ImGui::GetWindowDrawList();
    const ImVec2 max{min.x + imageW, min.y + imageH};
    if (gTexture) {
        // The last picture stays while a newer one is prepared; dimmed when the camera can't be shown now.
        const bool current = gTextureSerial == frame.serial || frame.copy || frame.message.rfind("Showing the last still", 0) == 0;
        list->AddImage(gTexture, min, max, {0, 0}, {1, 1}, current ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 70));
    } else {
        list->AddRectFilled(min, max, IM_COL32(24, 29, 40, 255));
        const char* text = "Preparing...";
        const ImVec2 t = ImGui::CalcTextSize(text);
        list->AddText({min.x + (imageW - t.x) * 0.5f, min.y + (imageH - t.y) * 0.5f}, IM_COL32(160, 160, 170, 255), text);
    }
    if (!frame.camera.empty()) {
        const std::string type = frame.cameraType.empty() ? "" : cameraTypeName(frame.cameraType);
        ImGui::Text("%s%s%s", frame.camera.c_str(), type.empty() ? "" : " · ", type.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s %s\nCamera edits apply to Play after Reload.", frame.cameraId.c_str(), frame.cameraType.c_str());
        if (frame.area.valid()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Select")) PetariNative::EditorPick::selectRow(frame.area.v, PetariNative::EditorPick::Click::Replace);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select the camera area this view comes from");
        }
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + imageW);
    ImGui::TextDisabled("%s", frame.message.empty() ? "Set the play marker to choose where Play starts." : frame.message.c_str());
    ImGui::PopTextWrapPos();
    const bool consumed = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    ImGui::End();
    return consumed;
}

}  // namespace PetariEditor::Ui
