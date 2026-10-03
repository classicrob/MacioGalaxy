#include "editor_m4_smoke.hpp"
#include "host.hpp"
#include "imgui.h"
#include "smoke.hpp"
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
        int buttonReleaseIn = 0;
        Smoke::Button heldButton = Smoke::Button::Home;
        int textKeyReleaseIn = 0;
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
        bool visible(const char* target) {
            float x, y, w, h;
            return PetariEditor::Ui::testTargetRect(target, &x, &y, &w, &h);
        }
        bool click(const char* target) {
            float x, y, w, h;
            if (!PetariEditor::Ui::testTargetRect(target, &x, &y, &w, &h))
                return false;
            std::fprintf(stderr, "[editor-m4-ui] click %s\n", target);
            return mouse(x + w / 2, y + h / 2);
        }
        bool button(Smoke::Button value) {
            Events::pressButton(static_cast< int >(value), true);
            heldButton = value;
            buttonReleaseIn = 3;
            return true;
        }
        bool homeClick(const char* label) {
            const auto view = HomeMenu::publishedView();
            if (!view.visible)
                return false;
            int focus = -1, target = -1;
            for (int i = 0; i < view.itemCount; ++i) {
                if (view.items[i].focused)
                    focus = i;
                if (std::strcmp(view.items[i].label, label) == 0)
                    target = i;
            }
            if (target < 0 || focus < 0)
                return false;
            if (focus == target)
                return button(Smoke::Button::A);
            button(focus < target ? Smoke::Button::StickDown : Smoke::Button::StickUp);
            return false;
        }
        bool selectText() {
            auto& io = ImGui::GetIO();
            io.AddKeyEvent(io.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl, true);
            io.AddKeyEvent(ImGuiKey_A, true);
            textKeyReleaseIn = 2;
            return true;
        }
    }  // namespace

    void editorM4Smoke(bool gameplay) {
        static const std::string which = std::getenv("PETARI_EDITOR_M4_UI_TEST") ? std::getenv("PETARI_EDITOR_M4_UI_TEST") : "";
        static bool done = false, started = false;
        static int step = 0, elapsed = 0, wait = 0;
        if (which.empty() || done)
            return;
        if (buttonReleaseIn && --buttonReleaseIn == 0)
            Events::pressButton(static_cast< int >(heldButton), false);
        if (textKeyReleaseIn && --textKeyReleaseIn == 0) {
            auto& io = ImGui::GetIO();
            io.AddKeyEvent(ImGuiKey_A, false);
            io.AddKeyEvent(io.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl, false);
        }
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
        if (++elapsed > 5000) {
            std::fprintf(stderr, "[editor-m4-ui] timeout at step %d, Home phase %d, Edit %d\n", step,
                         static_cast<int>(HomeMenu::publishedView().phase), EditorSettings::active());
            return finish("FAIL: UI flow timed out");
        }
        static int reported = -1;
        if (reported != step) {
            std::fprintf(stderr, "[editor-m4-ui] %s step %d\n", which.c_str(), step);
            reported = step;
        }
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
        if (which == "home-verify" || which == "settings-verify" || which == "settings-final") {
            if (!EditorSettings::active() || !model())
                return;
            const auto info = modGalaxyInfo();
            if (!info)
                return finish("FAIL: created galaxy settings unavailable");
            const char* sky = std::getenv("PETARI_EDITOR_M4_EXPECT_SKY");
            bool found = !sky || !*sky;
            for (int z = 0; z < model()->zoneCount(); ++z)
                for (int t = 0; t < model()->tableCount(z); ++t)
                    model()->forEachRow(z, t, [&](RowId row) {
                        if (sky && model()->objectName(row) && std::string(model()->objectName(row)) == sky)
                            found = true;
                    });
            if (!found)
                return finish("FAIL: chosen sky missing from layout");
            if (which == "settings-final") {
                if (info->missions.size() != 1 || info->name != "M4 Edited Galaxy")
                    return finish("FAIL: removed mission settings");
                if (step++ == 0)
                    EfbDump::mark("m4-settings-final");
                else
                    finish("PASS Observatory Home, sky, live galaxy name, add/remove mission SDL flow and restart into Edit");
                return;
            }
            if (which == "settings-verify") {
                if (info->missions.size() != 2 || info->name != "M4 Edited Galaxy")
                    return finish("FAIL: added mission settings");
                if (step == 0 && visible("section:Galaxy settings"))
                    ++step;
                else if (step == 1) {
                    EfbDump::mark("m4-settings-added");
                    ++step;
                } else if (step == 2 && click("button:Remove mission"))
                    ++step;
                else if (step == 3) {
                    setenv("PETARI_EDITOR_M4_UI_TEST", "settings-final", 1);
                    if (click("button:Apply galaxy settings"))
                        ++step;
                }
                return;
            }
            if (step == 0) {
                if (info->name != "My Galaxy")
                    return finish("FAIL: created galaxy name");
                EfbDump::mark("m4-created");
                ++step;
            } else if (step == 1 && visible("section:Galaxy settings"))
                ++step;
            else if (step == 2 && click("field:Galaxy name"))
                ++step;
            else if (step == 3 && selectText())
                ++step;
            else if (step == 4) {
                ImGui::GetIO().AddInputCharactersUTF8("M4 Edited Galaxy");
                ++step;
            } else if (step == 5 && click("button:Apply galaxy settings"))
                ++step;
            else if (step == 6 && info->name == "M4 Edited Galaxy") {
                EfbDump::mark("m4-settings-live");
                ++step;
            } else if (step == 7 && click("button:Add mission"))
                ++step;
            else if (step == 8) {
                setenv("PETARI_EDITOR_M4_UI_TEST", "settings-verify", 1);
                if (click("button:Apply galaxy settings"))
                    ++step;
            }
            return;
        }
        if (which != "home")
            return finish("FAIL: unknown UI flow");
        if (step == 0 && button(Smoke::Button::Home))
            ++step;
        else if (step == 1 && homeClick("Mods"))
            ++step;
        else if (step == 2 && homeClick("Level Select"))
            ++step;
        else if (step == 3 && HomeMenu::publishedView().phase == HomeMenu::Phase::Levels && button(Smoke::Button::StickLeft))
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
