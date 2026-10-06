/* Presentation interface between the SDK side (present_host.cpp, which reads
 * the native VI and SC state) and the Aurora side (present_aurora.cpp, which
 * the patched aurora_end_frame uses). Plain C, so neither side sees the
 * other's GX headers. Main thread only. */
#ifndef PETARI_GX_PRESENT_H
#define PETARI_GX_PRESENT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PetariPresentVideo {
    const void* xfb;             /* XFB latched by VI (VIGetCurrentFrameBuffer) */
    int configured;              /* a render mode is latched */
    int black;                   /* VI output blanked (VISetBlack) */
    int dimmed;                  /* screen-saver dimming active */
    unsigned aspectWidth;        /* display aspect: 16:9 or 4:3 */
    unsigned aspectHeight;
} PetariPresentVideo;

/* Starts presenting XFBs: until then aurora_end_frame presents the EFB as
 * upstream Aurora does. */
void petari_present_enable(void);

/* The video state for the frame the next aurora_end_frame presents. */
void petari_present_set_video(const PetariPresentVideo* video);

/* The displayed image's aspect (16:9 or 4:3) while XFBs are presented, for
 * the patched aurora::window::get_window_size: the EFB render target takes
 * this aspect inside the drawable, so the XFB is shown pixel for pixel.
 * Returns 0 before petari_present_enable. Any thread. */
int petari_present_content_aspect(unsigned* width, unsigned* height);

/* Where the game image is presented, in window points. Returns 0 while there
 * is no surface. */
int petari_present_image_rect(float* x, float* y, float* width, float* height);

/* The level editor's docked viewport (docs/dev/level-editor/TECH.md 3.6): the
 * game image is fitted into this rectangle of the window (points) instead of
 * the whole window, and the EFB takes the rectangle's pixel size. A width or
 * height <= 0 goes back to the whole window. Size changes resize the EFB once
 * they have settled (a splitter drag resizes it once, not every frame).
 * Main thread. */
void petari_present_set_content_area(float x, float y, float width, float height);

/* The content area's size in drawable pixels while one is set, for the patched
 * aurora::window::get_window_size. Returns 0 when there is none. Any thread. */
int petari_present_content_area_pixels(unsigned* width, unsigned* height);

/* While enabled (and a content area is set), the game renders at the content
 * area's shape: the EFB and the presented image take its aspect, and the
 * game's projection follows (petari/editor_view.hpp). The editor enables it
 * while editing, when the HUD is hidden (2D layouts would stretch). Main thread. */
void petari_present_set_free_aspect(int enabled);

/* The level editor's thumbnails (docs/dev/level-editor/TECH.md 3.5): read back the RGBA8 image the
 * game copied with GXCopyTex to `copy` this frame, once the frame has run. Pixels of the key colour
 * (255, 0, 255) come back transparent. Game thread (the copy's frame). */
void petari_present_request_readback(const void* copy);
/* A finished readback of `copy`: width * height * 4 bytes of RGBA, rows top first. 1 when it was ready
 * (and is then forgotten), 0 while pending or unknown, -1 when `capacity` is too small (width and height
 * are set and the image is kept: call again). Any thread. */
int petari_present_take_readback(const void* copy, unsigned char* rgba, unsigned capacity, unsigned* width, unsigned* height);

#ifdef __cplusplus
}
#endif

#endif
