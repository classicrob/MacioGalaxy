#include "editor_m4_smoke.hpp"
#include "smoke_background.hpp"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <petari/editor/edit_api.hpp>
#include <petari/editor/ui.hpp>
#include <petari/editor_settings.hpp>
#include <petari/efb_dump_mark.hpp>
#include <petari/home_menu.hpp>
#include <string>

namespace PetariNative::App {
    namespace {
        SDL_Window* window() {
            int count = 0;
            SDL_Window** windows = SDL_GetWindows(&count);
            SDL_Window* result = count ? windows[0] : nullptr;
            SDL_free(windows);
            return result;
        }
        SDL_Event release{};
        int releaseIn = 0;
        bool mouse(float x, float y) {
            auto* w = window();
            if (!w)
                return false;
            SDL_Event e{};
            e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            e.button.windowID = SDL_GetWindowID(w);
            e.button.which = SmokeBackground::kTestMouseId;
            e.button.button = SDL_BUTTON_LEFT;
            e.button.down = true;
            e.button.clicks = 1;
            e.button.x = x;
            e.button.y = y;
            if (!SDL_PushEvent(&e))
                return false;
            release = e;
            release.type = SDL_EVENT_MOUSE_BUTTON_UP;
            release.button.down = false;
            releaseIn = 2;
            return true;
        }
        bool click(const char* target) {
            float x, y, w, h;
            if (!PetariEditor::Ui::testTargetRect(target, &x, &y, &w, &h))
                return false;
            std::fprintf(stderr, "[editor-m4-ui] click %s\n", target);
            return mouse(x + w / 2, y + h / 2);
        }
        bool homeClick(const char* label) {
            const auto view = HomeMenu::publishedView();
            auto* w = window();
            if (!view.visible || !w)
                return false;
            int width, height;
            SDL_GetWindowSize(w, &width, &height);
            for (int i = 0; i < view.itemCount; ++i) {
                if (std::strcmp(view.items[i].label, label))
                    continue;
                const auto r = view.items[i].rect;
                return mouse((.5f + (r.x0 + r.x1) / 4) * width, (.5f + (r.y0 + r.y1) / 4) * height);
            }
            return false;
        }
        bool key(SDL_Scancode scan, SDL_Keycode code) {
            auto* w = window();
            if (!w)
                return false;
            SDL_Event e{};
            e.type = SDL_EVENT_KEY_DOWN;
            e.key.windowID = SDL_GetWindowID(w);
            e.key.scancode = scan;
            e.key.key = code;
            e.key.down = true;
            if (!SDL_PushEvent(&e))
                return false;
            release = e;
            release.type = SDL_EVENT_KEY_UP;
            release.key.down = false;
            releaseIn = 2;
            return true;
        }
    }  // namespace

    void editorM4Smoke(bool gameplay) {
        static const std::string which = std::getenv("PETARI_EDITOR_M4_UI_TEST") ? std::getenv("PETARI_EDITOR_M4_UI_TEST") : "";
        static bool done = false, started = false;
        static int step = 0, elapsed = 0, wait = 0;
        if (which.empty() || done)
            return;
        if (releaseIn && --releaseIn == 0)
            SDL_PushEvent(&release);
        if (!started && (gameplay || EditorSettings::active()))
            started = true;
        if (!started)
            return;
        auto finish = [&](const char* text) {
            std::fprintf(stderr, "[editor-m4-ui] %s\n", text);
            done = true;
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit);
        };
        if (++elapsed > 5000)
            return finish("FAIL: UI flow timed out");
        if (++wait < 30 || releaseIn)
            return;
        wait = 0;
        using namespace PetariEditor;
        if (which == "journal-restore" || which == "journal-discard") {
            if (!EditorSettings::active()) {
                EditorSettings::requestEnter();
                return;
            }
            if (step == 0 && status().journalEdits > 0) {
                EfbDump::mark("m4-journal");
                ++step;
            } else if (step == 1 && click(which == "journal-restore" ? "button:Restore journal" : "button:Discard journal")) {
                ++step;
            } else if (step == 2 && mode() == Mode::Editing && status().journalEdits == 0) {
                const bool restore = which == "journal-restore";
                if (status().unsaved != restore)
                    return finish("FAIL: recovery unsaved state");
                if (restore && status().notice.find("Restored") == std::string::npos)
                    return finish("FAIL: missing restore notice");
                EfbDump::mark("m4-journal-result");
                ++step;
            } else if (step == 3)
                finish("PASS journal SDL choice and recovery result");
            return;
        }
        if (which == "home-verify") {
            if (!EditorSettings::active() || !model())
                return;
            const auto info = modGalaxyInfo();
            if (!info || info->name != "My Galaxy")
                return finish("FAIL: created galaxy settings");
            const char* sky = std::getenv("PETARI_EDITOR_M4_EXPECT_SKY");
            bool found = !sky || !*sky;
            for (int z = 0; z < model()->zoneCount(); ++z)
                for (int t = 0; t < model()->tableCount(z); ++t)
                    model()->forEachRow(z, t, [&](RowId row) {
                        if (sky && model()->objectName(row) && std::string(model()->objectName(row)) == sky)
                            found = true;
                    });
            if (!found)
                return finish("FAIL: chosen sky missing from created layout");
            if (step++ == 0)
                EfbDump::mark("m4-created");
            else
                finish("PASS Observatory Home to New Galaxy SDL flow, chosen sky and restart into Edit");
            return;
        }
        if (which != "home")
            return finish("FAIL: unknown UI flow");
        if (step == 0 && key(SDL_SCANCODE_F1, SDLK_F1))
            ++step;
        else if (step == 1 && homeClick("Mods"))
            ++step;
        else if (step == 2 && homeClick("Level Select"))
            ++step;
        else if (step == 3 && HomeMenu::publishedView().phase == HomeMenu::Phase::Levels && key(SDL_SCANCODE_LEFT, SDLK_LEFT))
            ++step;
        else if (step == 4 && std::strstr(HomeMenu::publishedView().items[0].label, "New galaxy")) {
            EfbDump::mark("m4-home");
            ++step;
        } else if (step == 5 && homeClick("Create..."))
            ++step;
        else if (step == 6 && EditorSettings::active() && model()) {
            if (std::string(model()->galaxy()) != "AstroGalaxy")
                return finish("FAIL: Home test must start at the Observatory");
            if (click("template:Brush skeleton"))
                ++step;
        } else if (step == 7 && click("combo:New galaxy sky"))
            ++step;
        else if (step == 8) {
            const auto& skies = newGalaxyChoices().skies;
            if (skies.empty())
                return finish("FAIL: sky choices are not available");
            if (click(("sky:" + skies.front()).c_str())) {
                setenv("PETARI_EDITOR_M4_EXPECT_SKY", skies.front().c_str(), 1);
                ++step;
            }
        } else if (step == 9) {
            EfbDump::mark("m4-new-sheet");
            ++step;
        } else if (step == 10) {
            setenv("PETARI_EDITOR_M4_UI_TEST", "home-verify", 1);
            if (click("button:Create"))
                ++step;
        }
    }
}  // namespace PetariNative::App
