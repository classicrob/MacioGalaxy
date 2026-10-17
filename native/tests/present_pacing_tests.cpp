// petari/present_pacing.hpp: the refresh-grid present gate's decisions.
#include <petari/present_pacing.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>

namespace Pacing = PetariNative::PresentPacing;

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// Simulate frames finishing every VI field with jitter; the gate releases after a refresh and the
// frame is shown at the next refresh. Returns the histogram of on-screen intervals in refreshes.
static void simulate(double refreshHz, bool gated, double jitterMs, unsigned out[5], std::uint64_t* slips) {
    const double refresh = 1000.0 / refreshHz, field = 1000.0 * 1001.0 / 60000.0;
    const unsigned ticks = Pacing::ticksPerFrame(refresh / 1000.0);
    std::mt19937 random(3);
    std::uniform_real_distribution<double> jitter(0.0, jitterMs);
    std::int64_t last = INT64_MIN, previousShown = INT64_MIN;
    *slips = 0;
    for (int frame = 0; frame < 6000; ++frame) {
        const double ready = 3.0 + frame * field + jitter(random);  // ms, display refresh 0 at t=0
        std::int64_t release = static_cast<std::int64_t>(std::floor(ready / refresh));
        if (gated) {
            const auto d = Pacing::decide(last, release, ticks);
            release = d.release;
            *slips += d.slipped;
            last = release;
        }
        const std::int64_t shown = release + 1;
        if (previousShown != INT64_MIN) {
            const auto k = shown - previousShown;
            out[k < 1 ? 0 : k > 4 ? 4 : k] += 1;
        }
        previousShown = shown;
    }
}

int main() {
    check(Pacing::ticksPerFrame(1.0 / 60.0) == 1, "60 Hz: every refresh");
    check(Pacing::ticksPerFrame(1.0 / 120.0) == 2, "120 Hz: every 2nd refresh");
    check(Pacing::ticksPerFrame(1.0 / 59.94) == 1, "59.94 Hz counts as 60");
    check(Pacing::ticksPerFrame(1.0 / 144.0) == 0, "144 Hz: unpaced");
    check(Pacing::ticksPerFrame(1.0 / 100.0) == 0, "100 Hz: unpaced");
    check(Pacing::ticksPerFrame(0.0) == 0, "no period: unpaced");

    auto d = Pacing::decide(INT64_MIN, 10, 2);
    check(!d.wait && d.release == 10 && !d.slipped, "first frame: release now");
    d = Pacing::decide(10, 11, 2);
    check(d.wait && d.release == 12, "early: wait for the slot");
    d = Pacing::decide(10, 12, 2);
    check(!d.wait && d.release == 12 && !d.slipped, "late within the slot's refresh: on cadence");
    d = Pacing::decide(10, 13, 2);
    check(!d.wait && d.release == 13 && d.slipped, "a whole refresh late: slip");

    Pacing::Grid grid{100.0, 0.01};
    check(Pacing::refreshIndex(grid, 100.0) == 0 && Pacing::refreshIndex(grid, 100.0099) == 0 &&
              Pacing::refreshIndex(grid, 100.0101) == 1 && Pacing::refreshIndex(grid, 99.995) == -1,
          "refresh index");

    // Ungated, the 59.94 Hz beat and 2 ms of jitter give bursts of 1- and 3-refresh intervals at 120 Hz.
    for (double hz : {120.0, 60.0}) {
        unsigned plain[5] = {}, gated[5] = {};
        std::uint64_t slips = 0, unused = 0;
        simulate(hz, false, 2.0, plain, &unused);
        simulate(hz, true, 2.0, gated, &slips);
        const unsigned want = hz == 120.0 ? 2 : 1;
        const unsigned off = [&] { unsigned n = 0; for (unsigned k = 0; k < 5; ++k) if (k != want) n += gated[k]; return n; }();
        std::printf("%.0f Hz, 2 ms jitter, 6000 frames: ungated 1/2/3-refresh %u/%u/%u, gated %u/%u/%u (slips %llu)\n", hz,
                    plain[1], plain[2], plain[3], gated[1], gated[2], gated[3], static_cast<unsigned long long>(slips));
        check(plain[want - 1 + 0] + plain[want + 1] > 50, "the ungated model shows the beat bursts");
        // 6000 frames = 100 s: one slip per beat (8.3 s at 120 Hz, 16.7 s at 60 Hz), never a burst.
        check(slips <= (hz == 120.0 ? 13u : 7u) && off <= slips, "gated: at most one late frame per beat");
        check(gated[want - 1] == 0, "gated: never a short interval");
    }
    // VI servo: refusals.
    {
        Pacing::ServoInput in;
        in.grid = {0.0, 1.0 / 120.0};
        in.ticks = 2;
        in.fresh = true;
        Pacing::ServoOutput out;
        const double ntsc = 1001.0 / 60000.0;
        check(Pacing::servo(in, ntsc, &out) && std::fabs(out.period - 1.0 / 60.0) < 1e-9 && out.correction == 0.0,
              "servo: 120 Hz grid gives a 60 Hz field, no phase step before any arrival");
        in.fresh = false;
        check(!Pacing::servo(in, ntsc, &out), "servo: stale grid -> own timer");
        in.fresh = true;
        in.ticks = 0;
        check(!Pacing::servo(in, ntsc, &out), "servo: non-60 multiple -> own timer");
        in.ticks = 2;
        check(!Pacing::servo(in, 1.0 / 50.0, &out), "servo: 50 Hz field -> own timer");
    }
    // VI servo, closed loop: fields from the servo, frames reach the present `work` ms after the
    // retrace (with jitter), the arrival offset is smoothed as the render worker does. It must
    // converge to the target phase and then never slip against the 120 Hz grid.
    {
        const double refresh = 1.0 / 120.0;
        std::mt19937 random(5);
        std::uniform_real_distribution<double> jitter(-0.001, 0.001);
        double retrace = 0.0042, arrival = 0.0;  // arbitrary starting phase
        bool valid = false;
        unsigned hist[5] = {};
        std::int64_t previous = INT64_MIN, last = INT64_MIN;
        std::uint64_t slips = 0;
        double finalOffset = 0.0;
        for (int field = 0; field < 20000; ++field) {
            const double ready = retrace + 0.0055 + jitter(random);
            const double offset = Pacing::wrapPhase(ready, refresh);
            arrival = valid ? Pacing::wrapPhase(arrival + 0.1 * Pacing::wrapPhase(offset - arrival, refresh), refresh) : offset;
            valid = true;
            const auto d = Pacing::decide(last, static_cast<std::int64_t>(std::floor(ready / refresh)), 2);
            last = d.release;
            if (field > 2000) {
                slips += d.slipped;
                if (previous != INT64_MIN) {
                    const auto k = d.release + 1 - previous;
                    hist[k < 1 ? 0 : k > 4 ? 4 : k] += 1;
                }
                finalOffset = arrival;
            }
            previous = d.release + 1;
            Pacing::ServoInput in;
            in.grid = {0.0, refresh};
            in.ticks = 2;
            in.fresh = true;
            in.arrivalValid = valid;
            in.arrivalOffset = arrival;
            Pacing::ServoOutput out;
            check(Pacing::servo(in, 1001.0 / 60000.0, &out), "servo locks");
            retrace += out.period + out.correction;
        }
        std::printf("VI servo closed loop (120 Hz, +-1 ms jitter): arrival %.2f ms after a refresh (target %.2f), "
                    "intervals 1/2/3 refreshes %u/%u/%u, slips %llu\n", finalOffset * 1000, Pacing::arrivalTarget() * 1000,
                    hist[1], hist[2], hist[3], static_cast<unsigned long long>(slips));
        check(std::fabs(finalOffset - Pacing::arrivalTarget()) < 0.0005, "servo converges to the target phase");
        check(slips == 0 && hist[1] == 0 && hist[3] == 0, "locked: every frame two refreshes apart");
    }
    if (failures == 0) std::puts("Present pacing: decisions, rate gating, the beat model and the VI servo pass");
    return failures == 0 ? 0 : 1;
}
