// Aurora side of the RFL render test: window and Metal setup, frames, and
// mid-frame EFB readback (native/gx/efb_snapshot). Mirrors petari_gx_probe.

#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/gfx.h>
#include <aurora/main.h>
#include <gfx/render_worker.hpp>
#include <gx/fifo.hpp>
#include <petari/host_allocation.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

#include "efb_snapshot.hpp"
#include "rfl_render_aurora.h"
#include "webgpu/gpu.hpp"

namespace {

std::atomic<std::uint64_t> gReadyTicket{0};
std::string gStatePath;

void markReady(std::uint64_t ticket) {
    gReadyTicket.store(ticket, std::memory_order_release);
}

void logMessage(AuroraLogLevel level, const char* module, const char* message, unsigned int length) {
    std::fprintf(stderr, "[%s] %.*s\n", module, static_cast<int>(length), message);
    if (level == LOG_FATAL) {
        std::abort();
    }
}

}  // namespace

// aurora/main.h renames this to aurora_main; aurora::main provides main.
int main(int argc, char** argv) {
    return rfl_render_main(argc, argv);
}

extern "C" bool rfl_render_initialize(int argc, char** argv) {
    PetariNative::HostAllocationScope host;
    // This test reads pixels back and checks them, so every draw must reach the EFB. The default pipeline policy waits at most
    // ~6 ms per frame for a pending pipeline and then SKIPS that draw until its compile lands. With an empty pipeline cache
    // (build/rfl-render-state below: a fresh checkout or build directory, i.e. the first run of a clean full check) every pipeline
    // compiles from scratch, takes far more than 6 ms, and the fill, the face or the icon was skipped: the capture read zeros or the
    // wrong colour, differently on each run, until the cache was warm (then it passed, "always alone"). On an empty cache this
    // failed 3 of 3 runs with 18-23 skipped draws; blocking waits for each pipeline, 0 skipped. Set before the first pipeline
    // lookup (the policy is read once).
    setenv("PETARI_PIPELINE_POLICY", "blocking", 1);
    gStatePath = (std::filesystem::current_path() / "build/rfl-render-state").string();
    std::filesystem::create_directories(gStatePath);
    AuroraConfig config{};
    config.appName = "Petari - RFL render test";
    config.userPath = gStatePath.c_str();
    config.cachePath = gStatePath.c_str();
    config.desiredBackend = BACKEND_METAL;
    config.windowWidth = 640;
    config.windowHeight = 480;
    config.vsync = true;
    config.logCallback = logMessage;
    config.logLevel = LOG_INFO;
    const AuroraInfo info = aurora_initialize(argc, argv, &config);
    if (!info.window || aurora_get_backend() != BACKEND_METAL) {
        std::fprintf(stderr, "Metal initialization failed\n");
        aurora_shutdown();
        return false;
    }
    return true;
}

extern "C" bool rfl_render_begin_frame(void) {
    PetariNative::HostAllocationScope host;
    for (const AuroraEvent* event = aurora_update(); event && event->type != AURORA_NONE; ++event) {
        if (event->type == AURORA_EXIT) {
            return false;
        }
    }
    return aurora_begin_frame();
}

extern "C" void rfl_render_end_frame(void) {
    PetariNative::HostAllocationScope host;
    aurora_end_frame();
}

extern "C" void rfl_render_shutdown(void) {
    PetariNative::HostAllocationScope host;
    aurora_shutdown();
}

extern "C" bool rfl_render_capture(uint64_t ticket, RflRenderStats* stats) {
    PetariNative::HostAllocationScope host;
    aurora::gx::fifo::drain();
    PetariNative::GX::captureEfb(ticket, markReady);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (gReadyTicket.load(std::memory_order_acquire) != ticket) {
        if (std::chrono::steady_clock::now() >= deadline) {
            std::fprintf(stderr, "EFB capture %llu did not complete\n", static_cast<unsigned long long>(ticket));
            return false;
        }
        aurora::gfx::render_worker::enqueue_work([] { aurora::webgpu::g_instance.ProcessEvents(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    PetariNative::GX::SnapshotStats result{};
    if (!PetariNative::GX::snapshotStats(ticket, result)) {
        return false;
    }
    stats->differentColorPixels = result.differentColorPixels;
    stats->writtenDepthPixels = result.writtenDepthPixels;
    return true;
}

extern "C" bool rfl_render_pixel(uint64_t ticket, uint16_t x, uint16_t y, uint32_t* argb) {
    std::uint32_t depth = 0;
    return PetariNative::GX::readSnapshot(ticket, x, y, depth, *argb);
}

extern "C" void rfl_render_retire(uint64_t ticket) {
    PetariNative::GX::retireSnapshots(ticket);
}
