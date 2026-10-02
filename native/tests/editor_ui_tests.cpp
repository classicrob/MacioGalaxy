// The level editor's frame (petari/editor/ui.hpp): the dock layout and the game's viewport,
// headless (no window, no renderer). The presenter's calls are recorded instead.
#include <petari/editor/ui.hpp>


#include "imgui.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

int gFailures = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++gFailures;                                                           \
        }                                                                          \
    } while (0)

struct Area {
    float x = 0, y = 0, w = 0, h = 0;
    int calls = 0;
    int freeAspect = -1;
} gArea;

void frame(float width, float height) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(width, height);
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    PetariEditor::Ui::drawFrame(width, height);
    ImGui::Render();
}

bool near(float a, float b, float tolerance = 1.5f) {
    return std::fabs(a - b) <= tolerance;
}

}  // namespace

// The presenter (native/gx/present/present.h), recorded.
extern "C" void petari_present_set_content_area(float x, float y, float width, float height) {
    gArea.x = x;
    gArea.y = y;
    gArea.w = width;
    gArea.h = height;
    gArea.calls++;
}
extern "C" void petari_present_set_free_aspect(int enabled) {
    gArea.freeAspect = enabled;
}


int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // the default layout every run
    unsigned char* pixels = nullptr;
    int texWidth = 0, texHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &texWidth, &texHeight);

    namespace Ui = PetariEditor::Ui;
    float x, y, w, h;

    // Hidden: nothing drawn, no viewport, the presenter untouched.
    frame(1440, 900);
    CHECK(!Ui::viewportRect(&x, &y, &w, &h));
    CHECK(gArea.calls == 0);

    // Shown at the MacBook Air default size (UX section 4): the game between a 260-point
    // Outliner and a 300-point Inspector, under the 44-point toolbar, above the 28-point footer.
    Ui::setVisible(true);
    frame(1440, 900);
    frame(1440, 900);  // docking settles on the second frame
    CHECK(Ui::viewportRect(&x, &y, &w, &h));
    std::printf("viewport at 1440x900: %.0f,%.0f %.0fx%.0f\n", x, y, w, h);
    CHECK(near(y, 44.0f));
    CHECK(near(y + h, 900.0f - 28.0f));
    CHECK(near(x, 260.0f, 4.0f));
    CHECK(near(x + w, 1440.0f - 300.0f, 4.0f));
    // The presenter got the same rectangle, and renders the game at its shape.
    CHECK(near(gArea.x, x, 0.01f) && near(gArea.y, y, 0.01f) && near(gArea.w, w, 0.01f) && near(gArea.h, h, 0.01f));
    CHECK(gArea.freeAspect == 1);

    // A bigger window: the docks keep their widths and the game takes the rest.
    frame(2560, 1440);
    frame(2560, 1440);
    CHECK(Ui::viewportRect(&x, &y, &w, &h));
    std::printf("viewport at 2560x1440: %.0f,%.0f %.0fx%.0f\n", x, y, w, h);
    CHECK(near(y, 44.0f) && near(y + h, 1440.0f - 28.0f));
    CHECK(w > 1500.0f && x > 200.0f && x + w < 2560.0f - 200.0f);

    // Focus mode: no panels, the game takes the whole window again.
    Ui::setFocusMode(true);
    frame(2560, 1440);
    CHECK(!Ui::viewportRect(&x, &y, &w, &h));
    CHECK(gArea.w == 0.0f && gArea.h == 0.0f);
    CHECK(gArea.freeAspect == 0);
    Ui::setFocusMode(false);
    frame(2560, 1440);
    CHECK(Ui::viewportRect(&x, &y, &w, &h));

    // Hidden again (back to playing): the whole window, and the game's own aspect.
    Ui::setVisible(false);
    frame(2560, 1440);
    CHECK(!Ui::viewportRect(&x, &y, &w, &h));
    CHECK(gArea.w == 0.0f && gArea.h == 0.0f && gArea.freeAspect == 0);
    const int calls = gArea.calls;
    frame(2560, 1440);
    CHECK(gArea.calls == calls);  // nothing more once the area is cleared

    // A window too small for the panels: the editor steps aside rather than squeezing the game.
    Ui::setVisible(true);
    frame(300, 100);
    CHECK(!Ui::viewportRect(&x, &y, &w, &h));

    ImGui::DestroyContext();
    if (gFailures != 0) {
        std::fprintf(stderr, "editor_ui_tests: %d failure(s)\n", gFailures);
        return 1;
    }
    std::printf("editor_ui_tests: OK\n");
    return 0;
}
