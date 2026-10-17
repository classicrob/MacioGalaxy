#pragma once
// Display-level presentation statistics (PETARI_FRAME_CSV / PETARI_FRAME_STATS).
//
// The frame seam's interval is game-thread time: it is locked to the VI retrace
// wait and says nothing about when a frame reached the screen. This records,
// per Aurora frame, when its CAMetalLayer drawable was actually presented
// (MTLDrawable presentedTime, via addPresentedHandler; the Objective-C side is
// native/app/latency_report.cpp), and summarizes the intervals between
// consecutive on-screen presents.
//
// Header-only (C++17 inline variables) so Aurora's patched sources, the app and
// the tests share one copy. Recording is lock-free and allocation-free; all
// statistics are computed at exit.
//
// Frame numbers are the seam's Aurora frame indices (LatencyProbe::frame at
// compose time), which equal the FrameStats::Frame::index of the interval that
// ends at the seam that submitted the frame.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>

namespace PetariNative::PresentStats {

// Recording is on when frame statistics are (the same switches as the seam's).
inline bool enabled() {
    static const bool on = [] {
        for (const char* name : {"PETARI_FRAME_CSV", "PETARI_FRAME_STATS", "PETARI_PRESENT_STATS", "PETARI_TRACE_BOOT"}) {
            const char* value = std::getenv(name);
            if (value != nullptr && value[0] != '\0' && value[0] != '0') return true;
        }
        return false;
    }();
    return on;
}

// ---- pure math (unit-tested) ----

struct IntervalSummary {
    std::size_t count = 0;
    double p50 = 0, p95 = 0, p99 = 0, max = 0;
    std::size_t offCadence = 0;  // not within toleranceMs of a multiple of the refresh period
    std::size_t ticks[4] = {};   // intervals of 1, 2, 3 and 4+ refresh periods (rounded)
};

// Nearest-rank percentile of an ascending-sorted vector.
inline double percentileSorted(const std::vector<double>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    const double rank = std::ceil(p / 100.0 * static_cast<double>(sorted.size()));
    const std::size_t index = rank < 1.0 ? 0 : std::min(sorted.size() - 1, static_cast<std::size_t>(rank) - 1);
    return sorted[index];
}

// The nearest whole number of refresh periods (at least one) and whether the
// interval is within toleranceMs of it.
inline bool onCadence(double intervalMs, double refreshHz, double toleranceMs, unsigned* multiple = nullptr) {
    const double period = refreshHz > 0.0 ? 1000.0 / refreshHz : 1000.0 / 60.0;
    // At least one period; capped so the unsigned conversion below stays defined for huge or non-finite intervals.
    const double k = std::min(1.0e6, std::max(1.0, std::round(intervalMs / period)));
    if (multiple != nullptr) *multiple = static_cast<unsigned>(k);
    return std::fabs(intervalMs - k * period) <= toleranceMs;
}

inline IntervalSummary summarize(std::vector<double> intervalsMs, double refreshHz, double toleranceMs = 2.0) {
    IntervalSummary s;
    s.count = intervalsMs.size();
    if (intervalsMs.empty()) return s;
    for (double interval : intervalsMs) {
        unsigned k = 1;
        if (!onCadence(interval, refreshHz, toleranceMs, &k)) ++s.offCadence;
        ++s.ticks[std::clamp(k, 1u, 4u) - 1];  // audit-ok: clamped to 0..3, ticks has 4 entries
    }
    std::sort(intervalsMs.begin(), intervalsMs.end());
    s.p50 = percentileSorted(intervalsMs, 50);
    s.p95 = percentileSorted(intervalsMs, 95);
    s.p99 = percentileSorted(intervalsMs, 99);
    s.max = intervalsMs.back();
    return s;
}

// ---- shared state ----

constexpr std::uint32_t kNoFrame = 0xffffffffu;
constexpr std::uint32_t kRing = 1u << 17;  // frames kept (36 minutes at 60 Hz)

enum State : std::uint8_t { Empty = 0, Acquired = 1, Presented = 2, Dropped = 3 };

struct Record {
    std::atomic<std::uint32_t> frame{0};  // frame + 1; 0 = unused
    std::atomic<std::uint8_t> state{Empty};
    std::atomic<std::uint64_t> acquiredNs{0};   // host uptime clock
    std::atomic<std::uint64_t> presentedNs{0};  // host uptime clock (CACurrentMediaTime base)
    std::atomic<std::uint64_t> drawableId{0};   // MTLDrawable.drawableID (per CAMetalLayer)
    std::atomic<std::uint8_t> occluded{0};      // NSWindow occlusionState lacked Visible when acquired
};
inline Record records[kRing];
inline std::atomic<std::uint32_t> highestFrame{0};
inline std::atomic<std::uint64_t> acquiredTotal{0}, presentedTotal{0}, droppedTotal{0};

// The Aurora frame the current thread is acquiring a drawable for (set around
// Surface::GetCurrentTexture by the patched aurora_end_frame).
inline thread_local std::uint32_t acquiringFrame = kNoFrame;

inline std::atomic<unsigned> maxDrawables{0};  // CAMetalLayer.maximumDrawableCount, 0 unknown
inline std::atomic<bool> maxDrawablesFromEnv{false};  // PETARI_MAX_DRAWABLES applied
inline std::atomic<std::uint64_t> occludedAcquired{0}, presentedWhileOccluded{0};

inline std::atomic<double> refreshHz{0.0};  // display refresh rate (SDL), 0 unknown
inline char modeName[24] = "unknown";        // wgpu::PresentMode, set where Aurora picks it
inline void setMode(std::string_view name) {
    const std::size_t n = std::min(name.size(), sizeof(modeName) - 1);
    std::memcpy(modeName, name.data(), n);
    modeName[n] = '\0';
}

// Render worker: a drawable was acquired for `frame`.
inline void noteAcquired(std::uint32_t frame, std::uint64_t nowNs, std::uint64_t drawableId = 0, bool occluded = false) {
    Record& r = records[frame % kRing];
    r.state.store(Acquired, std::memory_order_relaxed);
    r.acquiredNs.store(nowNs, std::memory_order_relaxed);
    r.presentedNs.store(0, std::memory_order_relaxed);
    r.drawableId.store(drawableId, std::memory_order_relaxed);
    r.occluded.store(occluded ? 1 : 0, std::memory_order_relaxed);
    if (occluded) occludedAcquired.fetch_add(1, std::memory_order_relaxed);
    r.frame.store(frame + 1, std::memory_order_release);
    std::uint32_t high = highestFrame.load(std::memory_order_relaxed);
    while (frame > high && !highestFrame.compare_exchange_weak(high, frame, std::memory_order_relaxed)) {
    }
    acquiredTotal.fetch_add(1, std::memory_order_relaxed);
}

// Any thread: the presented handler ran. presentedSeconds is 0 when the
// drawable was dropped (never reached the screen).
inline void notePresented(std::uint32_t frame, double presentedSeconds) {
    Record& r = records[frame % kRing];
    if (r.frame.load(std::memory_order_acquire) != frame + 1) return;  // the slot was reused
    if (presentedSeconds > 0.0) {
        r.presentedNs.store(static_cast<std::uint64_t>(presentedSeconds * 1e9), std::memory_order_relaxed);
        r.state.store(Presented, std::memory_order_release);
        if (r.occluded.load(std::memory_order_relaxed) != 0) presentedWhileOccluded.fetch_add(1, std::memory_order_relaxed);
        presentedTotal.fetch_add(1, std::memory_order_relaxed);
    } else {
        r.state.store(Dropped, std::memory_order_release);
        droppedTotal.fetch_add(1, std::memory_order_relaxed);
    }
}

// ---- exit-time queries ----

inline const Record* find(std::uint32_t frame) {
    const Record& r = records[frame % kRing];
    return r.frame.load(std::memory_order_acquire) == frame + 1 ? &r : nullptr;
}

// Frame numbers with a presented record, ascending, and their on-screen times.
inline void presentedSequence(std::vector<std::uint32_t>* frames, std::vector<std::uint64_t>* times) {
    const std::uint32_t high = highestFrame.load(std::memory_order_relaxed);
    const std::uint32_t low = high >= kRing ? high - kRing + 1 : 0;
    for (std::uint32_t f = low;; ++f) {
        const Record* r = find(f);
        if (r != nullptr && r->state.load(std::memory_order_acquire) == Presented) {
            frames->push_back(f);
            times->push_back(r->presentedNs.load(std::memory_order_relaxed));
        }
        if (f == high) break;
    }
}

// Intervals (ms) between consecutive on-screen presents, in frame order.
// Non-positive intervals (presented out of frame order) are counted in *reordered.
inline std::vector<double> presentIntervalsMs(std::size_t* reordered = nullptr) {
    std::vector<std::uint32_t> frames;
    std::vector<std::uint64_t> times;
    presentedSequence(&frames, &times);
    std::vector<double> out;
    for (std::size_t i = 1; i < times.size(); ++i) {
        if (times[i] > times[i - 1]) out.push_back(static_cast<double>(times[i] - times[i - 1]) / 1e6);
        else if (reordered != nullptr) ++*reordered;
    }
    return out;
}

struct RowFields {
    double presentMs = -1;         // this frame's drawable vs the previous presented one; -1: n/a
    long onScreenFrame = -1;       // the frame whose drawable was on screen at entryNs; -1: none known
    const char* state = "none";    // ok | dropped | none (no drawable seen) | pending
    int occluded = -1;             // 1 window not visible when the drawable was acquired, 0 visible, -1 n/a
    long long drawableId = -1;     // MTLDrawable.drawableID, -1 n/a
};

// Per CSV row: `frame` is the FrameStats frame index, entryNs the seam entry time.
inline RowFields rowFields(std::uint64_t frame, std::uint64_t entryNs) {
    RowFields out;
    if (frame >= kNoFrame) return out;
    const std::uint32_t f = static_cast<std::uint32_t>(frame);
    const Record* r = find(f);
    if (r != nullptr) {
        out.occluded = r->occluded.load(std::memory_order_relaxed);
        out.drawableId = static_cast<long long>(r->drawableId.load(std::memory_order_relaxed));
        switch (r->state.load(std::memory_order_acquire)) {
        case Presented: out.state = "ok"; break;
        case Dropped: out.state = "dropped"; break;
        case Acquired: out.state = "pending"; break;
        default: break;
        }
        if (r->state.load(std::memory_order_acquire) == Presented) {
            const std::uint64_t mine = r->presentedNs.load(std::memory_order_relaxed);
            for (std::uint32_t back = 1; back <= 16 && back <= f; ++back) {
                const Record* p = find(f - back);
                if (p != nullptr && p->state.load(std::memory_order_acquire) == Presented) {
                    const std::uint64_t prev = p->presentedNs.load(std::memory_order_relaxed);
                    if (mine > prev) out.presentMs = static_cast<double>(mine - prev) / 1e6;
                    break;
                }
            }
        }
    }
    // The latest drawable on screen at the seam entry, among this and earlier frames.
    std::uint64_t best = 0;
    for (std::uint32_t back = 0; back <= 16 && back <= f; ++back) {
        const Record* p = find(f - back);
        if (p == nullptr || p->state.load(std::memory_order_acquire) != Presented) continue;
        const std::uint64_t t = p->presentedNs.load(std::memory_order_relaxed);
        if (t <= entryNs && t > best) {
            best = t;
            out.onScreenFrame = static_cast<long>(f - back);
        }
    }
    return out;
}

inline void writeSummary(std::FILE* out) {
    if (acquiredTotal.load() == 0) {
        if (enabled()) {
            std::fprintf(out, "Petari present intervals: no drawables observed (present mode %s)\n", modeName);
        }
        return;
    }
    std::size_t reordered = 0;
    const double hz = refreshHz.load();
    const IntervalSummary s = summarize(presentIntervalsMs(&reordered), hz);
    const std::uint64_t acquired = acquiredTotal.load(), presented = presentedTotal.load(), dropped = droppedTotal.load();
    // Up to the layer's drawable count can still be queued when the process exits.
    const std::uint64_t unpresented = acquired > presented + dropped ? acquired - presented - dropped : 0;
    std::fprintf(out,
                 "Petari present intervals (on-screen, %zu): p50 %.2f p95 %.2f p99 %.2f max %.2f ms; "
                 "off-cadence (not within 2 ms of a multiple of the refresh period) %zu (%.1f%%); "
                 "intervals of 1/2/3/4+ refreshes %zu/%zu/%zu/%zu; display %.2f Hz; present mode %s; "
                 "drawables %llu, presented %llu, dropped %llu, never presented %llu, out of order %zu; "
                 "maximumDrawableCount %u%s; occluded at acquire %llu, presented while occluded %llu\n",
                 s.count, s.p50, s.p95, s.p99, s.max, s.offCadence,
                 s.count != 0 ? 100.0 * static_cast<double>(s.offCadence) / static_cast<double>(s.count) : 0.0, s.ticks[0],
                 s.ticks[1], s.ticks[2], s.ticks[3], hz, modeName, static_cast<unsigned long long>(acquired),
                 static_cast<unsigned long long>(presented), static_cast<unsigned long long>(dropped),
                 static_cast<unsigned long long>(unpresented), reordered, maxDrawables.load(),
                 maxDrawablesFromEnv.load() ? " (PETARI_MAX_DRAWABLES)" : " (default)",
                 static_cast<unsigned long long>(occludedAcquired.load()),
                 static_cast<unsigned long long>(presentedWhileOccluded.load()));
}

}  // namespace PetariNative::PresentStats
