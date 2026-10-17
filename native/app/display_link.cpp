// The display's refresh grid for the VI clock lock (petari/present_pacing.hpp,
// PETARI_VI_CLOCK=display): a CADisplayLink from the game view
// (-[NSView displayLinkWithTarget:selector:], macOS 14+), which follows the
// view to whatever display the window is on, run on its own thread's run loop
// (the main thread runs the game and only pumps events between frames).
#include "display_link.hpp"

#include <petari/present_pacing.hpp>

#include <CoreFoundation/CoreFoundation.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>
#include <objc/message.h>
#include <objc/runtime.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace PetariNative::App::DisplayLink {
namespace {

namespace Pacing = PetariNative::PresentPacing;

struct FrameRateRange {  // CAFrameRateRange
    float minimum, maximum, preferred;
};

id gLink = nullptr;
std::atomic<unsigned> gLoggedTicks{~0u};

double sendDouble(id object, const char* selector) {
    return reinterpret_cast<double (*)(id, SEL)>(objc_msgSend)(object, sel_registerName(selector));
}

void tick(id, SEL, id link) {
    const double timestamp = sendDouble(link, "timestamp");
    const double target = sendDouble(link, "targetTimestamp");
    const double period = target - timestamp;
    if (!(period > 0.0)) return;
    Pacing::publishRefresh(timestamp, period);
    // Once per change of the refresh rate (startup, moving to another display), at most 10 lines
    // (a variable-rate display could change every refresh).
    const unsigned ticks = Pacing::ticksPerFrame(period);
    if (gLoggedTicks.exchange(ticks) != ticks) {
        static std::atomic<unsigned> lines{0};
        const unsigned line = lines.fetch_add(1);
        if (line < 10) {
            std::fprintf(stderr, "[vi clock] display link %.2f Hz: %s\n", 1.0 / period,
                         ticks != 0 ? "a multiple of 60 Hz" : "not a multiple of 60 Hz (VI stays on its timer)");
        } else if (line == 10) {
            std::fprintf(stderr, "[vi clock] display link rate keeps changing; no more rate lines\n");
        }
    }
}

}  // namespace

void start(void* window) {
    if (!Pacing::linkWanted() || gLink != nullptr) return;
    auto* sdlWindow = static_cast<SDL_Window*>(window);
    id view = static_cast<id>(SDL_GetPointerProperty(SDL_GetWindowProperties(sdlWindow), "aurora.metal_view", nullptr));
    const SEL create = sel_registerName("displayLinkWithTarget:selector:");
    if (view == nullptr || !class_respondsToSelector(object_getClass(view), create)) {
        std::fprintf(stderr, "[vi clock] no NSView display link (needs macOS 14 and a Metal view): VI stays on its timer\n");
        return;
    }
    Class targetClass = objc_getClass("PetariDisplayLinkTarget");
    if (targetClass == nullptr) {
        targetClass = objc_allocateClassPair(objc_getClass("NSObject"), "PetariDisplayLinkTarget", 0);
        class_addMethod(targetClass, sel_registerName("tick:"), reinterpret_cast<IMP>(tick), "v@:@");
        objc_registerClassPair(targetClass);
    }
    id target = reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(reinterpret_cast<id>(targetClass), sel_registerName("new"));
    // Main thread (AppKit); the link keeps its target.
    gLink = reinterpret_cast<id (*)(id, SEL, id, SEL)>(objc_msgSend)(view, create, target, sel_registerName("tick:"));
    if (gLink == nullptr) {
        std::fprintf(stderr, "[vi clock] display link creation failed: VI stays on its timer\n");
        return;
    }
    reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(gLink, sel_registerName("retain"));
    // One callback per 60 Hz frame where the display allows it (every 2nd refresh at 120 Hz).
    const FrameRateRange range{60.0f, 60.0f, 60.0f};
    reinterpret_cast<void (*)(id, SEL, FrameRateRange)>(objc_msgSend)(gLink, sel_registerName("setPreferredFrameRateRange:"), range);
    std::thread([] {
        id runLoop = reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(reinterpret_cast<id>(objc_getClass("NSRunLoop")),
                                                                     sel_registerName("currentRunLoop"));
        reinterpret_cast<void (*)(id, SEL, id, id)>(objc_msgSend)(gLink, sel_registerName("addToRunLoop:forMode:"), runLoop,
                                                                  (id)kCFRunLoopCommonModes);
        CFRunLoopRun();
    }).detach();
    std::fprintf(stderr, "[vi clock] display link started: VI follows the display when its refresh is steady\n");
}

}  // namespace PetariNative::App::DisplayLink
