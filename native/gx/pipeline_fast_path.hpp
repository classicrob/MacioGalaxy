#pragma once
// Warm-path memo for Aurora's GX find_pipeline (patch_aurora_pipeline.py).
//
// Every GX pipeline change on the FIFO processor looks its pipeline up. The full
// lookup hashes the whole PipelineConfig five times and takes the pipeline mutex
// four times (stage attribution, remembered configs, variant samples, the
// pipeline map), although for a pipeline that is already compiled and has been
// noted for the current stage all of that bookkeeping is idempotent and the
// result is just the runtime key. This memo remembers such keys per thread.
//
// A key is stored only after a full lookup found its pipeline ready, tagged with
// the generation read *before* that lookup started. Anything that could make a
// full lookup do more than return the key (stage begin or reset, pipeline-cache
// shutdown) bumps the generation before and after it runs, so a lookup racing it
// stores a stale tag and later misses. A miss always takes the full lookup.
//
// PETARI_PIPELINE_FAST_CHECK=1: hits also run the full lookup and compare (for
// verification runs; counts mismatches, reported at exit).
//
// Standard headers only (unit-tested in native/tests/pipeline_fast_path_tests.py).

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace PetariFastPipeline {

inline std::atomic<std::uint64_t> generation{1};  // 0 marks an empty entry
inline std::atomic<std::uint64_t> hits{0};
inline std::atomic<std::uint64_t> misses{0};
inline std::atomic<std::uint64_t> checkMismatches{0};

inline std::uint64_t current() { return generation.load(std::memory_order_acquire); }
inline void invalidate() { generation.fetch_add(1, std::memory_order_acq_rel); }

// Bumps the generation on entry and on every exit of a scope that changes state
// a full lookup reads.
struct InvalidateScope {
    InvalidateScope() { invalidate(); }
    ~InvalidateScope() { invalidate(); }
    InvalidateScope(const InvalidateScope&) = delete;
    InvalidateScope& operator=(const InvalidateScope&) = delete;
};

inline bool checkEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("PETARI_PIPELINE_FAST_CHECK");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    return enabled;
}

// At exit (petari_gx_pipeline_report, from the frame seam's exit report), when checking.
inline void reportIfChecking() {
    if (!checkEnabled()) return;
    std::fprintf(stderr, "[gx pipeline fast path] hits=%llu misses=%llu check=1 check_mismatches=%llu\n",
                 static_cast<unsigned long long>(hits.load()), static_cast<unsigned long long>(misses.load()),
                 static_cast<unsigned long long>(checkMismatches.load()));
}

constexpr std::size_t kEntries = 2048;  // direct-mapped, per thread (32 KiB)
struct Entry {
    std::uint64_t key = 0;
    std::uint64_t generation = 0;
};
inline thread_local std::array<Entry, kEntries> entries{};

inline Entry& slot(std::uint64_t key) {
    // Keys are xxh3 hashes already; fold the high half in for the index.
    return entries[(key ^ (key >> 32)) & (kEntries - 1)];
}

inline bool hit(std::uint64_t key, std::uint64_t gen) {
    const Entry& entry = slot(key);
    return entry.key == key && entry.generation == gen;
}

inline void store(std::uint64_t key, std::uint64_t gen) {
    Entry& entry = slot(key);
    entry.key = key;
    entry.generation = gen;
}

}  // namespace PetariFastPipeline
