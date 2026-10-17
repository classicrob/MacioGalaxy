#pragma once
// Display-locked present pacing (PETARI_PRESENT_PACING=displaylink).
//
// The game finishes a frame every VI field (59.94 Hz), at a time that jitters by
// a millisecond or two with its work, and Aurora's render worker presented it as
// soon as it was encoded. The window shows a drawable at the first refresh
// after it is ready, so whenever the ready time drifted across a refresh (the
// 59.94 Hz VI beats against the display: every ~8 s at 120 Hz, ~17 s at 60 Hz)
// the jitter flipped frames between two refreshes for dozens of frames: bursts
// of 3- and 1-refresh intervals in the present statistics.
//
// The gate holds each Present until a refresh grid position: the first refresh
// at least `ticksPerFrame` refreshes after the previous release (one 60 Hz frame:
// 1 at 60 Hz, 2 at 120 Hz). Released just after refresh k, a frame is shown at
// refresh k + 1. A frame that arrives later than its slot is released at once
// and is still on cadence as long as it arrives before the following refresh,
// so arrival jitter of up to a whole refresh no longer changes the cadence; only
// the slow VI drift does, once per beat (one slip, counted).
//
// The display link (native/app/display_link.cpp) publishes the refresh grid: the
// latest refresh time and the refresh period, in mach_absolute_time seconds
// (CACurrentMediaTime). The render worker calls waitToPresent() before
// Surface::Present (patch_aurora_present.py). No grid (link not started, window
// hidden: the link stops), a stale grid, or a refresh rate that is not a
// multiple of 60 Hz: no wait, present as before.
//
// Header-only; the decision is a pure function (unit-tested).

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string_view>
#include <thread>
#include <chrono>

#if defined(__APPLE__)
#include <mach/mach_time.h>
#endif

namespace PetariNative::PresentPacing {

inline bool enabled() {
    static const bool on = [] {
        const char* value = std::getenv("PETARI_PRESENT_PACING");
        return value != nullptr && std::string_view(value) == "displaylink";
    }();
    return on;
}

// PETARI_VI_CLOCK=display: the VI retrace clock runs at the display's 60 Hz frame rate,
// phase-locked so frames reach the present a fixed time after a refresh (see viServo).
inline bool viLockEnabled() {
    static const bool on = [] {
        const char* value = std::getenv("PETARI_VI_CLOCK");
        return value != nullptr && std::string_view(value) == "display";
    }();
    return on;
}

// The display link runs for either.
inline bool linkWanted() { return enabled() || viLockEnabled(); }

// Seconds on the CACurrentMediaTime clock.
inline double mediaTime() {
#if defined(__APPLE__)
    static const double scale = [] {
        mach_timebase_info_data_t info{};
        mach_timebase_info(&info);
        return static_cast<double>(info.numer) / static_cast<double>(info.denom) / 1e9;
    }();
    return static_cast<double>(mach_absolute_time()) * scale;
#else
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}

// Refreshes per 60 Hz frame, or 0 if the refresh rate is not (within 3%) a whole multiple of 60 Hz.
inline unsigned ticksPerFrame(double refreshPeriod) {
    if (!(refreshPeriod > 0.0)) return 0;
    const double ratio = (1.0 / refreshPeriod) / 60.0;
    const double whole = std::round(ratio);
    if (whole < 1.0 || whole > 8.0 || std::fabs(ratio - whole) > 0.03 * whole) return 0;
    return static_cast<unsigned>(whole);
}

struct Grid {
    double refreshTime = 0.0;  // a refresh, media time
    double period = 0.0;       // refresh period, seconds
};

// The index of the latest refresh at or before `now` on the grid (refresh 0 = grid.refreshTime).
inline std::int64_t refreshIndex(const Grid& grid, double now) {
    return static_cast<std::int64_t>(std::floor((now - grid.refreshTime) / grid.period));
}

struct Decision {
    std::int64_t release = 0;  // refresh index after which to present
    bool wait = false;         // that refresh is still ahead
    bool slipped = false;      // later than the slot: a cadence slip
};

// `last`: the refresh index of the previous release, or INT64_MIN for none. `now`: current refresh index.
inline Decision decide(std::int64_t last, std::int64_t now, unsigned ticks) {
    Decision d;
    if (last == INT64_MIN || ticks == 0) {
        d.release = now;
        return d;
    }
    const std::int64_t slot = last + ticks;
    if (now < slot) {
        d.release = slot;
        d.wait = true;
    } else {
        d.release = now;
        d.slipped = now > slot;
    }
    return d;
}

// Shared state: the link writes the grid, the render worker reads it.
struct State {
    std::mutex mutex;
    Grid grid;
    double lastCallback = 0.0;    // media time of the latest link callback
    std::int64_t lastRelease = INT64_MIN;
    double lastReleaseGridTime = 0.0;  // refresh time the last release index is relative to
    std::atomic<std::uint64_t> releases{0};
    std::atomic<std::uint64_t> waits{0};
    std::atomic<std::uint64_t> slips{0};
    std::atomic<std::uint64_t> bypassed{0};  // no usable grid
    std::atomic<std::uint64_t> waitedUs{0};
    std::atomic<std::uint32_t> ticks{0};
    // Render worker: where frames reach the present, relative to the latest link refresh
    // (seconds, wrapped to +-half a link period), smoothed. Read by the VI servo.
    double arrivalOffset = 0.0;  // under mutex
    bool arrivalValid = false;   // under mutex
};
inline State state;

// Display link thread: a refresh happened at `refreshTime` (media time); the period is `period`.
inline void publishRefresh(double refreshTime, double period) {
    std::lock_guard lock{state.mutex};
    state.grid = Grid{refreshTime, period};
    state.lastCallback = mediaTime();
    state.ticks.store(ticksPerFrame(period), std::memory_order_relaxed);
}

// Wraps x to [-period/2, period/2).
inline double wrapPhase(double x, double period) {
    return x - period * std::floor(x / period + 0.5);
}

// Render worker: a frame reached the present at `now` (media time).
inline void noteArrival(double now) {
    std::lock_guard lock{state.mutex};
    if (!(state.grid.period > 0.0) || now - state.lastCallback > 0.1) {
        state.arrivalValid = false;
        return;
    }
    const double offset = wrapPhase(now - state.grid.refreshTime, state.grid.period);
    if (!state.arrivalValid) {
        state.arrivalOffset = offset;
        state.arrivalValid = true;
    } else {
        // Smoothed on the circle: step toward the new sample by the wrapped difference.
        state.arrivalOffset = wrapPhase(state.arrivalOffset + 0.1 * wrapPhase(offset - state.arrivalOffset, state.grid.period),
                                        state.grid.period);
    }
}

// Render worker, just before Surface::Present.
inline void waitToPresent() {
    if (!linkWanted()) return;
    noteArrival(mediaTime());
    if (!enabled()) return;
    Grid grid;
    std::int64_t last = INT64_MIN;
    double lastGrid = 0.0;
    double lastCallback = 0.0;
    {
        std::lock_guard lock{state.mutex};
        grid = state.grid;
        last = state.lastRelease;
        lastGrid = state.lastReleaseGridTime;
        lastCallback = state.lastCallback;
    }
    const unsigned ticks = ticksPerFrame(grid.period);
    const double now = mediaTime();
    // No refresh for 100 ms: the window is hidden or occluded and the link stopped.
    if (ticks == 0 || now - lastCallback > 0.1) {
        state.bypassed.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard lock{state.mutex};
        state.lastRelease = INT64_MIN;
        return;
    }
    // Re-express the previous release on the current grid (the link moves its base every refresh).
    if (last != INT64_MIN) {
        last += static_cast<std::int64_t>(std::llround((lastGrid - grid.refreshTime) / grid.period));
    }
    const Decision d = decide(last, refreshIndex(grid, now), ticks);
    if (d.wait) {
        // Just after the refresh: the frame is shown at the next one.
        const double until = grid.refreshTime + static_cast<double>(d.release) * grid.period + 0.0003;
        const double seconds = until - now;
        if (seconds > 0.0 && seconds < 0.05) {
            std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
            state.waitedUs.fetch_add(static_cast<std::uint64_t>(seconds * 1e6), std::memory_order_relaxed);
        }
        state.waits.fetch_add(1, std::memory_order_relaxed);
    }
    if (d.slipped) state.slips.fetch_add(1, std::memory_order_relaxed);
    state.releases.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard lock{state.mutex};
    state.lastRelease = d.release;
    state.lastReleaseGridTime = grid.refreshTime;
}

// At exit (the frame seam's report), when the link was wanted.
inline void writeSummary(std::FILE* out) {
    if (!linkWanted()) return;
    const unsigned long long waits = state.waits.load();
    std::fprintf(out,
                 "[present pacing] gate %s, VI clock %s: releases %llu, waited %llu (mean %.2f ms), slips %llu, "
                 "unpaced (no link/hidden/rate) %llu\n",
                 enabled() ? "on" : "off", viLockEnabled() ? "display" : "own timer",
                 static_cast<unsigned long long>(state.releases.load()), waits,
                 waits != 0 ? state.waitedUs.load() / 1000.0 / waits : 0.0, static_cast<unsigned long long>(state.slips.load()),
                 static_cast<unsigned long long>(state.bypassed.load()));
}

// ---- VI clock lock (PETARI_VI_CLOCK=display) ----
//
// The VI retrace thread keeps its own timer and deadline; each field it asks the servo
// for the period and a small correction of its next deadline. Locked, the period is the
// display's 60 Hz frame (one link period times ticksPerFrame) instead of 59.94 Hz, so
// the field no longer drifts against the refresh grid, and the phase is steered so that
// frames reach the present `arrivalTarget` (2 ms, PETARI_VI_PHASE_TARGET_MS) after a link
// refresh, early enough to be shown at the next refresh even without the gate: a frame released just
// after a refresh is shown at the next one, and any lateness up to nearly a whole refresh
// keeps that cadence. Without a usable grid (no link, window hidden, a refresh rate that
// is not a multiple of 60 Hz, a nominal field rate that is not ~60 Hz) it returns false
// and VI uses its nominal period.

struct ServoInput {
    Grid grid;
    unsigned ticks = 0;
    bool fresh = false;
    bool arrivalValid = false;
    double arrivalOffset = 0.0;
};
struct ServoOutput {
    double period = 0.0;      // seconds per field
    double correction = 0.0;  // seconds to add to the next deadline
};

inline double arrivalTarget() {
    static const double target = [] {
        const char* value = std::getenv("PETARI_VI_PHASE_TARGET_MS");
        return value != nullptr && value[0] != '\0' ? std::atof(value) / 1000.0 : 0.002;
    }();
    return target;
}

// Pure (unit-tested).
inline bool servo(const ServoInput& in, double nominalPeriod, ServoOutput* out) {
    if (!in.fresh || in.ticks == 0) return false;
    const double frame = in.ticks * in.grid.period;
    if (std::fabs(frame - nominalPeriod) > 0.01 * nominalPeriod) return false;
    out->period = frame;
    out->correction = 0.0;
    if (in.arrivalValid) {
        // Arriving later than the target: retrace earlier (a negative correction), and the reverse.
        const double error = wrapPhase(in.arrivalOffset - arrivalTarget(), in.grid.period);
        const double step = 0.05 * error;
        const double limit = 0.0002;  // 0.2 ms per field: a full-period re-phase takes ~1-2 s
        out->correction = -(step > limit ? limit : step < -limit ? -limit : step);
    }
    return true;
}

// VI thread, once per field.
inline bool viServo(double nominalPeriod, ServoOutput* out) {
    if (!viLockEnabled()) return false;
    ServoInput in;
    {
        std::lock_guard lock{state.mutex};
        in.grid = state.grid;
        in.fresh = mediaTime() - state.lastCallback <= 0.1;
        in.arrivalValid = state.arrivalValid;
        in.arrivalOffset = state.arrivalOffset;
    }
    in.ticks = ticksPerFrame(in.grid.period);
    return servo(in, nominalPeriod, out);
}

}  // namespace PetariNative::PresentPacing
