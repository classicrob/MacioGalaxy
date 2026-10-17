// petari/present_pacing.hpp: the VI clock's per-field decisions (PETARI_VI_CLOCK=display) on
// displays this machine does not have: other refresh rates, variable rates, stale and hidden
// windows, a display change mid-run, plus the closed-loop phase servo.
#include <petari/present_pacing.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace Pacing = PetariNative::PresentPacing;

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

constexpr double kNtsc = 1001.0 / 60000.0;  // 16.683 ms VI field
constexpr double kPal = 1.0 / 50.0;

struct Trace {
    unsigned locks = 0, unlocks = 0, lockedFields = 0;
    double longestField = 0.0, shortestField = 1.0;  // period + correction: how long the game waits
    const char* lastReason = "";
};

// Feeds `fields` fields with a grid of `period` (the link's refresh period, seconds) to the servo.
// `since(field)`: seconds since the latest link callback; `jitterPeriod`: a variable-rate display.
template <class Since, class PeriodAt>
Trace run(Pacing::ViClockState& s, int fields, PeriodAt periodAt, Since since, double nominal = kNtsc, bool arrivals = true) {
    Trace t;
    double now = 0.0;
    for (int field = 0; field < fields; ++field) {
        Pacing::ViFieldInput in;
        in.grid = {now - 0.003, periodAt(field)};
        in.sinceCallback = since(field);
        in.arrivalValid = arrivals;
        in.arrivalOffset = 0.004;  // 2 ms off the target: the servo steers at its limit
        const auto r = Pacing::viStep(s, in, nominal);
        t.locks += r.event == Pacing::ViEvent::Locked;
        t.unlocks += r.event == Pacing::ViEvent::Unlocked;
        t.lockedFields += r.locked;
        if (r.reason[0] != '\0') t.lastReason = r.reason;
        check(r.locked || (r.period == nominal && r.correction == 0.0), "unlocked fields use the nominal timer");
        const double wait = r.period + r.correction;
        t.longestField = std::max(t.longestField, wait);
        t.shortestField = std::min(t.shortestField, wait);
        now += wait;
    }
    return t;
}

auto fixed(double hz) {
    return [hz](int) { return 1.0 / hz; };
}
auto fresh() {
    return [](int) { return 0.004; };
}

// No field may take longer than a nominal field plus one steering step, or be much shorter:
// no pause of the game beyond one field, no burst.
void checkBounded(const Trace& t, double nominal, const char* what) {
    const double slack = 0.01 * nominal + Pacing::kMaxCorrection + 1e-9;
    if (t.longestField > nominal + slack || t.shortestField < nominal - slack) {
        std::fprintf(stderr, "  %s: fields %.4f..%.4f ms\n", what, t.shortestField * 1000, t.longestField * 1000);
        check(false, what);
    }
}

}  // namespace

int main() {
    // Refresh-rate classification.
    check(Pacing::ticksPerFrame(1.0 / 60.0) == 1, "60 Hz: every refresh");
    check(Pacing::ticksPerFrame(1.0 / 120.0) == 2, "120 Hz: every 2nd refresh");
    check(Pacing::ticksPerFrame(1.0 / 240.0) == 4, "240 Hz: every 4th refresh");
    check(Pacing::ticksPerFrame(1.0 / 59.94) == 1, "59.94 Hz counts as 60");
    check(Pacing::ticksPerFrame(1.0 / 119.88) == 2, "119.88 Hz counts as 120");
    for (double hz : {50.0, 75.0, 90.0, 100.0, 144.0, 165.0, 48.0, 30.0}) {
        check(Pacing::ticksPerFrame(1.0 / hz) == 0, "not a multiple of 60 Hz");
    }
    check(Pacing::ticksPerFrame(0.0) == 0 && Pacing::ticksPerFrame(-1.0) == 0, "no period");

    // Steady 120 Hz: locks once, after kStableFields fields, at the display's 60 Hz frame.
    {
        Pacing::ViClockState s;
        const Trace t = run(s, 600, fixed(120.0), fresh());
        check(t.locks == 1 && t.unlocks == 0, "120 Hz: one lock");
        check(t.lockedFields == 600 - Pacing::kStableFields + 1, "120 Hz: locks after the stability window");
        checkBounded(t, kNtsc, "120 Hz: bounded fields");
        Pacing::ViFieldInput in;
        in.grid = {0.0, 1.0 / 120.0};
        in.sinceCallback = 0.004;
        const auto r = Pacing::viStep(s, in, kNtsc);
        check(r.locked && std::fabs(r.period - 1.0 / 60.0) < 1e-12, "120 Hz: 16.667 ms field");
    }
    // 59.94 Hz panel: locks at its own 16.683 ms frame (VI's nominal rate, now phase-locked).
    {
        Pacing::ViClockState s;
        const Trace t = run(s, 200, fixed(59.94), fresh());
        check(t.locks == 1 && t.lockedFields > 0, "59.94 Hz: locks");
        Pacing::ViFieldInput in;
        in.grid = {0.0, 1.0 / 59.94};
        in.sinceCallback = 0.004;
        check(std::fabs(Pacing::viStep(s, in, kNtsc).period - 1.0 / 59.94) < 1e-12, "59.94 Hz: its own frame");
    }
    // Rates that are not a multiple of 60 Hz, and a 50 Hz game field: never lock, say why.
    for (double hz : {144.0, 165.0, 75.0, 100.0, 50.0}) {
        Pacing::ViClockState s;
        const Trace t = run(s, 300, fixed(hz), fresh());
        check(t.locks == 0 && t.lockedFields == 0, "non-60 multiple: never locks");
        check(std::string_view(t.lastReason) == "refresh rate is not a multiple of 60 Hz", "non-60 multiple: reason");
    }
    {
        Pacing::ViClockState s;
        const Trace t = run(s, 300, fixed(60.0), fresh(), kPal);
        check(t.locks == 0 && std::string_view(t.lastReason) == "the game's field rate is not ~60 Hz", "PAL field: never locks");
    }
    // Variable refresh rate: the reported period changes every refresh (48-120 Hz) -> stays on the timer.
    // Also a rate that alternates between two valid values (60 / 120 Hz, a ProMotion panel switching).
    {
        std::mt19937 random(11);
        std::uniform_real_distribution<double> hz(48.0, 120.0);
        std::vector<double> periods(3000);
        for (double& p : periods) p = 1.0 / hz(random);
        Pacing::ViClockState s;
        const Trace t = run(s, 3000, [&](int f) { return periods[f]; }, fresh());
        check(t.locks == 0, "VRR: never locks");
        checkBounded(t, kNtsc, "VRR: bounded fields");
        Pacing::ViClockState flip;
        const Trace u = run(flip, 3000, [](int f) { return (f / 7) % 2 ? 1.0 / 60.0 : 1.0 / 120.0; }, fresh());
        check(u.locks == 0, "60/120 Hz flapping: never locks");
    }
    // Stale grid (link callbacks stop: window hidden, minimised or fully occluded) mid-run, then back.
    {
        Pacing::ViClockState s;
        const auto since = [](int f) { return f >= 300 && f < 500 ? 0.004 + (f - 299) * kNtsc : 0.004; };
        const Trace t = run(s, 800, fixed(120.0), since);
        check(t.locks == 2 && t.unlocks == 1, "hidden window: unlocks once, relocks once after it returns");
        checkBounded(t, kNtsc, "hidden window: bounded fields");
        // The unlock comes within the stale limit (100 ms = 6 fields), not later.
        Pacing::ViClockState s2;
        run(s2, 100, fixed(120.0), fresh());
        int unlockedAt = -1;
        for (int f = 0; f < 20 && unlockedAt < 0; ++f) {
            Pacing::ViFieldInput in;
            in.grid = {0.0, 1.0 / 120.0};
            in.sinceCallback = (f + 1) * kNtsc;
            const auto r = Pacing::viStep(s2, in, kNtsc);
            if (r.event == Pacing::ViEvent::Unlocked) {
                unlockedAt = f;
                check(std::string_view(r.reason).find("hidden or minimised") != std::string_view::npos, "stale: reason");
            }
        }
        check(unlockedAt >= 0 && (unlockedAt + 1) * kNtsc <= Pacing::kStaleSeconds + kNtsc, "stale: unlocks within 100 ms");
        // Never a link at all (no callback yet): timer, no events.
        Pacing::ViClockState none;
        const Trace n = run(none, 100, fixed(120.0), [](int) { return 1e9; });
        check(n.locks == 0 && n.unlocks == 0, "no link: timer, no events");
    }
    // Display change mid-run: the window moves from a 120 Hz panel to a 59.94 Hz external display,
    // and to a 144 Hz one. Unlock (logged reason), timer while the new rate settles, relock if usable.
    {
        Pacing::ViClockState s;
        const Trace t = run(s, 900, [](int f) { return f < 400 ? 1.0 / 120.0 : 1.0 / 59.94; }, fresh());
        check(t.locks == 2 && t.unlocks == 1, "120 -> 59.94 Hz: unlock then relock");
        checkBounded(t, kNtsc, "120 -> 59.94 Hz: no field beyond one field plus a step");
        Pacing::ViClockState s2;
        run(s2, 100, fixed(120.0), fresh());
        Pacing::ViFieldInput in;
        in.grid = {0.0, 1.0 / 144.0};
        in.sinceCallback = 0.004;
        const auto r = Pacing::viStep(s2, in, kNtsc);
        check(r.event == Pacing::ViEvent::Unlocked && !r.locked && r.period == kNtsc, "120 -> 144 Hz: unlocked at once");
        check(std::string_view(r.reason) == "refresh rate is not a multiple of 60 Hz", "120 -> 144 Hz: reason");
        Pacing::ViClockState s3;
        run(s3, 100, fixed(120.0), fresh());
        in.grid = {0.0, 1.0 / 60.0};
        const auto r3 = Pacing::viStep(s3, in, kNtsc);
        check(r3.event == Pacing::ViEvent::Unlocked && std::string_view(r3.reason) == "refresh rate changed",
              "120 -> 60 Hz: unlocked, reason logged");
    }

    // Closed loop: fields from viStep, frames reach the present 5.5 ms (+-1 ms) after the retrace, the
    // arrival phase smoothed as the render worker does. Converges to the target, then every frame is
    // shown two 120 Hz refreshes after the previous one. Midway the grid's phase jumps (another
    // display); the servo re-steers within the step limit and settles again.
    {
        double refresh = 1.0 / 120.0, gridBase = 0.0;
        std::mt19937 random(5);
        std::uniform_real_distribution<double> jitter(-0.001, 0.001);
        Pacing::ViClockState s;
        double retrace = 0.0042, arrival = 0.0;
        bool valid = false;
        unsigned hist[5] = {};
        std::int64_t previous = INT64_MIN;
        double finalOffset = 0.0, longest = 0.0;
        for (int field = 0; field < 24000; ++field) {
            if (field == 12000) gridBase = 0.0037;  // the new display's refreshes are at another phase
            const double ready = retrace + 0.0055 + jitter(random);
            const double offset = Pacing::wrapPhase(ready - gridBase, refresh);
            arrival = valid ? Pacing::wrapPhase(arrival + 0.1 * Pacing::wrapPhase(offset - arrival, refresh), refresh) : offset;
            valid = true;
            // Shown at the first refresh after it is ready.
            const auto shown = static_cast<std::int64_t>(std::floor((ready - gridBase) / refresh)) + 1;
            const bool settled = (field > 3000 && field < 12000) || field > 15000;
            if (settled && previous != INT64_MIN) {
                const auto k = shown - previous;
                hist[k < 1 ? 0 : k > 4 ? 4 : k] += 1;
                finalOffset = arrival;
            }
            previous = shown;
            Pacing::ViFieldInput in;
            in.grid = {gridBase, refresh};
            in.sinceCallback = 0.004;
            in.arrivalValid = valid;
            in.arrivalOffset = arrival;
            const auto r = Pacing::viStep(s, in, kNtsc);
            longest = std::max(longest, r.period + r.correction);
            retrace += r.period + r.correction;
        }
        std::printf("VI servo closed loop (120 Hz, +-1 ms jitter, grid phase jump midway): arrival %.2f ms after a refresh "
                    "(target %.2f), settled intervals 1/2/3 refreshes %u/%u/%u, longest field %.3f ms\n",
                    finalOffset * 1000, Pacing::arrivalTarget() * 1000, hist[1], hist[2], hist[3], longest * 1000);
        check(std::fabs(Pacing::wrapPhase(finalOffset - Pacing::arrivalTarget(), refresh)) < 0.0005, "servo converges to the target phase");
        check(hist[1] == 0 && hist[3] == 0 && hist[2] > 17000, "settled: every frame two refreshes apart");
        check(longest <= kNtsc + Pacing::kMaxCorrection, "no field longer than a field plus one step");
    }
    if (failures == 0) std::puts("VI clock: rate classes, VRR, stale/hidden, display changes and the closed-loop servo pass");
    return failures == 0 ? 0 : 1;
}
