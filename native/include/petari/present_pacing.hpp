#pragma once
// VI retrace clock locked to the display (default; PETARI_VI_CLOCK=timer opts out).
//
// The game finishes a frame every VI field. With VI on its own 59.94 Hz timer the
// frame's ready time drifts against the display's refresh grid; every time it
// crossed a refresh (every ~8 s at 120 Hz, ~17 s at 60 Hz), a millisecond or two of
// jitter flipped frames between two refreshes for dozens of frames: bursts of 3- and
// 1-refresh intervals on screen (present p99 25 ms in every present mode).
//
// Locked, the VI field is the display's 60 Hz frame (one link period times
// ticksPerFrame: 16.667 ms instead of 16.683), so the field no longer drifts, and its
// phase is steered so that frames reach the present `arrivalTarget` after a refresh
// of the link grid. Measured (perf_bench foreground, EggStar s1 + HB s1, 120 Hz panel):
// 1/2/3-refresh intervals 45/1364/48 -> 0/1457/0, present p99 25 -> 16.67 ms, audio
// unchanged (AI DMA follows the audio device clock, not VI).
//
// The display link (native/app/display_link.cpp) publishes the refresh grid in
// mach_absolute_time seconds (CACurrentMediaTime). The render worker reports when
// each frame reaches Surface::Present (notePresent, patch_aurora_present.py). The VI
// retrace thread (native/platform/vi/vi_host.cpp) calls viServo once per field.
// Without a usable, stable grid VI keeps its nominal timer; every lock and unlock is
// logged with its reason.
//
// Header-only; the per-field decision (viStep) is a pure function (unit-tested).

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string_view>

#if defined(__APPLE__)
#include <mach/mach_time.h>
#endif

namespace PetariNative::PresentPacing {

// Default: display. PETARI_VI_CLOCK=timer keeps VI on its fixed 59.94 Hz timer (the previous default).
inline bool viLockEnabled() {
    static const bool on = [] {
        const char* value = std::getenv("PETARI_VI_CLOCK");
        if (value == nullptr || value[0] == '\0' || std::string_view(value) == "display") return true;
        if (std::string_view(value) == "timer") return false;
        std::fprintf(stderr, "PETARI VI: unknown PETARI_VI_CLOCK=%s (display or timer); using display\n", value);
        return true;
    }();
    return on;
}

// The display link runs only for the VI clock.
inline bool linkWanted() { return viLockEnabled(); }

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

// Wraps x to [-period/2, period/2).
inline double wrapPhase(double x, double period) {
    return x - period * std::floor(x / period + 0.5);
}

// Shared state: the link writes the grid, the render worker the arrival phase, VI reads both.
struct State {
    std::mutex mutex;
    Grid grid;
    double lastCallback = 0.0;   // media time of the latest link callback
    double arrivalOffset = 0.0;  // frames reach the present this long after a link refresh (smoothed)
    bool arrivalValid = false;
    // Statistics (VI thread writes).
    std::atomic<std::uint64_t> lockedFields{0};
    std::atomic<std::uint64_t> timerFields{0};
    std::atomic<std::uint64_t> locks{0};
    std::atomic<std::uint64_t> unlocks{0};
};
inline State state;

// Display link thread: a refresh happened at `refreshTime` (media time); the period is `period`.
inline void publishRefresh(double refreshTime, double period) {
    std::lock_guard lock{state.mutex};
    state.grid = Grid{refreshTime, period};
    state.lastCallback = mediaTime();
}

// Render worker, just before Surface::Present: a frame reached the present now.
inline void notePresent() {
    if (!linkWanted()) return;
    const double now = mediaTime();
    std::lock_guard lock{state.mutex};
    if (!(state.grid.period > 0.0) || now - state.lastCallback > 0.1) {
        state.arrivalValid = false;
        return;
    }
    const double offset = wrapPhase(now - state.grid.refreshTime, state.grid.period);
    // Smoothed on the circle: step toward the new sample by the wrapped difference.
    state.arrivalOffset = state.arrivalValid
        ? wrapPhase(state.arrivalOffset + 0.1 * wrapPhase(offset - state.arrivalOffset, state.grid.period), state.grid.period)
        : offset;
    state.arrivalValid = true;
}

// 2.5 ms after a refresh of the (60 Hz) link grid: consistency over latency. Measured (perf_bench
// foreground, 120 Hz, EggStar s1 + HB s1, ~78 probe presses and ~5,650 gameplay frames per run;
// build/fc/bench/fc-phase2 in the frame-cost worktree):
//   target 2:   present-to-screen 22.9 ms, 1/3-refresh flips 0+0
//   target 3:   22.0 ms, 1+0     target 4: 21.0 ms, 1+0     target 5.5: 19.5 ms, 5+4
// Later targets leave less margin before the compositor deadline, so the rare draw-done spikes
// (host scheduling latency of the renderer threads, ~0.1% of frames) flip frames on screen.
// (The 59.94 Hz timer had ~20 ms on average but a burst of 1/3 flips every ~8 s.)
inline double arrivalTarget() {
    static const double target = [] {
        const char* value = std::getenv("PETARI_VI_PHASE_TARGET_MS");
        return value != nullptr && value[0] != '\0' ? std::atof(value) / 1000.0 : 0.0025;
    }();
    return target;
}

// ---- per-field decision (pure) ----

constexpr double kStaleSeconds = 0.1;         // no link callback this long: window hidden, minimised or occluded
constexpr unsigned kStableFields = 30;        // the refresh period must hold this many fields before locking
constexpr double kStableTolerance = 0.002;    // relative: a period change beyond this is a new display mode
constexpr double kMaxCorrection = 0.0002;     // seconds per field of phase steering (0.2 ms)

enum class ViEvent { None, Locked, Unlocked };

struct ViFieldInput {
    Grid grid;
    double sinceCallback = 1e9;  // seconds since the latest link callback
    bool arrivalValid = false;
    double arrivalOffset = 0.0;
};

struct ViClockState {  // VI thread only
    bool locked = false;
    double candidate = 0.0;  // the refresh period being watched for stability
    unsigned stable = 0;     // consecutive fields with that period
};

struct ViFieldResult {
    bool locked = false;
    double period = 0.0;      // seconds this field
    double correction = 0.0;  // seconds to add to the next deadline
    ViEvent event = ViEvent::None;
    const char* reason = "";  // why VI is (or went) on its own timer
};

inline ViFieldResult viStep(ViClockState& s, const ViFieldInput& in, double nominalPeriod) {
    ViFieldResult r;
    r.period = nominalPeriod;
    const unsigned ticks = ticksPerFrame(in.grid.period);
    const double frame = ticks * in.grid.period;
    if (in.sinceCallback > kStaleSeconds) {
        r.reason = "no display refresh for 100 ms (no link, window hidden or minimised)";
    } else if (ticks == 0) {
        r.reason = "refresh rate is not a multiple of 60 Hz";
    } else if (std::fabs(frame - nominalPeriod) > 0.01 * nominalPeriod) {
        r.reason = "the game's field rate is not ~60 Hz";
    }
    if (r.reason[0] != '\0') {
        s.stable = 0;
        s.candidate = 0.0;
        if (s.locked) {
            s.locked = false;
            r.event = ViEvent::Unlocked;
        }
        return r;
    }
    // A changed refresh period (another display, a mode change, a variable rate): back to the timer
    // until the new period has held for kStableFields fields.
    if (std::fabs(in.grid.period - s.candidate) <= kStableTolerance * in.grid.period) {
        ++s.stable;
    } else {
        s.candidate = in.grid.period;
        s.stable = 1;
        if (s.locked) {
            s.locked = false;
            r.event = ViEvent::Unlocked;
            r.reason = "refresh rate changed";
            return r;
        }
    }
    if (!s.locked) {
        if (s.stable < kStableFields) {
            r.reason = "waiting for a steady refresh rate";
            return r;
        }
        s.locked = true;
        r.event = ViEvent::Locked;
    }
    r.locked = true;
    r.period = frame;
    if (in.arrivalValid) {
        // Arriving later than the target: retrace earlier (a negative correction), and the reverse.
        const double step = 0.05 * wrapPhase(in.arrivalOffset - arrivalTarget(), in.grid.period);
        r.correction = -(step > kMaxCorrection ? kMaxCorrection : step < -kMaxCorrection ? -kMaxCorrection : step);
    }
    return r;
}

// VI thread, once per field: reads the shared state and steps `s`.
inline ViFieldResult viServo(ViClockState& s, double nominalPeriod) {
    ViFieldInput in;
    {
        std::lock_guard lock{state.mutex};
        in.grid = state.grid;
        in.sinceCallback = state.lastCallback > 0.0 ? mediaTime() - state.lastCallback : 1e9;
        in.arrivalValid = state.arrivalValid;
        in.arrivalOffset = state.arrivalOffset;
    }
    const ViFieldResult r = viStep(s, in, nominalPeriod);
    (r.locked ? state.lockedFields : state.timerFields).fetch_add(1, std::memory_order_relaxed);
    if (r.event == ViEvent::Locked) state.locks.fetch_add(1, std::memory_order_relaxed);
    if (r.event == ViEvent::Unlocked) state.unlocks.fetch_add(1, std::memory_order_relaxed);
    return r;
}

// At exit (the frame seam's report).
inline void writeSummary(std::FILE* out) {
    if (!viLockEnabled()) return;
    std::fprintf(out, "[vi clock] display: %llu fields locked to the display, %llu on the timer, %llu locks, %llu unlocks\n",
                 static_cast<unsigned long long>(state.lockedFields.load()),
                 static_cast<unsigned long long>(state.timerFields.load()),
                 static_cast<unsigned long long>(state.locks.load()), static_cast<unsigned long long>(state.unlocks.load()));
}

}  // namespace PetariNative::PresentPacing
