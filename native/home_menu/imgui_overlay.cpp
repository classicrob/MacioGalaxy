// Draws the native menu with Dear ImGui. Only ImGui and the menu's own header
// are included here, so ImGui's types never meet the SDK's GX headers.

#include "petari/home_menu.hpp"
#include "petari/launch_stage.hpp"
#include "petari/progress.hpp"
#include "petari/pipeline_startup.hpp"

#include "petari/galaxy_font.hpp"

#include <imgui.h>
#include <imgui_internal.h>  // ShadeVertsLinearColorGradientKeepAlpha, the stb_truetype builder
#ifdef IMGUI_ENABLE_FREETYPE
#include <misc/freetype/imgui_freetype.h>
#endif

#ifdef PETARI_HOME_MENU_INPUT
#include <string>

#include "petari/host_allocation.hpp"
#include "petari/input.hpp"
#endif

#include <algorithm>
#include <atomic>
#include <vector>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

namespace PetariNative::HomeMenu {
namespace App = PetariNative::App;

namespace {

struct ImageSpace {
    float x, y, width, height;

    ImVec2 point(float kx, float ky) const { return {x + (kx + 1.0f) * 0.5f * width, y + (ky + 1.0f) * 0.5f * height}; }
};

ImU32 color(float r, float g, float b, float a) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, std::clamp(a, 0.0f, 1.0f)));
}

// --- The game's fonts in the ImGui atlas (installGalaxyFonts) ---

GalaxyFont::Set gGalaxy;           // kept: the atlas copies glyph pixels from it whenever it is rebuilt
ImFont* gTextFont = nullptr;       // MessageFont26, with the picture icons at U+E000..
ImFont* gTitleFont = nullptr;      // MenuFont64
struct GlyphRect {
    int rect;                      // atlas custom rect index
    const GalaxyFont::Glyph* glyph;
};
std::vector<GlyphRect> gGlyphRects;

const ImFontBuilderIO* baseBuilder() {
#ifdef IMGUI_ENABLE_FREETYPE
    return ImGuiFreeType::GetBuilderForFreeType();
#else
    return ImFontAtlasGetBuilderForStbTruetype();
#endif
}

// The usual build, then the glyph cells copied into their rectangles. The
// atlas becomes RGBA (the picture icons are in color); the rest stays white.
bool buildWithGalaxyGlyphs(ImFontAtlas* atlas) {
    if (!baseBuilder()->FontBuilder_Build(atlas)) {
        return false;
    }
    const int width = atlas->TexWidth;
    const int height = atlas->TexHeight;
    if (atlas->TexPixelsRGBA32 == nullptr) {
        if (atlas->TexPixelsAlpha8 == nullptr) {
            return false;
        }
        atlas->TexPixelsRGBA32 = static_cast<unsigned int*>(IM_ALLOC(static_cast<size_t>(width) * height * 4));
        for (int i = 0; i < width * height; ++i) {
            atlas->TexPixelsRGBA32[i] = IM_COL32(255, 255, 255, atlas->TexPixelsAlpha8[i]);
        }
    }
    for (const GlyphRect& entry : gGlyphRects) {
        const ImFontAtlasCustomRect* rect = atlas->GetCustomRectByIndex(entry.rect);
        const GalaxyFont::Glyph& glyph = *entry.glyph;
        for (int y = 0; y < glyph.height && y < rect->Height; ++y) {
            for (int x = 0; x < glyph.width && x < rect->Width; ++x) {
                const std::uint8_t* p = glyph.rgba.data() + (static_cast<size_t>(y) * glyph.width + x) * 4;
                atlas->TexPixelsRGBA32[static_cast<size_t>(rect->Y + y) * width + rect->X + x] = IM_COL32(p[0], p[1], p[2], p[3]);
            }
        }
    }
    atlas->TexPixelsUseColors = true;
    // Only the RGBA copy is complete. GetTexDataAsRGBA32 would otherwise convert the
    // alpha copy again over it; backends read RGBA.
    if (atlas->TexPixelsAlpha8 != nullptr) {
        IM_FREE(atlas->TexPixelsAlpha8);
        atlas->TexPixelsAlpha8 = nullptr;
    }
    return true;
}

ImFontBuilderIO gGalaxyBuilder = {buildWithGalaxyGlyphs};

constexpr std::uint32_t kIconBase = 0xE000;
constexpr float kBottomBandTop = 0.9f;  // the pause layout's bottom band (KPAD y), the footer inside it  // picture glyph c is U+E000 + (c - 0x20)

ImFont* addGalaxyFont(ImFontAtlas* atlas, const GalaxyFont::Font& font, const char* name, const GalaxyFont::Font* icons) {
    static const ImWchar kSpaceOnly[] = {0x20, 0x20, 0};  // the base font only carries the size; the glyphs are ours
    ImFontConfig config;
    config.SizePixels = static_cast<float>(font.lineFeed);
    config.GlyphRanges = kSpaceOnly;
    std::snprintf(config.Name, sizeof(config.Name), "%s", name);
    ImFont* imFont = atlas->AddFontDefault(&config);
    auto add = [&](const GalaxyFont::Glyph& glyph, ImWchar code, float offsetY) {
        const int rect = atlas->AddCustomRectFontGlyph(imFont, code, std::max(1, glyph.width), std::max(1, glyph.height),
                                                       static_cast<float>(glyph.advance),
                                                       ImVec2(static_cast<float>(glyph.left), offsetY));
        gGlyphRects.push_back({rect, &glyph});
    };
    for (const GalaxyFont::Glyph& glyph : font.glyphs) {
        if (glyph.code <= 0xFFFF) add(glyph, static_cast<ImWchar>(glyph.code), 0.0f);
    }
    if (icons != nullptr) {
        for (const GalaxyFont::Glyph& glyph : icons->glyphs) {
            if (glyph.code >= 0x20 && glyph.code < 0x120) {
                add(glyph, static_cast<ImWchar>(kIconBase + glyph.code - 0x20),
                    std::floor((static_cast<float>(font.lineFeed) - static_cast<float>(glyph.height)) * 0.5f));
            }
        }
    }
    return imFont;
}

ImFont* textFont() {
    return gTextFont != nullptr ? gTextFont : ImGui::GetFont();
}

// The picture icons' stand-ins when the game's fonts are not loaded.
const char* iconFallback(std::uint32_t code) {
    switch (code - kIconBase + 0x20) {
    case GalaxyFont::Picture::A: return "(A)";
    case GalaxyFont::Picture::B: return "(B)";
    case GalaxyFont::Picture::C: return "(C)";
    case GalaxyFont::Picture::Z: return "(Z)";
    case GalaxyFont::Picture::One: return "(1)";
    case GalaxyFont::Picture::Two: return "(2)";
    case GalaxyFont::Picture::Plus: return "(+)";
    case GalaxyFont::Picture::Minus: return "(-)";
    case GalaxyFont::Picture::Home: return "(Home)";
    case GalaxyFont::Picture::Pad: return "(D-pad)";
    case GalaxyFont::Picture::Stick: return "(Stick)";
    case GalaxyFont::Picture::Remote: return "(Shake)";
    default: return "";
    }
}

// An icon's code point if `p` starts one (U+E000..U+E0FF, UTF-8 EE 80..83 xx).
std::uint32_t iconAt(const char* p) {
    const auto* u = reinterpret_cast<const unsigned char*>(p);
    if (u[0] == 0xEE && u[1] >= 0x80 && u[1] <= 0x83 && (u[2] & 0xC0) == 0x80) {
        return 0xE000u | ((u[1] & 0x3Fu) << 6) | (u[2] & 0x3Fu);
    }
    return 0;
}

// Text as drawn: icons replaced by their stand-ins when the game's fonts are
// missing. A fixed buffer: this can run on a game thread.
struct Prepared {
    char text[256];
};
Prepared prepare(const char* text) {
    Prepared out{};
    size_t n = 0;
    for (const char* p = text; *p != '\0' && n + 1 < sizeof(out.text);) {
        const std::uint32_t icon = iconAt(p);
        if (icon != 0 && gTextFont == nullptr) {
            for (const char* f = iconFallback(icon); *f != '\0' && n + 1 < sizeof(out.text); ++f) out.text[n++] = *f;
            p += 3;
        } else {
            out.text[n++] = *p++;
        }
    }
    out.text[n] = '\0';
    return out;
}

ImVec2 measure(ImFont* font, float size, const char* text) {
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
}

// Text with a soft drop shadow, as the game's messages have; icons keep their
// own colors (drawn white).
void richText(ImDrawList* list, ImFont* font, float size, ImVec2 pos, ImU32 col, const char* text, bool shadow = true) {
    const float alpha = ImGui::ColorConvertU32ToFloat4(col).w;
    if (shadow) {
        const float d = std::max(1.0f, size * 0.06f);
        list->AddText(font, size, ImVec2(pos.x + d, pos.y + d), color(0.0f, 0.04f, 0.1f, 0.75f * alpha), text);
    }
    const char* run = text;
    float x = pos.x;
    for (const char* p = text;;) {
        const std::uint32_t icon = *p != '\0' ? iconAt(p) : 0;
        if (*p == '\0' || icon != 0) {
            if (p > run) {
                list->AddText(font, size, ImVec2(x, pos.y), col, run, p);
                x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, run, p).x;
            }
            if (*p == '\0') break;
            list->AddText(font, size, ImVec2(x, pos.y), color(1.0f, 1.0f, 1.0f, alpha), p, p + 3);
            x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, p, p + 3).x;
            p += 3;
            run = p;
        } else {
            ++p;
        }
    }
}

// Fits text into maxWidth (scaling it down) and returns its size.
float fitSize(ImFont* font, float size, float maxWidth, const char* text, ImVec2* extent) {
    *extent = measure(font, size, text);
    if (extent->x > maxWidth && extent->x > 0.0f) {
        size *= maxWidth / extent->x;
        *extent = measure(font, size, text);
    }
    return size;
}

// Text centered on a point, scaled down if needed to fit maxWidth.
void centeredText(ImDrawList* list, ImFont* font, float size, float maxWidth, ImVec2 center, ImU32 col,
                  const char* text, bool shadow = true) {
    if (text == nullptr || text[0] == '\0') {
        return;
    }
    const Prepared prepared = prepare(text);
    ImVec2 extent;
    size = fitSize(font, size, maxWidth, prepared.text, &extent);
    richText(list, font, size, ImVec2(center.x - extent.x * 0.5f, center.y - extent.y * 0.5f), col, prepared.text, shadow);
}

// Text starting at a point's x, vertically centered on it, scaled down if
// needed to fit maxWidth.
void leftText(ImDrawList* list, ImFont* font, float size, float maxWidth, ImVec2 left, ImU32 col, const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return;
    }
    const Prepared prepared = prepare(text);
    ImVec2 extent;
    size = fitSize(font, size, maxWidth, prepared.text, &extent);
    richText(list, font, size, ImVec2(left.x, left.y - extent.y * 0.5f), col, prepared.text);
}

// A centered message, on two lines (split at the space nearest the middle)
// when one line would have to shrink by more than a fifth.
void messageText(ImDrawList* list, ImFont* font, float size, float maxWidth, ImVec2 center, ImU32 col, const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return;
    }
    const Prepared prepared = prepare(text);
    const char* split = nullptr;
    if (measure(font, size, prepared.text).x > maxWidth * 1.25f) {
        const size_t length = std::strlen(prepared.text);
        for (size_t i = 0; i < length; ++i) {
            if (prepared.text[i] == ' ' &&
                (split == nullptr || std::abs(static_cast<long>(i) - static_cast<long>(length / 2)) <
                                         std::abs(static_cast<long>(split - prepared.text) - static_cast<long>(length / 2)))) {
                split = prepared.text + i;
            }
        }
    }
    if (split == nullptr) {
        centeredText(list, font, size, maxWidth, center, col, text);
        return;
    }
    Prepared first{}, second{};
    std::snprintf(first.text, sizeof(first.text), "%.*s", static_cast<int>(split - prepared.text), prepared.text);
    std::snprintf(second.text, sizeof(second.text), "%s", split + 1);
    // One size for both lines: the larger fit.
    ImVec2 e1, e2;
    const float s = std::min(fitSize(font, size, maxWidth, first.text, &e1), fitSize(font, size, maxWidth, second.text, &e2));
    const float line = s * 1.05f;
    e1 = measure(font, s, first.text);
    e2 = measure(font, s, second.text);
    richText(list, font, s, ImVec2(center.x - e1.x * 0.5f, center.y - line), col, first.text);
    richText(list, font, s, ImVec2(center.x - e2.x * 0.5f, center.y), col, second.text);
}

// A page title in the game's outlined title lettering (MenuFont64: white fill,
// black outline, tinted by col), or an outlined fallback.
void titleText(ImDrawList* list, float size, float maxWidth, ImVec2 center, float alpha, const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return;
    }
    ImFont* font = gTitleFont != nullptr ? gTitleFont : textFont();
    ImVec2 extent;
    size = fitSize(font, size, maxWidth, text, &extent);
    const ImVec2 pos(center.x - extent.x * 0.5f, center.y - extent.y * 0.5f);
    const float d = std::max(1.0f, size * 0.05f);
    list->AddText(font, size, ImVec2(pos.x + d, pos.y + d * 1.5f), color(0.0f, 0.05f, 0.12f, 0.6f * alpha), text);
    if (gTitleFont == nullptr) {
        const ImU32 outline = color(0.02f, 0.08f, 0.18f, alpha);
        for (int i = 0; i < 8; ++i) {
            const float a = static_cast<float>(i) * 0.785398f;
            list->AddText(font, size, ImVec2(pos.x + std::cos(a) * d, pos.y + std::sin(a) * d), outline, text);
        }
    }
    list->AddText(font, size, pos, color(1.0f, 1.0f, 1.0f, alpha), text);
}

// A vertical gradient over the shapes added since vertex `start`, alpha kept.
void shade(ImDrawList* list, int start, ImVec2 min, ImVec2 max, ImU32 top, ImU32 bottom) {
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(list, start, list->VtxBuffer.Size, ImVec2(min.x, min.y),
                                                  ImVec2(min.x, max.y), top, bottom);
}

// The menu panel: deep space blue, a cyan rim and a faint inner line, like the
// game's pause bands and window frames.
void galaxyPanel(ImDrawList* list, ImVec2 min, ImVec2 max, float rounding, float unit, float alpha) {
    const int start = list->VtxBuffer.Size;
    // Nearly opaque: whatever the game shows (its own pause menu, if open) stays behind.
    list->AddRectFilled(min, max, color(0.0f, 0.0f, 0.0f, 0.97f * alpha), rounding);
    shade(list, start, min, max, color(0.03f, 0.34f, 0.45f, 1.0f), color(0.01f, 0.14f, 0.24f, 1.0f));
    list->AddRect(min, max, color(0.40f, 0.86f, 1.0f, 0.9f * alpha), rounding, 0, unit * 3.0f);
    const ImVec2 inset(unit * 4.0f, unit * 4.0f);
    list->AddRect(ImVec2(min.x + inset.x, min.y + inset.y), ImVec2(max.x - inset.x, max.y - inset.y),
                  color(1.0f, 1.0f, 1.0f, 0.16f * alpha), std::max(0.0f, rounding - inset.x), 0, unit);
}

// A menu button: the game's white-to-blue capsule with a cyan rim; the focused
// one turns gold and glows, as the game's selected buttons light up.
void galaxyButton(ImDrawList* list, ImFont* font, float textSize, ImVec2 min, ImVec2 max, bool focused, float unit,
                  float alpha, const char* label) {
    const float radius = (max.y - min.y) * 0.5f;
    if (focused) {
        const float glow = unit * 5.0f;
        list->AddRectFilled(ImVec2(min.x - glow, min.y - glow), ImVec2(max.x + glow, max.y + glow),
                            color(1.0f, 0.86f, 0.30f, 0.35f * alpha), radius + glow);
    }
    const int start = list->VtxBuffer.Size;
    list->AddRectFilled(min, max, color(1.0f, 1.0f, 1.0f, (focused ? 1.0f : 0.94f) * alpha), radius);
    if (focused) {
        shade(list, start, min, max, color(1.0f, 1.0f, 0.93f, 1.0f), color(1.0f, 0.84f, 0.36f, 1.0f));
    } else {
        shade(list, start, min, max, color(1.0f, 1.0f, 1.0f, 1.0f), color(0.60f, 0.78f, 0.97f, 1.0f));
    }
    const ImU32 rim = focused ? color(0.98f, 0.80f, 0.22f, alpha) : color(0.22f, 0.74f, 1.0f, 0.95f * alpha);
    list->AddRect(min, max, rim, radius, 0, unit * 3.0f);
    const ImVec2 inset(unit * 3.5f, unit * 3.5f);
    list->AddRect(ImVec2(min.x + inset.x, min.y + inset.y), ImVec2(max.x - inset.x, max.y - inset.y),
                  color(1.0f, 1.0f, 1.0f, 0.7f * alpha), std::max(0.0f, radius - inset.x), 0, unit);
    const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    const float labelWidth = (max.x - min.x) - radius * 1.4f;
    // Dark lettering with a light emboss below it, as on the game's buttons.
    const ImU32 ink = focused ? color(0.30f, 0.17f, 0.02f, alpha) : color(0.20f, 0.25f, 0.34f, alpha);
    centeredText(list, font, textSize, labelWidth, ImVec2(center.x, center.y + unit), color(1.0f, 1.0f, 1.0f, 0.8f * alpha),
                 label, false);
    centeredText(list, font, textSize, labelWidth, center, ink, label, false);
}


// The Controls page's remapper: action names, then two keyboard/mouse and two
// controller slots per row, each a small key cap; the focused slot is a gold
// capsule ("Press..." while it waits for an input).
void drawRemapTable(ImDrawList* list, ImFont* font, const ImageSpace& image, const RemapView& remap, float unit, float alpha) {
    const ImVec2 areaMin = image.point(remap.area.x0, remap.area.y0);
    const ImVec2 areaMax = image.point(remap.area.x1, remap.area.y1);
    const float rowHeight = (areaMax.y - areaMin.y) / static_cast<float>(kRemapVisibleRows + 1);
    const float textSize = std::min(rowHeight * 0.66f, image.height * 0.032f);
    const float pad = (areaMax.x - areaMin.x) * 0.012f;
    const float labelRight = image.point(remap.cells[0][0].x0, 0.0f).x;
    const float rounding = rowHeight * 0.3f;

    // Header: the two device columns, and where the list is.
    list->AddRectFilled(areaMin, ImVec2(areaMax.x, areaMin.y + rowHeight), color(0.35f, 0.80f, 1.0f, 0.22f * alpha), rounding);
    const float headerY = areaMin.y + rowHeight * 0.5f;
    const ImU32 headerColor = color(0.60f, 0.92f, 1.0f, alpha);
    const float keysX0 = image.point(remap.cells[0][0].x0, 0.0f).x, keysX1 = image.point(remap.cells[0][1].x1, 0.0f).x;
    const float padsX0 = image.point(remap.cells[0][2].x0, 0.0f).x, padsX1 = image.point(remap.cells[0][3].x1, 0.0f).x;
    centeredText(list, font, textSize, keysX1 - keysX0, ImVec2((keysX0 + keysX1) * 0.5f, headerY), headerColor, "Keyboard and mouse");
    centeredText(list, font, textSize, padsX1 - padsX0, ImVec2((padsX0 + padsX1) * 0.5f, headerY), headerColor, "Controller");
    char where[48];
    std::snprintf(where, sizeof(where), "%d-%d of %d", remap.firstRow + 1, remap.firstRow + remap.rowCount, remap.totalRows);
    leftText(list, font, textSize * 0.85f, labelRight - areaMin.x - 2.0f * pad, ImVec2(areaMin.x + pad, headerY),
             color(0.75f, 0.88f, 1.0f, 0.85f * alpha), where);
    // The scrollbar: arrow buttons at both ends, the track, and a capsule thumb
    // (gold while dragged). Dimmed when every row fits.
    const bool scrollable = remap.totalRows > remap.rowCount;
    const float barAlpha = (scrollable ? 1.0f : 0.35f) * alpha;
    const ImVec2 trackMin = image.point(remap.track.x0, remap.track.y0);
    const ImVec2 trackMax = image.point(remap.track.x1, remap.track.y1);
    const float barWidth = trackMax.x - trackMin.x;
    list->AddRectFilled(trackMin, trackMax, color(0.0f, 0.05f, 0.12f, 0.45f * barAlpha), barWidth * 0.5f);
    list->AddRect(trackMin, trackMax, color(0.40f, 0.86f, 1.0f, 0.45f * barAlpha), barWidth * 0.5f, 0, unit);
    const ImVec2 thumbMin = image.point(remap.thumb.x0, remap.thumb.y0);
    const ImVec2 thumbMax = image.point(remap.thumb.x1, remap.thumb.y1);
    {
        const int start = list->VtxBuffer.Size;
        list->AddRectFilled(ImVec2(thumbMin.x + unit, thumbMin.y), ImVec2(thumbMax.x - unit, thumbMax.y),
                            color(1.0f, 1.0f, 1.0f, barAlpha), barWidth * 0.5f);
        if (remap.dragging) {
            shade(list, start, thumbMin, thumbMax, color(1.0f, 1.0f, 0.93f, 1.0f), color(1.0f, 0.84f, 0.36f, 1.0f));
        } else {
            shade(list, start, thumbMin, thumbMax, color(1.0f, 1.0f, 1.0f, 1.0f), color(0.60f, 0.78f, 0.97f, 1.0f));
        }
        list->AddRect(ImVec2(thumbMin.x + unit, thumbMin.y), ImVec2(thumbMax.x - unit, thumbMax.y),
                      remap.dragging ? color(0.98f, 0.80f, 0.22f, barAlpha) : color(0.22f, 0.74f, 1.0f, barAlpha), barWidth * 0.5f,
                      0, unit * 1.5f);
    }
    for (int end = 0; end < 2; end++) {
        const Rect& r = end == 0 ? remap.upArrow : remap.downArrow;
        const ImVec2 min = image.point(r.x0, r.y0), max = image.point(r.x1, r.y1);
        const ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        const float radius = std::min(max.x - min.x, max.y - min.y) * 0.5f;
        list->AddCircleFilled(c, radius, color(0.20f, 0.55f, 0.75f, 0.7f * barAlpha));
        list->AddCircle(c, radius, color(0.55f, 0.92f, 1.0f, barAlpha), 0, unit * 1.5f);
        const bool more = end == 0 ? remap.firstRow > 0 : remap.firstRow + remap.rowCount < remap.totalRows;
        const float t = radius * 0.5f, dir = end == 0 ? -1.0f : 1.0f;
        list->AddTriangleFilled(ImVec2(c.x, c.y + dir * t), ImVec2(c.x - t, c.y - dir * t * 0.6f), ImVec2(c.x + t, c.y - dir * t * 0.6f),
                                color(1.0f, 1.0f, 1.0f, (more ? 1.0f : 0.35f) * barAlpha));
    }

    const float pulse = 0.65f + 0.35f * std::sin(static_cast<float>(ImGui::GetTime()) * 6.0f);
    for (int r = 0; r < std::clamp(remap.rowCount, 0, kRemapVisibleRows); r++) {
        const RemapRow& row = remap.rows[r];
        const float top = areaMin.y + rowHeight * static_cast<float>(r + 1);
        const float middle = top + rowHeight * 0.5f;
        if (r % 2 == 0) {
            list->AddRectFilled(ImVec2(areaMin.x, top), ImVec2(areaMax.x, top + rowHeight), color(1.0f, 1.0f, 1.0f, 0.06f * alpha),
                                rounding);
        }
        if (r == remap.selectedRow) {
            list->AddRect(ImVec2(areaMin.x, top), ImVec2(areaMax.x, top + rowHeight), color(0.98f, 0.80f, 0.22f, 0.55f * alpha),
                          rounding, 0, unit * 1.5f);
        }
        // Changed from the default: the name in gold.
        leftText(list, font, textSize, labelRight - areaMin.x - 2.0f * pad, ImVec2(areaMin.x + pad, middle),
                 row.changed ? color(1.0f, 0.84f, 0.42f, alpha) : color(0.90f, 0.96f, 1.0f, alpha), row.label);
        for (int slot = 0; slot < kRemapSlots; slot++) {
            const ImVec2 min = image.point(remap.cells[r][slot].x0, remap.cells[r][slot].y0);
            const ImVec2 max = image.point(remap.cells[r][slot].x1, remap.cells[r][slot].y1);
            const bool focused = r == remap.focusRow && slot == remap.focusSlot;
            const char* text = row.slots[slot];
            if (focused) {
                galaxyButton(list, font, textSize, min, max, true, unit * 0.6f, remap.capturing ? alpha * pulse : alpha,
                             remap.capturing ? "Press..." : (text[0] != '\0' ? text : "-"));
                continue;
            }
            const bool conflict = row.conflict[slot];
            const float capRounding = (max.y - min.y) * 0.35f;
            list->AddRectFilled(min, max, color(0.75f, 0.90f, 1.0f, (text[0] != '\0' ? 0.13f : 0.05f) * alpha), capRounding);
            if (r == remap.selectedRow && slot == remap.selectedSlot) {
                list->AddRect(min, max, color(0.98f, 0.80f, 0.22f, alpha), capRounding, 0, unit * 2.0f);
            } else if (conflict) {
                list->AddRect(min, max, color(1.0f, 0.55f, 0.25f, 0.9f * alpha), capRounding, 0, unit * 1.5f);
            }
            const ImU32 ink = text[0] == '\0' ? color(0.60f, 0.72f, 0.85f, 0.5f * alpha)
                              : conflict      ? color(1.0f, 0.68f, 0.40f, alpha)
                              : slot < 2      ? color(1.0f, 0.92f, 0.55f, alpha)
                                              : color(0.62f, 0.86f, 1.0f, alpha);
            centeredText(list, font, textSize * 0.92f, (max.x - min.x) * 0.9f, ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f),
                         ink, text[0] != '\0' ? text : "-");
        }
    }
}

}  // namespace

bool installGalaxyFonts(const char* fontArchive, std::string* error) {
    if (ImGui::GetCurrentContext() == nullptr) {
        if (error != nullptr) *error = "no ImGui context";
        return false;
    }
    if (gTextFont != nullptr) {
        return true;
    }
    if (!GalaxyFont::load(fontArchive, &gGalaxy, error)) {
        gGalaxy = GalaxyFont::Set{};
        return false;
    }
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    if (atlas->Fonts.Size == 0) {
        atlas->AddFontDefault();  // ImGui's own font stays the default for anything else drawn with ImGui
    }
    gGlyphRects.clear();
    gTextFont = addGalaxyFont(atlas, gGalaxy.message, "Galaxy MessageFont26", &gGalaxy.picture);
    gTitleFont = addGalaxyFont(atlas, gGalaxy.menu, "Galaxy MenuFont64", nullptr);
    atlas->FontBuilderIO = &gGalaxyBuilder;
    return true;
}

bool galaxyFontsInstalled() {
    return gTextFont != nullptr;
}

ImFont* galaxyTextFont() {
    return gTextFont;
}

ImFont* galaxyTitleFont() {
    return gTitleFont;
}

Rect titleHintRect() {
    // Above the logo: the title's own text ("Press both A and B", the
    // copyright) fills the bottom of the image.
    return {-0.62f, -0.955f, 0.62f, -0.855f};
}

void drawTitleHint(const char* text, float imageX, float imageY, float imageWidth, float imageHeight) {
    if (text == nullptr || text[0] == '\0' || !(imageWidth > 0.0f) || !(imageHeight > 0.0f)) {
        return;
    }
    ImDrawList* list = ImGui::GetForegroundDrawList();
    const ImageSpace image{imageX, imageY, imageWidth, imageHeight};
    const Rect r = titleHintRect();
    const ImVec2 barMin = image.point(r.x0, r.y0);
    const ImVec2 barMax = image.point(r.x1, r.y1);
    list->PushClipRect(image.point(-1.0f, -1.0f), image.point(1.0f, 1.0f), false);
    list->AddRectFilled(barMin, barMax, color(0.02f, 0.03f, 0.08f, 0.62f), (barMax.y - barMin.y) * 0.5f);
    centeredText(list, textFont(), imageHeight * 0.034f, (barMax.x - barMin.x) * 0.92f,
                 ImVec2((barMin.x + barMax.x) * 0.5f, (barMin.y + barMax.y) * 0.5f), color(1.0f, 1.0f, 1.0f, 0.95f),
                 text);
    list->PopClipRect();
}

// My Progress badges (petari/progress.hpp): a check and the best time under each mission star the player cleared
// themselves, while the mission select is up. Positions come from the game's own star panes (normalized game-image
// coordinates), so they follow the image at any window size.
void drawProgressBadges(float imageX, float imageY, float imageWidth, float imageHeight) {
    std::vector<Progress::BadgeStar> stars;
    std::string stage;
    if (!(imageWidth > 0.0f) || !(imageHeight > 0.0f) || !Progress::currentBadges(&stars, &stage)) {
        return;
    }
    ImDrawList* list = ImGui::GetForegroundDrawList();
    ImFont* font = textFont();
    list->PushClipRect(ImVec2(imageX, imageY), ImVec2(imageX + imageWidth, imageY + imageHeight), false);
    const float textSize = imageHeight * 0.03f;
    const float height = imageHeight * 0.045f;
    for (const Progress::BadgeStar& star : stars) {
        if (!star.cleared) {
            continue;
        }
        char text[32];
        const int whole = static_cast<int>(star.bestTimeS);
        std::snprintf(text, sizeof(text), "%d:%02d.%d", whole / 60, whole % 60, static_cast<int>((star.bestTimeS - whole) * 10.0));
        const ImVec2 extent = font->CalcTextSizeA(textSize, 1.0e9f, 0.0f, text);
        const float pad = height * 0.35f;
        const float width = height + extent.x + pad * 2.0f;
        const ImVec2 center(imageX + star.u * imageWidth, imageY + star.v * imageHeight + imageHeight * 0.085f);
        const ImVec2 min(center.x - width * 0.5f, center.y - height * 0.5f);
        const ImVec2 max(center.x + width * 0.5f, center.y + height * 0.5f);
        list->AddRectFilled(min, max, color(0.02f, 0.03f, 0.08f, 0.82f), height * 0.5f);
        list->AddRect(min, max, color(0.35f, 0.85f, 0.45f, 0.9f), height * 0.5f, 0, imageHeight * 0.002f);
        const ImVec2 badge(min.x + height * 0.5f, center.y);
        list->AddCircleFilled(badge, height * 0.34f, color(0.2f, 0.72f, 0.32f, 1.0f));
        const float unit = height * 0.34f;
        list->AddPolyline(std::array<ImVec2, 3>{ImVec2(badge.x - unit * 0.5f, badge.y), ImVec2(badge.x - unit * 0.12f, badge.y + unit * 0.4f),
                                                  ImVec2(badge.x + unit * 0.55f, badge.y - unit * 0.4f)}.data(),
                          3, color(1.0f, 1.0f, 1.0f, 1.0f), 0, imageHeight * 0.004f);
        list->AddText(font, textSize, ImVec2(min.x + height + pad * 0.6f, center.y - extent.y * 0.5f), color(1.0f, 1.0f, 1.0f, 0.97f), text);
    }
    // "x/y cleared by you" for the galaxy, in a corner of the image where the mission select leaves room.
    if (const App::LaunchStage::Galaxy* galaxy = App::LaunchStage::find(stage)) {
        int cleared = 0;
        for (int mission = 1; mission <= galaxy->missions; ++mission) {
            cleared += Progress::find(galaxy->stage, mission, nullptr) ? 1 : 0;
        }
        char text[64];
        std::snprintf(text, sizeof(text), "%d/%d cleared by you", cleared, galaxy->missions);
        const float size = imageHeight * 0.03f;
        const ImVec2 extent = font->CalcTextSizeA(size, 1.0e9f, 0.0f, text);
        const float padX = size * 0.8f, padY = size * 0.4f;
        const ImVec2 min(imageX + imageWidth * 0.02f, imageY + imageHeight * 0.02f);
        const ImVec2 max(min.x + extent.x + padX * 2.0f, min.y + extent.y + padY * 2.0f);
        list->AddRectFilled(min, max, color(0.02f, 0.03f, 0.08f, 0.82f), (max.y - min.y) * 0.5f);
        list->AddRect(min, max, color(0.35f, 0.85f, 0.45f, 0.9f), (max.y - min.y) * 0.5f, 0, imageHeight * 0.002f);
        list->AddText(font, size, ImVec2(min.x + padX, min.y + padY), color(1.0f, 1.0f, 1.0f, 0.97f), text);
    }
    list->PopClipRect();
}

#ifdef PETARI_HOME_MENU_INPUT
extern "C" float petari_skip_progress(void);  // native/input/skip.cpp: the hold-to-skip mod's progress
#endif

namespace {
#ifdef PETARI_HOME_MENU_INPUT
std::atomic<HoldProgressFn> gHoldProgress{&petari_skip_progress};
#else
std::atomic<HoldProgressFn> gHoldProgress{nullptr};
#endif
}  // namespace

void setHoldProgressSource(HoldProgressFn source) {
    gHoldProgress.store(source, std::memory_order_relaxed);
}

void drawHoldRing(float progress, float imageX, float imageY, float imageWidth, float imageHeight, const char* key) {
    if (!(progress > 0.0f) || !(imageWidth > 0.0f) || !(imageHeight > 0.0f) || !ImGui::GetCurrentContext()) {
        return;
    }
    progress = std::min(progress, 1.0f);
    const ImageSpace image{imageX, imageY, imageWidth, imageHeight};
    const float unit = imageHeight / 720.0f;
    // A tap in a cutscene barely shows: the ring fades in over the first tenth of the hold.
    const float alpha = std::clamp(progress / 0.1f, 0.0f, 1.0f);
    const float radius = unit * 30.0f;
    const ImVec2 center = image.point(0.92f, -0.80f);
    const bool full = progress >= 1.0f;
    ImDrawList* list = ImGui::GetForegroundDrawList();
    list->PushClipRect(image.point(-1.0f, -1.0f), image.point(1.0f, 1.0f), false);
    list->AddCircleFilled(center, radius + unit * 5.0f, color(0.0f, 0.10f, 0.18f, 0.62f * alpha), 48);
    list->AddCircle(center, radius, color(0.55f, 0.85f, 1.0f, 0.28f * alpha), 48, unit * 5.0f);
    const float start = -1.5707963f;  // twelve o'clock
    list->PathArcTo(center, radius, start, start + 6.2831853f * progress, 48);
    list->PathStroke(full ? color(1.0f, 0.92f, 0.45f, alpha) : color(0.40f, 0.86f, 1.0f, alpha), 0, unit * 5.0f);
    if (key != nullptr && key[0] != '\0') {
        // A keycap's worth of text inside the ring: shrinks to fit longer names ("Left Shift").
        centeredText(list, textFont(), unit * 17.0f, radius * 1.65f, center, color(1.0f, 1.0f, 1.0f, 0.95f * alpha), key);
    } else {
        centeredText(list, textFont(), unit * 30.0f, radius * 1.6f, center, color(1.0f, 1.0f, 1.0f, 0.95f * alpha), PETARI_ICON_A);
    }
    list->PopClipRect();
}

#ifdef PETARI_HOME_MENU_INPUT
extern "C" bool petari_point_move_cue(float*, float*, float*, float*);  // native/input/point_move.cpp
#endif

namespace {
#ifdef PETARI_HOME_MENU_INPUT
std::atomic<SteerCueFn> gSteerCue{&petari_point_move_cue};
#else
std::atomic<SteerCueFn> gSteerCue{nullptr};
#endif
}  // namespace

void setSteerCueSource(SteerCueFn source) {
    gSteerCue.store(source, std::memory_order_relaxed);
}

void drawSteerCue(float marioX, float marioY, float pointerX, float pointerY, float imageX, float imageY, float imageWidth,
                  float imageHeight) {
    if (!(imageWidth > 0.0f) || !(imageHeight > 0.0f) || !ImGui::GetCurrentContext() || !std::isfinite(marioX) ||
        !std::isfinite(marioY) || !std::isfinite(pointerX) || !std::isfinite(pointerY)) {
        return;
    }
    const ImageSpace image{imageX, imageY, imageWidth, imageHeight};
    const float unit = imageHeight / 720.0f;
    const ImVec2 from = image.point(marioX, marioY);
    const ImVec2 to = image.point(pointerX, pointerY);
    const float dx = to.x - from.x, dy = to.y - from.y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    // Inside Mario's dead zone (3% of the screen height) he does not move: no cue. The cue fades in
    // over the next few percent so it never pops.
    const float deadZone = imageHeight * 0.03f;
    const float alpha = std::clamp((distance - deadZone) / (imageHeight * 0.04f), 0.0f, 1.0f);
    if (alpha <= 0.0f) {
        return;
    }
    const ImVec2 dir(dx / distance, dy / distance);
    const ImVec2 side(-dir.y, dir.x);
    // From a little ahead of Mario, up to a fifth of the screen toward the pointer.
    const float start = unit * 34.0f;
    const float length = std::clamp(distance - start - unit * 10.0f, 0.0f, imageHeight * 0.2f);
    if (length < unit * 12.0f) {
        return;
    }
    const ImVec2 a(from.x + dir.x * start, from.y + dir.y * start);
    const ImVec2 tip(a.x + dir.x * length, a.y + dir.y * length);
    const float head = unit * 18.0f;
    const ImVec2 base(tip.x - dir.x * head, tip.y - dir.y * head);
    ImDrawList* list = ImGui::GetForegroundDrawList();
    list->PushClipRect(image.point(-1.0f, -1.0f), image.point(1.0f, 1.0f), false);
    // A dark underlay keeps it readable on light ground; the light arrow itself stays translucent.
    for (const bool shadow : {true, false}) {
        const float grow = shadow ? unit * 2.0f : 0.0f;
        const ImU32 col = shadow ? color(0.0f, 0.05f, 0.1f, 0.35f * alpha) : color(1.0f, 1.0f, 1.0f, 0.62f * alpha);
        list->AddLine(a, base, col, unit * 5.0f + grow * 2.0f);
        list->AddTriangleFilled(ImVec2(tip.x + dir.x * grow, tip.y + dir.y * grow),
                                ImVec2(base.x + side.x * (head * 0.62f + grow), base.y + side.y * (head * 0.62f + grow)),
                                ImVec2(base.x - side.x * (head * 0.62f + grow), base.y - side.y * (head * 0.62f + grow)), col);
    }
    list->PopClipRect();
}

void drawImGuiOverlay(float imageX, float imageY, float imageWidth, float imageHeight) {
    const View view = publishedView();
    if (!view.visible) {
        if (const SteerCueFn source = gSteerCue.load(std::memory_order_relaxed)) {
            float mx = 0.0f, my = 0.0f, px = 0.0f, py = 0.0f;
            if (source(&mx, &my, &px, &py)) {
                drawSteerCue(mx, my, px, py, imageX, imageY, imageWidth, imageHeight);
            }
        }
        if (const HoldProgressFn source = gHoldProgress.load(std::memory_order_relaxed)) {
            const float progress = source();
            const char* key = nullptr;
#ifdef PETARI_HOME_MENU_INPUT
            // The key bound to A (Space by default), looked up when a hold starts.
            static bool sHolding = false;
            static std::string sKey;
            if (progress > 0.0f && !sHolding) {
                HostAllocationScope scope;  // this may be a game thread
                sKey.clear();
                const Input::Bindings bindings = Input::bindings();  // a copy: keep it alive through the loop
                for (const Input::Binding& input : bindings.inputs(Input::Action::A)) {
                    if (input.device == Input::Binding::Device::Key) {
                        sKey = Input::displayName(input);
                        break;
                    }
                }
            }
            sHolding = progress > 0.0f;
            // A player on the controller sees the A icon, not the keyboard's key.
            key = Input::lastDevice() == Input::Binding::Device::Pad ? nullptr : sKey.c_str();
#endif
            drawHoldRing(progress, imageX, imageY, imageWidth, imageHeight, key);
        }
    }
#ifdef PETARI_HOME_MENU_INPUT
    // The title's "press A and B": say which keys do that. The text is built
    // from the bindings each time the prompt appears.
    static bool sPromptWasActive = false;
    static std::string sHint;
    const bool prompt = Input::titlePromptActive();
    if (prompt && !sPromptWasActive) {
        HostAllocationScope scope;  // this may be a game thread
        sHint = Input::titleHint(Input::bindings());
    }
    sPromptWasActive = prompt;
    if (prompt && !view.visible) {
        drawTitleHint(sHint.c_str(), imageX, imageY, imageWidth, imageHeight);
    }
#endif
    if (!view.visible) {
        drawProgressBadges(imageX, imageY, imageWidth, imageHeight);
    }
    if (!view.visible || !(imageWidth > 0.0f) || !(imageHeight > 0.0f)) {
        return;
    }

    // The foreground list draws over everything without a window, so the
    // overlay never takes ImGui focus or input.
    ImDrawList* list = ImGui::GetForegroundDrawList();
    ImFont* font = textFont();
    const ImageSpace image{imageX, imageY, imageWidth, imageHeight};
    const ImVec2 imageMin = image.point(-1.0f, -1.0f);
    const ImVec2 imageMax = image.point(1.0f, 1.0f);
    const float unit = imageHeight / 720.0f;  // one pixel of a 720-line image
    list->PushClipRect(imageMin, imageMax, false);

    if (view.bands) {
        // As the game's pause screen: translucent teal bands top and bottom, the scene
        // clear between them.
        const float a = view.dimOpacity * 1.6f;
        const ImVec2 topMax(imageMax.x, image.point(0.0f, view.panel.y1).y);
        const ImVec2 bottomMin(imageMin.x, image.point(0.0f, kBottomBandTop).y);
        list->AddRectFilled(imageMin, topMax, color(0.03f, 0.43f, 0.53f, a));
        list->AddRectFilled(bottomMin, imageMax, color(0.03f, 0.43f, 0.53f, a));
        list->AddLine(ImVec2(imageMin.x, topMax.y), topMax, color(0.55f, 0.92f, 1.0f, a * 0.9f), unit * 2.0f);
        list->AddLine(bottomMin, ImVec2(imageMax.x, bottomMin.y), color(0.55f, 0.92f, 1.0f, a * 0.9f), unit * 2.0f);
    } else if (view.dimOpacity > 0.0f) {
        // The game's pause screen tints the scene deep teal rather than grey.
        list->AddRectFilled(imageMin, imageMax, color(0.0f, 0.16f, 0.26f, view.dimOpacity * 1.3f));
    }

    const float alpha = view.panelOpacity;
    if (alpha > 0.0f) {
        const float rounding = imageHeight * 0.035f;
        const ImVec2 panelMin = image.point(view.panel.x0, view.panel.y0);
        const ImVec2 panelMax = image.point(view.panel.x1, view.panel.y1);
        if (!view.bands) {
            galaxyPanel(list, panelMin, panelMax, rounding, unit, alpha);
        }

        const float panelCenterX = (panelMin.x + panelMax.x) * 0.5f;
        const float textWidth = (panelMax.x - panelMin.x) * 0.9f;
        const float titleSize = imageHeight * 0.062f;
        const float messageSize = imageHeight * 0.034f;
        const float itemSize = imageHeight * 0.04f;
        const float titleY = image.point(0.0f, view.titleY).y;
        const float messageY = image.point(0.0f, view.messageY).y;
        titleText(list, titleSize, textWidth, ImVec2(panelCenterX, titleY), alpha, view.title);
        messageText(list, font, messageSize, textWidth, ImVec2(panelCenterX, messageY), color(0.80f, 0.93f, 1.0f, alpha),
                    view.message);

        // Controls page: action names on the left, inputs on the right, one
        // row each, with a light band on every other row for reading across.
        // Clamped to the array: a corrupt count must not read past View::lines.
        const int lineCount = std::clamp(view.lineCount, 0, kMaxControls);
        if (lineCount > 0) {
            const ImVec2 areaMin = image.point(view.linesArea.x0, view.linesArea.y0);
            const ImVec2 areaMax = image.point(view.linesArea.x1, view.linesArea.y1);
            // Few lines keep a normal row height from the top rather than stretching.
            const float rowHeight = std::min((areaMax.y - areaMin.y) / static_cast<float>(lineCount), imageHeight * 0.064f);
            const float areaWidth = areaMax.x - areaMin.x;
            const float textSize = std::min(rowHeight * 0.78f, imageHeight * 0.036f);
            const float pad = areaWidth * 0.02f;
            bool padColumn = false;
            for (int i = 0; i < lineCount; i++) {
                padColumn = padColumn || view.lines[i].pad[0] != '\0';
            }
            // Action | keyboard and mouse [| controller].
            const float split = areaMin.x + areaWidth * (padColumn ? 0.3f : 0.42f);
            const float split2 = padColumn ? areaMin.x + areaWidth * 0.69f : areaMax.x;
            const float bandRounding = rowHeight * 0.3f;
            for (int i = 0; i < lineCount; i++) {
                const float top = areaMin.y + rowHeight * static_cast<float>(i);
                const float middle = top + rowHeight * 0.5f;
                const bool header = view.lines[i].action[0] == '\0';
                if (header) {
                    list->AddRectFilled(ImVec2(areaMin.x, top), ImVec2(areaMax.x, top + rowHeight),
                                        color(0.35f, 0.80f, 1.0f, 0.22f * alpha), bandRounding);
                } else if (i % 2 == 0) {
                    list->AddRectFilled(ImVec2(areaMin.x, top), ImVec2(areaMax.x, top + rowHeight),
                                        color(1.0f, 1.0f, 1.0f, 0.07f * alpha), bandRounding);
                }
                const ImU32 headerColor = color(0.60f, 0.92f, 1.0f, alpha);
                leftText(list, font, textSize, split - areaMin.x - 2.0f * pad, ImVec2(areaMin.x + pad, middle),
                         color(0.90f, 0.96f, 1.0f, alpha), view.lines[i].action);
                leftText(list, font, textSize, split2 - split - 2.0f * pad, ImVec2(split + pad, middle),
                         header ? headerColor : color(1.0f, 0.92f, 0.55f, alpha), view.lines[i].inputs);
                if (padColumn) {
                    leftText(list, font, textSize, areaMax.x - split2 - 2.0f * pad, ImVec2(split2 + pad, middle),
                             header ? headerColor : color(0.62f, 0.86f, 1.0f, alpha), view.lines[i].pad);
                }
            }
        }

        if (view.remap.active) {
            drawRemapTable(list, font, image, view.remap, unit, alpha);
        }

        for (int i = 0; i < std::clamp(view.itemCount, 0, kMaxItems); i++) {
            const ViewItem& item = view.items[i];
            const ImVec2 itemMin = image.point(item.rect.x0, item.rect.y0), itemMax = image.point(item.rect.x1, item.rect.y1);
            // A page with many rows (the Mods page with a dozen mods) has short buttons: the label shrinks
            // with them instead of crowding the edges. Normal pages keep itemSize.
            const float labelSize = std::min(itemSize, (itemMax.y - itemMin.y) * 0.78f);
            galaxyButton(list, font, labelSize, itemMin, itemMax, item.focused, unit, alpha, item.label);
        }

        // The buttons that work here, along the bottom of the image, as the
        // game's own screens show them.
        if (view.footer != nullptr && view.footer[0] != '\0') {
            const ImVec2 bandMin = image.point(-1.0f, 0.915f);
            if (!view.bands) list->AddRectFilledMultiColor(bandMin, imageMax, color(0.0f, 0.12f, 0.22f, 0.0f), color(0.0f, 0.12f, 0.22f, 0.0f),
                                          color(0.0f, 0.12f, 0.22f, 0.85f * alpha), color(0.0f, 0.12f, 0.22f, 0.85f * alpha));
            centeredText(list, font, imageHeight * 0.03f, imageWidth * 0.94f,
                         ImVec2((imageMin.x + imageMax.x) * 0.5f, image.point(0.0f, 0.958f).y),
                         color(0.92f, 0.97f, 1.0f, alpha), view.footer);
        }
    }

    if (view.blackOpacity > 0.0f) {
        list->AddRectFilled(imageMin, imageMax, color(0.0f, 0.0f, 0.0f, view.blackOpacity));
    }

    list->PopClipRect();
}

bool drawPipelinePreparation(const PipelinePreparationView& view, float width, float height) {
    if (!ImGui::GetCurrentContext() || width <= 0 || height <= 0) return false;
    ImDrawList* list = ImGui::GetForegroundDrawList();
    ImFont* font = textFont();
    const float scale = std::min(width / 1280.0f, height / 720.0f);
    const float center = width * 0.5f;
    const float contentWidth = width * 0.82f;
    const float fraction = view.total ? std::clamp(float(view.completed) / view.total, 0.0f, 1.0f) : 1.0f;
    list->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(width, height),
        color(0.025f, 0.045f, 0.11f, 1), color(0.025f, 0.045f, 0.11f, 1),
        color(0.08f, 0.12f, 0.23f, 1), color(0.08f, 0.12f, 0.23f, 1));
    centeredText(list, font, 34 * scale, contentWidth, ImVec2(center, height * 0.34f),
                 color(0.96f, 0.98f, 1, 1), "Preparing shaders for smooth play");
    centeredText(list, font, 21 * scale, contentWidth, ImVec2(center, height * 0.405f),
                 color(0.65f, 0.76f, 0.91f, 1), view.global ? "This may take a few minutes after installation or updates" : "Preparing saved shaders");
    char count[96];
    std::snprintf(count, sizeof(count), "%u / %u  (%u%%)", view.completed, view.total,
                  static_cast<unsigned>(fraction * 100));
    centeredText(list, font, 25 * scale, contentWidth, ImVec2(center, height * 0.47f),
                 color(0.94f, 0.97f, 1, 1), count);
    const ImVec2 barMin(width * 0.21f, height * 0.515f), barMax(width * 0.79f, height * 0.537f);
    list->AddRectFilled(barMin, barMax, color(0.16f, 0.22f, 0.34f, 1), 7 * scale);
    if (fraction > 0)
        list->AddRectFilled(barMin, ImVec2(barMin.x + (barMax.x - barMin.x) * fraction, barMax.y),
                            color(0.45f, 0.75f, 1, 1), 7 * scale);
    char estimate[96] = "Estimating time remaining...";
    if (std::isfinite(view.remainingSeconds) && view.remainingSeconds >= 0) {
        const unsigned seconds = static_cast<unsigned>(std::min(59999.0, std::ceil(view.remainingSeconds)));
        if (seconds < 60) std::snprintf(estimate, sizeof(estimate), "About %u seconds remaining", seconds);
        else std::snprintf(estimate, sizeof(estimate), "About %u min %02u sec remaining", seconds / 60, seconds % 60);
    }
    centeredText(list, font, 20 * scale, contentWidth, ImVec2(center, height * 0.585f),
                 color(0.65f, 0.76f, 0.91f, 1), estimate);
    centeredText(list, font, 22 * scale, contentWidth, ImVec2(center, height * 0.70f),
                 color(0.94f, 0.89f, 0.65f, 1), "Press Return to start now (the rest keeps preparing in the background)");
    centeredText(list, font, 18 * scale, contentWidth, ImVec2(center, height * 0.75f),
                 color(0.65f, 0.76f, 0.91f, 1), "Controller A / Start also works");
    return true;
}

}  // namespace PetariNative::HomeMenu
