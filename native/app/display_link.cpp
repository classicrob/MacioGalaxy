// The display's refresh grid for present pacing (petari/present_pacing.hpp,
// PETARI_PRESENT_PACING=displaylink): a CADisplayLink from the game view
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
    // Once per change of the refresh rate (startup, moving to another display).
    const unsigned ticks = Pacing::ticksPerFrame(period);
    if (gLoggedTicks.exchange(ticks) != ticks) {
        if (ticks != 0) {
            std::fprintf(stderr, "[present pacing] display link %.2f Hz: present every %u refresh(es)\n", 1.0 / period, ticks);
        } else {
            std::fprintf(stderr, "[present pacing] display link %.2f Hz is not a multiple of 60 Hz: presenting unpaced\n",
                         1.0 / period);
        }
    }
}

void report() {
    const auto& s = Pacing::state;
    const unsigned long long waits = s.waits.load();
    std::fprintf(stderr,
                 "[present pacing] releases %llu, waited %llu (mean %.2f ms), slips %llu, unpaced (no link/hidden/rate) %llu\n",
                 static_cast<unsigned long long>(s.releases.load()), waits,
                 waits != 0 ? s.waitedUs.load() / 1000.0 / waits : 0.0, static_cast<unsigned long long>(s.slips.load()),
                 static_cast<unsigned long long>(s.bypassed.load()));
}

}  // namespace

void start(void* window) {
    if (!Pacing::linkWanted() || gLink != nullptr) return;
    auto* sdlWindow = static_cast<SDL_Window*>(window);
    id view = static_cast<id>(SDL_GetPointerProperty(SDL_GetWindowProperties(sdlWindow), "aurora.metal_view", nullptr));
    const SEL create = sel_registerName("displayLinkWithTarget:selector:");
    if (view == nullptr || !class_respondsToSelector(object_getClass(view), create)) {
        std::fprintf(stderr, "[present pacing] no NSView display link (needs macOS 14 and a Metal view): presenting unpaced\n");
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
        std::fprintf(stderr, "[present pacing] display link creation failed: presenting unpaced\n");
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
    std::atexit(report);
    std::fprintf(stderr, "[present pacing] display link started (present gate %s, VI clock %s)\n", Pacing::enabled() ? "on" : "off",
                 Pacing::viLockEnabled() ? "display" : "own timer");
}

}  // namespace PetariNative::App::DisplayLink
