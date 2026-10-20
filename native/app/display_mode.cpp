#include "petari/display_mode.hpp"
#include "petari/home_menu.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <imgui.h>

#include <cstdio>
#include <fstream>

namespace PetariNative::App::Display {
namespace {

std::filesystem::path gStore;  // event thread only, like everything here
bool gFullscreen = false;
SDL_Window* gWindow = nullptr;
bool gCursorHidden = false;

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

void save() {
    if (gStore.empty()) return;
    const std::filesystem::path temporary = gStore.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::trunc);
        out << "# Petari display settings (Cmd+Return switches)\n"
            << "Fullscreen=" << (gFullscreen ? "on" : "off") << "\n";
        if (!out) {
            std::fprintf(stderr, "petari: display: cannot write %s\n", temporary.string().c_str());
            return;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temporary, gStore, ec);
    if (ec) std::fprintf(stderr, "petari: display: cannot replace %s: %s\n", gStore.string().c_str(), ec.message().c_str());
}

bool windowFullscreen() {
    return gWindow != nullptr && (SDL_GetWindowFlags(gWindow) & SDL_WINDOW_FULLSCREEN) != 0;
}

}  // namespace

bool load(const std::filesystem::path& file, std::string* error) {
    gStore = file;
    std::ifstream in(file);
    if (!in) return true;  // no file: windowed
    std::string line;
    int number = 0;
    while (std::getline(in, line)) {
        number++;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos || trim(line.substr(0, equals)) != "Fullscreen") continue;  // unknown lines are ignored
        const std::string value = trim(line.substr(equals + 1));
        if (value != "on" && value != "off") {
            if (error) *error = file.string() + ": line " + std::to_string(number) + ": Fullscreen must be on or off";
            return false;
        }
        gFullscreen = value == "on";
    }
    return true;
}

bool fullscreen() { return gFullscreen; }

void configureHints() {
    SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0");
}

void attach(SDL_Window* window) {
    gWindow = window;
}

void handleEvent(const SDL_Event& event) {
    if (gWindow == nullptr) return;
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
        if (!event.key.repeat && (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) &&
            (event.key.mod & SDL_KMOD_GUI) != 0) {
            if (!SDL_SetWindowFullscreen(gWindow, !windowFullscreen())) {
                std::fprintf(stderr, "petari: display: fullscreen switch failed: %s\n", SDL_GetError());
            }
        }
        break;
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN: {
        const bool on = event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN;
        if (on != gFullscreen) {
            gFullscreen = on;
            save();
        }
        std::fprintf(stderr, "petari: display: %s\n", on ? "fullscreen" : "windowed");
        break;
    }
    default:
        break;
    }
}

void update() {
    if (gWindow == nullptr) return;
    const bool focused = (SDL_GetWindowFlags(gWindow) & SDL_WINDOW_INPUT_FOCUS) != 0;
    const bool hide = windowFullscreen() && focused && !HomeMenu::publishedView().visible;
    if (hide == gCursorHidden) return;
    // Aurora's ImGui SDL3 backend shows the cursor again on every new frame
    // unless told to leave it alone, so it is kept out while the cursor is hidden.
    if (ImGui::GetCurrentContext() != nullptr) {
        ImGuiIO& io = ImGui::GetIO();
        if (hide) {
            io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        } else {
            io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
        }
    }
    if (hide ? SDL_HideCursor() : SDL_ShowCursor()) {
        gCursorHidden = hide;
    }
}

}  // namespace PetariNative::App::Display
