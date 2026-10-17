// Present-interval summary and off-cadence math (petari/present_stats.hpp), and the
// per-frame record bookkeeping behind the PETARI_FRAME_CSV present columns.
#include <petari/present_stats.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace PS = PetariNative::PresentStats;

static int gFailures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++gFailures;                                                     \
        }                                                                    \
    } while (0)

static bool near(double a, double b) { return a - b < 1e-6 && b - a < 1e-6; }

int main() {
    // Cadence: multiples of the refresh period within +-2 ms.
    CHECK(PS::onCadence(16.67, 60.0, 2.0));
    CHECK(PS::onCadence(33.4, 60.0, 2.0));
    CHECK(!PS::onCadence(25.0, 60.0, 2.0));    // 1.5 periods
    CHECK(!PS::onCadence(20.0, 60.0, 2.0));    // 3.3 ms past one period
    CHECK(PS::onCadence(18.6, 60.0, 2.0));     // 1.9 ms past
    CHECK(PS::onCadence(8.33, 120.0, 2.0));
    CHECK(PS::onCadence(25.0, 120.0, 2.0));    // 3 ticks at 120 Hz
    CHECK(!PS::onCadence(12.5, 120.0, 2.0));   // 1.5 ticks
    CHECK(PS::onCadence(3.0, 60.0, 2.0) == false);  // below one period never rounds to zero
    CHECK(PS::onCadence(1.0, 60.0, 2.0) == false);
    unsigned k = 0;
    PS::onCadence(50.0, 60.0, 2.0, &k);
    CHECK(k == 3);

    // Percentiles are nearest-rank over the ascending sort.
    std::vector<double> v;
    for (int i = 1; i <= 100; ++i) v.push_back(i);
    PS::IntervalSummary s = PS::summarize(v, 60.0);
    CHECK(s.count == 100);
    CHECK(near(s.p50, 50) && near(s.p95, 95) && near(s.p99, 99) && near(s.max, 100));

    // Mixed: 90 steady 16.67, 6 doubles (33.3), 4 off-cadence (25.0).
    std::vector<double> mixed(90, 16.67);
    mixed.insert(mixed.end(), 6, 33.3);
    mixed.insert(mixed.end(), 4, 25.0);
    s = PS::summarize(mixed, 60.0);
    CHECK(s.count == 100 && s.offCadence == 4);
    CHECK(s.ticks[0] == 90 && s.ticks[1] == 10 && s.ticks[2] == 0 && s.ticks[3] == 0);  // 25.0 rounds to 2
    CHECK(near(s.max, 33.3));
    CHECK(PS::summarize({}, 60.0).count == 0);

    // Unknown refresh falls back to 60 Hz.
    CHECK(PS::summarize({16.67}, 0.0).offCadence == 0);

    // Records: frames 10..14, frame 12 dropped, frame 13 never reported; times in 16.7 ms steps
    // except frame 14 which lands 25 ms after frame 11.
    const std::uint64_t base = 1'000'000'000ull;
    for (std::uint32_t f = 10; f <= 14; ++f) PS::noteAcquired(f, base + f, 100 + f, f == 11 || f == 14);
    PS::notePresented(10, base / 1e9 + 0.0);
    PS::notePresented(11, (base + 16'700'000ull) / 1e9);
    PS::notePresented(12, 0.0);  // dropped
    PS::notePresented(14, (base + 16'700'000ull + 25'000'000ull) / 1e9);
    std::size_t reordered = 0;
    const std::vector<double> intervals = PS::presentIntervalsMs(&reordered);
    CHECK(intervals.size() == 2 && reordered == 0);
    CHECK(near(std::round(intervals[0] * 10), 167) && near(std::round(intervals[1] * 10), 250));

    PS::RowFields row = PS::rowFields(11, base + 17'000'000ull);
    CHECK(std::string(row.state) == "ok" && near(std::round(row.presentMs * 10), 167));
    CHECK(row.onScreenFrame == 11);
    CHECK(row.occluded == 1 && row.drawableId == 111);
    row = PS::rowFields(12, base + 30'000'000ull);
    CHECK(row.occluded == 0 && row.drawableId == 112);
    CHECK(std::string(row.state) == "dropped" && row.presentMs < 0 && row.onScreenFrame == 11);
    row = PS::rowFields(13, base + 40'000'000ull);
    CHECK(std::string(row.state) == "pending" && row.onScreenFrame == 11);
    row = PS::rowFields(14, base + 50'000'000ull);
    CHECK(std::string(row.state) == "ok" && near(std::round(row.presentMs * 10), 250));  // vs frame 11
    CHECK(row.onScreenFrame == 14);
    row = PS::rowFields(500, base);
    CHECK(std::string(row.state) == "none" && row.onScreenFrame == -1);

    // Frames 11 and 14 were presented while the window was occluded.
    CHECK(PS::occludedAcquired.load() == 2 && PS::presentedWhileOccluded.load() == 2);
    CHECK(PS::rowFields(500, base).occluded == -1 && PS::rowFields(500, base).drawableId == -1);

    // The summary names the drawable queue and the occlusion counts.
    PS::maxDrawables.store(2);
    PS::maxDrawablesFromEnv.store(true);
    std::FILE* tmp = std::tmpfile();
    PS::writeSummary(tmp);
    std::rewind(tmp);
    char line[1024] = {};
    std::fgets(line, sizeof line, tmp);
    std::fclose(tmp);
    CHECK(std::strstr(line, "maximumDrawableCount 2 (PETARI_MAX_DRAWABLES)") != nullptr);
    CHECK(std::strstr(line, "presented while occluded 2") != nullptr);
    CHECK(std::strstr(line, "present mode") != nullptr);

    // A reused ring slot ignores a stale handler.
    PS::noteAcquired(10 + PS::kRing, base + 99);
    PS::notePresented(10, 5.0);
    CHECK(PS::find(10) == nullptr);

    if (gFailures == 0) std::puts("present_stats tests passed");
    return gFailures == 0 ? 0 : 1;
}
