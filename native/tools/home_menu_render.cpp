// Development tool: renders every Home menu page offline to PNG, with a small
// software rasterizer for ImGui's draw data, so the menu's look can be checked
// without running the game.
//
// usage: petari_home_menu_render <out-dir> [--font <LayoutData/Font.arc>] [--background <file.ppm>]
//                                 [--size WxH] [--wide 0|1]
// Writes <out-dir>/<page>.png for the list, Controls, Mods, Odyssey camera,
// Level Select, Mod folder, My Progress and the Restart confirmation. Without
// --font the pages use ImGui's built-in font (the fallback). The background
// (binary PPM, e.g. a converted XFB dump) is stretched to the image; without it
// a dark gradient stands in for the game.

#include "petari/home_menu.hpp"
#include "petari/png_write.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace HM = PetariNative::HomeMenu;

namespace {

struct Canvas {
    int width = 0, height = 0;
    std::vector<float> rgb;  // 0..1
};

bool readPpm(const std::string& path, std::vector<unsigned char>* rgb, int* width, int* height) {
    std::ifstream in(path, std::ios::binary);
    std::string magic;
    int max = 0;
    in >> magic >> *width >> *height >> max;
    in.get();
    if (!in || magic != "P6" || max != 255) return false;
    rgb->resize(static_cast<size_t>(*width) * *height * 3);
    in.read(reinterpret_cast<char*>(rgb->data()), static_cast<std::streamsize>(rgb->size()));
    return static_cast<bool>(in);
}

void fillBackground(Canvas& canvas, const std::vector<unsigned char>& rgb, int bw, int bh) {
    for (int y = 0; y < canvas.height; ++y) {
        for (int x = 0; x < canvas.width; ++x) {
            float* p = &canvas.rgb[(static_cast<size_t>(y) * canvas.width + x) * 3];
            if (rgb.empty()) {
                const float t = static_cast<float>(y) / canvas.height;
                p[0] = 0.10f + 0.15f * t;
                p[1] = 0.25f + 0.25f * t;
                p[2] = 0.30f + 0.10f * t;
                continue;
            }
            const int sx = x * bw / canvas.width, sy = y * bh / canvas.height;
            for (int c = 0; c < 3; ++c) p[c] = rgb[(static_cast<size_t>(sy) * bw + sx) * 3 + c] / 255.0f;
        }
    }
}

struct Texture {
    const unsigned char* rgba;
    int width, height;
    void sample(float u, float v, float out[4]) const {
        const float x = u * width - 0.5f, y = v * height - 0.5f;
        const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
        const float fx = x - x0, fy = y - y0;
        for (int c = 0; c < 4; ++c) out[c] = 0.0f;
        for (int j = 0; j < 2; ++j) {
            for (int i = 0; i < 2; ++i) {
                const int sx = std::clamp(x0 + i, 0, width - 1), sy = std::clamp(y0 + j, 0, height - 1);
                const float w = (i ? fx : 1.0f - fx) * (j ? fy : 1.0f - fy);
                const unsigned char* p = rgba + (static_cast<size_t>(sy) * width + sx) * 4;
                for (int c = 0; c < 4; ++c) out[c] += w * p[c] / 255.0f;
            }
        }
    }
};

void triangle(Canvas& canvas, const Texture& texture, const ImDrawVert& a, const ImDrawVert& b, const ImDrawVert& c,
              const ImVec4& clip) {
    const float area = (b.pos.x - a.pos.x) * (c.pos.y - a.pos.y) - (b.pos.y - a.pos.y) * (c.pos.x - a.pos.x);
    if (std::fabs(area) < 1e-6f) return;
    const int x0 = std::max(static_cast<int>(std::floor(std::max(std::min({a.pos.x, b.pos.x, c.pos.x}), clip.x))), 0);
    const int y0 = std::max(static_cast<int>(std::floor(std::max(std::min({a.pos.y, b.pos.y, c.pos.y}), clip.y))), 0);
    const int x1 = std::min(static_cast<int>(std::ceil(std::min(std::max({a.pos.x, b.pos.x, c.pos.x}), clip.z))), canvas.width);
    const int y1 = std::min(static_cast<int>(std::ceil(std::min(std::max({a.pos.y, b.pos.y, c.pos.y}), clip.w))), canvas.height);
    const ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a.col), cb = ImGui::ColorConvertU32ToFloat4(b.col),
                 cc = ImGui::ColorConvertU32ToFloat4(c.col);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const float px = x + 0.5f, py = y + 0.5f;
            const float w0 = ((b.pos.x - px) * (c.pos.y - py) - (b.pos.y - py) * (c.pos.x - px)) / area;
            const float w1 = ((c.pos.x - px) * (a.pos.y - py) - (c.pos.y - py) * (a.pos.x - px)) / area;
            const float w2 = 1.0f - w0 - w1;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
            float t[4];
            texture.sample(w0 * a.uv.x + w1 * b.uv.x + w2 * c.uv.x, w0 * a.uv.y + w1 * b.uv.y + w2 * c.uv.y, t);
            const float r = (w0 * ca.x + w1 * cb.x + w2 * cc.x) * t[0];
            const float g = (w0 * ca.y + w1 * cb.y + w2 * cc.y) * t[1];
            const float bl = (w0 * ca.z + w1 * cb.z + w2 * cc.z) * t[2];
            const float al = (w0 * ca.w + w1 * cb.w + w2 * cc.w) * t[3];
            float* p = &canvas.rgb[(static_cast<size_t>(y) * canvas.width + x) * 3];
            p[0] = p[0] * (1.0f - al) + r * al;
            p[1] = p[1] * (1.0f - al) + g * al;
            p[2] = p[2] * (1.0f - al) + bl * al;
        }
    }
}

void rasterize(Canvas& canvas, const Texture& texture) {
    const ImDrawData* data = ImGui::GetDrawData();
    for (int l = 0; l < data->CmdListsCount; ++l) {
        const ImDrawList* list = data->CmdLists[l];
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback != nullptr) continue;
            for (unsigned i = 0; i + 2 < cmd.ElemCount; i += 3) {
                const ImDrawIdx* idx = list->IdxBuffer.Data + cmd.IdxOffset + i;
                const ImDrawVert* v = list->VtxBuffer.Data + cmd.VtxOffset;
                triangle(canvas, texture, v[idx[0]], v[idx[1]], v[idx[2]], cmd.ClipRect);
            }
        }
    }
}

// A stand-in for the input layer's remapper: the default rows, and a capture
// that "presses" F, which Spin already has.
class FakeControls final : public HM::ControlsBackend {
public:
    int rowCount() override { return 34; }
    void row(int index, HM::RemapRow* out) override {
        static const char* const kLabels[] = {
            PETARI_ICON_STICK " Move forward", PETARI_ICON_STICK " Move back", PETARI_ICON_STICK " Move left",
            PETARI_ICON_STICK " Move right", PETARI_ICON_A " Jump / confirm", PETARI_ICON_SHAKE " Spin",
            PETARI_ICON_Z " Crouch / ground pound", PETARI_ICON_B " Shoot Star Bits / back", PETARI_ICON_A " Start (title: A and B)",
            PETARI_ICON_C " Recenter camera", PETARI_ICON_PAD " Rotate camera left", PETARI_ICON_PAD " Rotate camera right"};
        static const char* const kSlots[][4] = {
            {"W", "", "", ""}, {"S", "", "", ""}, {"A", "", "", ""}, {"D", "", "", ""},
            {"Space", "Right mouse", "bottom", ""}, {"F", "", "left", "RB"}, {"Left Shift", "Right Shift", "LT", ""},
            {"Left mouse", "Backspace", "right", "RT"}, {"Return", "Keypad Enter", "top", ""}, {"C", "", "LB", ""},
            {"Q", "Left arrow", "D-pad left", ""}, {"E", "Right arrow", "D-pad right", ""}};
        const int i = index % 12;
        std::snprintf(out->label, sizeof(out->label), "%s", index < 12 ? kLabels[i] : "Camera mod: orbit left");
        for (int s = 0; s < 4; ++s) std::snprintf(out->slots[s], sizeof(out->slots[s]), "%s", kSlots[i][s]);
        out->changed = index == 9;
        out->conflict[0] = index == 9;
    }
    void beginCapture() override {}
    void cancelCapture() override {}
    HM::CaptureStatus pollCapture(char* name, int size) override {
        if (!captured) return HM::CaptureStatus::Waiting;
        std::snprintf(name, static_cast<size_t>(size), "F");
        return HM::CaptureStatus::Captured;
    }
    HM::RemapOutcome place(int, int, HM::RemapChoice, char* other, int size) override {
        std::snprintf(other, static_cast<size_t>(size), "Spin");
        return HM::RemapOutcome::Conflict;
    }
    HM::RemapOutcome clearSlot(int, int) override { return HM::RemapOutcome::Done; }
    HM::RemapOutcome resetRow(int) override { return HM::RemapOutcome::Done; }
    HM::RemapOutcome resetAll() override { return HM::RemapOutcome::Done; }
    void notice(char* out, int size) override { std::snprintf(out, static_cast<size_t>(size), "%s", ""); }
    bool captured = false;
};

HM::FrameInput press(std::uint32_t button) {
    HM::FrameInput input;
    input.hold = button;
    input.trigger = button;
    return input;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <out-dir> [--font Font.arc] [--background file.ppm] [--size WxH] [--wide 0|1]\n", argv[0]);
        return 2;
    }
    const std::string out = argv[1];
    std::string font, background;
    int width = 1280, height = 720;
    bool wide = true;
    for (int i = 2; i + 1 < argc; i += 2) {
        const std::string option = argv[i];
        if (option == "--font") font = argv[i + 1];
        else if (option == "--background") background = argv[i + 1];
        else if (option == "--size") std::sscanf(argv[i + 1], "%dx%d", &width, &height);
        else if (option == "--wide") wide = std::strcmp(argv[i + 1], "0") != 0;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    if (!font.empty()) {
        std::string error;
        if (!HM::installGalaxyFonts(font.c_str(), &error)) std::fprintf(stderr, "fonts: %s (built-in font)\n", error.c_str());
    }
    unsigned char* atlas = nullptr;
    int atlasWidth = 0, atlasHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(&atlas, &atlasWidth, &atlasHeight);
    const Texture texture{atlas, atlasWidth, atlasHeight};

    std::vector<unsigned char> backgroundRgb;
    int bw = 0, bh = 0;
    if (!background.empty() && !readPpm(background, &backgroundRgb, &bw, &bh)) {
        std::fprintf(stderr, "cannot read %s (binary PPM)\n", background.c_str());
        return 1;
    }

    // The game image: 16:9 or 4:3, letterboxed in the canvas.
    const float aspect = wide ? 16.0f / 9.0f : 4.0f / 3.0f;
    float imageW = static_cast<float>(width), imageH = imageW / aspect;
    if (imageH > height) {
        imageH = static_cast<float>(height);
        imageW = imageH * aspect;
    }
    const float imageX = (width - imageW) * 0.5f, imageY = (height - imageH) * 0.5f;

    HM::Menu menu;
    menu.create(HM::Config{});
    menu.setWidescreen(wide);
    const HM::ControlsEntry controls[] = {
        {"", "Keyboard and mouse", "Controller"},
        {PETARI_ICON_STICK " Move", "W A S D", "left stick"},
        {PETARI_ICON_A " Jump / confirm", "Space / Right mouse", "bottom"},
        {PETARI_ICON_SHAKE " Spin", "F", "left / RB"},
        {PETARI_ICON_Z " Crouch / ground pound", "Shift", "LT"},
        {PETARI_ICON_B " Shoot Star Bits / back", "Left mouse / Backspace", "right / RT"},
        {PETARI_ICON_PAD " Rotate camera", "Q / E", "D-pad left / D-pad right"},
        {PETARI_ICON_C " Recenter camera", "C", "LB"},
        {PETARI_ICON_PLUS " Pause", "Escape / -", "Start / Back"},
        {PETARI_ICON_HOME " This menu", "F1 / `", "Guide"},
    };
    menu.setControls(controls, static_cast<int>(sizeof(controls) / sizeof(controls[0])));
    // Every slot filled, with the Mod folder entry below: the page at its tallest (12 rows).
    HM::ModsEntry mods[HM::kMaxMods] = {{"Collect Star Bits (G)", false}, {"Shoot nearest enemy (V)", false},
                                        {"Odyssey movement", true}, {"Cappy (R)", false, 2},
                                        {"Skip cutscenes", false}};
    for (int i = 5; i < HM::kMaxMods; ++i) {
        std::snprintf(mods[i].label, sizeof(mods[i].label), "Spare mod %d", i + 1);
    }
    menu.setMods(mods, HM::kMaxMods);
    HM::FolderModEntry folder[2] = {{"Coin ring (3 files)", true}, {"Retexture pack (40 files)", false}};
    menu.setFolderMods(folder, 2);
    menu.setFooterKeys("Space", "Escape", "F1", "\xE2\x86\x90/\xE2\x86\x92");

    auto settle = [&] {
        for (int i = 0; i < 12; ++i) menu.update(HM::FrameInput{});
    };
    bool menuHidden = false;  // the hold ring shots: the game scene with no menu
    auto shot = [&](const char* name) {
        HM::publish(menuHidden ? HM::View{} : menu.view());
        io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        HM::drawImGuiOverlay(imageX, imageY, imageW, imageH);
        ImGui::Render();
        Canvas canvas;
        canvas.width = width;
        canvas.height = height;
        canvas.rgb.assign(static_cast<size_t>(width) * height * 3, 0.0f);
        fillBackground(canvas, backgroundRgb, bw, bh);
        rasterize(canvas, texture);
        std::vector<unsigned char> rgb(canvas.rgb.size());
        for (size_t i = 0; i < rgb.size(); ++i) rgb[i] = static_cast<unsigned char>(std::clamp(canvas.rgb[i], 0.0f, 1.0f) * 255.0f + 0.5f);
        PetariNative::writePngRgb(out + "/" + name + ".png", static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                                  rgb.data());
        std::printf("%s/%s.png\n", out.c_str(), name);
    };
    using namespace HM::Button;
    menu.open();
    settle();
    shot("list");
    menu.update(press(Down));
    menu.update(press(A));
    shot("controls");
    // The remapper: browsing, waiting for a press, and a conflict.
    FakeControls fake;
    menu.setControlsBackend(&fake);
    menu.update(press(Plus));
    menu.update(press(A));
    for (int i = 0; i < 9; ++i) menu.update(press(Down));
    menu.update(press(Right));
    shot("remap");
    menu.update(press(A));
    shot("remap-capture");
    fake.captured = true;
    menu.update(HM::FrameInput{});
    shot("remap-conflict");
    menu.update(press(Plus));
    menu.update(press(Plus));
    menu.setControlsBackend(nullptr);
    menu.update(press(Down));
    menu.update(press(A));
    shot("mods");
    for (int i = 0; i < 4; ++i) menu.update(press(Down));
    menu.update(press(A));
    shot("camera");
    menu.update(press(Plus));
    menu.update(press(Down));
    menu.update(press(A));
    shot("levels");
    menu.update(press(Plus));
    menu.update(press(Down));
    menu.update(press(A));
    shot("modfolder");
    menu.update(press(Plus));
    menu.update(press(Plus));
    menu.update(press(Down));
    menu.update(press(A));
    shot("progress");
    menu.update(press(Plus));
    menu.update(press(Down));
    menu.update(press(A));
    shot("confirm");
    // The hold-to-skip ring while Space is held: a third, two thirds, and the skip running.
    static float ringProgress = 0.0f;
    HM::setHoldProgressSource([]() { return ringProgress; });
    menuHidden = true;
    for (const float p : {0.33f, 0.66f, 1.0f}) {
        ringProgress = p;
        shot(p < 0.5f ? "skip-ring-third" : p < 1.0f ? "skip-ring-twothirds" : "skip-ring-full");
    }
    // The same ring for a keyboard player (the key bound to A) and a long key name.
    {
        menuHidden = true;
        ringProgress = 0.66f;
        auto keyShot = [&](const char* name, const char* key) {
            io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
            io.DeltaTime = 1.0f / 60.0f;
            HM::publish(HM::View{});
            ImGui::NewFrame();
            HM::drawHoldRing(ringProgress, imageX, imageY, imageW, imageH, key);
            ImGui::Render();
            Canvas canvas;
            canvas.width = width;
            canvas.height = height;
            canvas.rgb.assign(static_cast<size_t>(width) * height * 3, 0.0f);
            fillBackground(canvas, backgroundRgb, bw, bh);
            rasterize(canvas, texture);
            std::vector<unsigned char> rgb(canvas.rgb.size());
            for (size_t i = 0; i < rgb.size(); ++i) rgb[i] = static_cast<unsigned char>(std::clamp(canvas.rgb[i], 0.0f, 1.0f) * 255.0f + 0.5f);
            PetariNative::writePngRgb(out + "/" + name + ".png", static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                                      rgb.data());
            std::printf("%s/%s.png\n", out.c_str(), name);
        };
        keyShot("skip-ring-key-space", "Space");
        keyShot("skip-ring-key-shift", "Left Shift");
    }
    // The Point-to-move steering cue: Mario at the lower middle, the pointer up and to the right.
    {
        menuHidden = true;
        ringProgress = 0.0f;
        static float cuePointer[2] = {0.65f, -0.35f};
        HM::setSteerCueSource([](float* mx, float* my, float* px, float* py) {
            *mx = 0.0f; *my = 0.35f; *px = cuePointer[0]; *py = cuePointer[1];
            return true;
        });
        shot("steer-cue-far");
        cuePointer[0] = 0.12f; cuePointer[1] = 0.20f;
        shot("steer-cue-near");
        HM::setSteerCueSource(nullptr);
    }
    ImGui::DestroyContext();
    return 0;
}
