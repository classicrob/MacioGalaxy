// Metal compile cost of GX pipelines through the app's prebuilt Dawn (native/tools/PIPELINE_SEEDING.md).
// Not built by CMake. Build (D = build/aurora/_deps/dawn_prebuilt-src):
//   clang++ -std=c++20 -O1 -fobjc-arc native/tools/pipeline_compile_bench.mm -I$D/include $D/lib/libwebgpu_dawn.a \
//     -framework Foundation -framework IOSurface -framework QuartzCore -framework Cocoa -framework IOKit \
//     -framework Metal -framework CoreFoundation -o build/pipeline_compile_bench
// WGSL inputs: petari_pipeline_cache_inspect SEED.db OUT_DIR. Usage:
//   pipeline_compile_bench [--dump] [--threads N] [--ns NAME] file.wgsl...
// Every run uses the Darwin cache namespace NAME (default: a fresh per-PID one, i.e. a cold Metal cache) and prints
// it as "NAMESPACE <dir>" on stdout: delete that directory afterwards. --dump writes Dawn's blob-cache values
// (blobN.bin; the MSL is the length-prefixed string at "#ifdef __clang__") to the current directory.
// Uses the app's release toggles (skip_validation, disable_robustness, disable_symbol_renaming); auto layout.
#include <dawn/native/DawnNative.h>
#include <webgpu/webgpu_cpp.h>
#include <dlfcn.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <algorithm>

static std::string readFile(const char* p) {
    std::ifstream f(p);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

int main(int argc, char** argv) {
    bool dump = false;
    unsigned threads = 1;
    std::string ns;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--dump") dump = true;
        else if (a == "--threads") threads = std::stoi(argv[++i]);
        else if (a == "--ns") ns = argv[++i];
        else files.push_back(a);
    }
    if (ns.empty()) ns = "ChBench-" + std::to_string(getpid());
    using SetSuffix = int (*)(const char*);
    auto setSuffix = reinterpret_cast<SetSuffix>(dlsym(RTLD_DEFAULT, "_set_user_dir_suffix"));
    if (!setSuffix || !setSuffix(ns.c_str())) { std::fprintf(stderr, "suffix failed\n"); return 2; }
    char dir[1024];
    confstr(_CS_DARWIN_USER_CACHE_DIR, dir, sizeof dir);
    std::fprintf(stderr, "cache namespace %s\n", dir);

    std::array instanceToggles{"allow_unsafe_apis"};
    wgpu::DawnTogglesDescriptor itd;
    itd.enabledToggleCount = instanceToggles.size();
    itd.enabledToggles = instanceToggles.data();
    dawn::native::DawnInstanceDescriptor did{};
    did.nextInChain = &itd;
    did.backendValidationLevel = dawn::native::BackendValidationLevel::Disabled;
    did.SetLoggingCallback([](wgpu::LoggingType, wgpu::StringView m) {
        std::fprintf(stdout, "%.*s\n", int(m.length), m.data);
    });
    std::array feats{wgpu::InstanceFeatureName::TimedWaitAny};
    wgpu::InstanceDescriptor id{};
    id.nextInChain = &did;
    id.requiredFeatureCount = feats.size();
    id.requiredFeatures = feats.data();
    auto instance = wgpu::CreateInstance(&id);
    wgpu::Adapter adapter;
    wgpu::RequestAdapterOptions ao{};
    ao.backendType = wgpu::BackendType::Metal;
    ao.powerPreference = wgpu::PowerPreference::HighPerformance;
    instance.WaitAny(instance.RequestAdapter(&ao, wgpu::CallbackMode::WaitAnyOnly,
        [&](wgpu::RequestAdapterStatus, wgpu::Adapter a, wgpu::StringView) { adapter = std::move(a); }), UINT64_MAX);
    if (!adapter) { std::fprintf(stderr, "no adapter\n"); return 2; }
    wgpu::Limits supported{};
    adapter.GetLimits(&supported);
    wgpu::Limits limits{};
    limits.maxImmediateSize = 64;
    limits.maxStorageBuffersPerShaderStage = 2;
    std::vector<const char*> en{"skip_validation", "disable_robustness", "disable_symbol_renaming",
                                "enable_immediate_error_handling"};
    if (dump) en.push_back("dump_shaders");
    wgpu::DawnCacheDeviceDescriptor cache{};
    static std::mutex blobMutex;
    static int blobCount = 0;
    static bool sDump = false;
    sDump = dump;
    cache.SetDawnLoadCacheDataCallback([](std::span<const std::byte>, std::span<std::byte>) noexcept -> size_t { return 0; });
    cache.SetDawnStoreCacheDataCallback([](std::span<const std::byte>, std::span<const std::byte> value) noexcept {
        if (!sDump) return;
        std::lock_guard l(blobMutex);
        std::string name = "blob" + std::to_string(blobCount++) + ".bin";
        std::ofstream(name, std::ios::binary).write(reinterpret_cast<const char*>(value.data()), value.size());
    });
    wgpu::DawnTogglesDescriptor td;
    td.nextInChain = &cache;
    td.enabledToggleCount = en.size();
    td.enabledToggles = en.data();
    std::array dfeats{wgpu::FeatureName::DualSourceBlending};
    wgpu::DeviceDescriptor dd{};
    dd.nextInChain = &td;
    dd.requiredLimits = &limits;
    dd.requiredFeatureCount = dfeats.size();
    dd.requiredFeatures = dfeats.data();
    dd.SetUncapturedErrorCallback([](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView m) {
        std::fprintf(stderr, "error: %.*s\n", int(m.length), m.data);
    });
    dd.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous,
                             [](const wgpu::Device&, wgpu::DeviceLostReason, wgpu::StringView) {});
    wgpu::Device device;
    instance.WaitAny(adapter.RequestDevice(&dd, wgpu::CallbackMode::WaitAnyOnly,
        [&](wgpu::RequestDeviceStatus s, wgpu::Device d, wgpu::StringView m) {
            if (s != wgpu::RequestDeviceStatus::Success) std::fprintf(stderr, "device: %.*s\n", int(m.length), m.data);
            device = std::move(d);
        }), UINT64_MAX);
    if (!device) return 2;

    std::atomic<size_t> next{0};
    std::vector<double> ms(files.size());
    auto t0 = std::chrono::steady_clock::now();
    auto work = [&] {
        for (size_t i; (i = next++) < files.size();) {
            const auto src = readFile(files[i].c_str());
            auto start = std::chrono::steady_clock::now();
            wgpu::ShaderSourceWGSL w{};
            w.code = src.c_str();
            wgpu::ShaderModuleDescriptor smd{};
            smd.nextInChain = &w;
            auto mod = device.CreateShaderModule(&smd);
            wgpu::ColorTargetState ct{};
            ct.format = wgpu::TextureFormat::RGBA8Unorm;
            wgpu::FragmentState fs{};
            fs.module = mod;
            fs.entryPoint = "fs_main";
            fs.targetCount = 1;
            fs.targets = &ct;
            wgpu::DepthStencilState ds{};
            ds.format = wgpu::TextureFormat::Depth32Float;
            ds.depthWriteEnabled = true;
            ds.depthCompare = wgpu::CompareFunction::Less;
            wgpu::RenderPipelineDescriptor rp{};
            rp.vertex.module = mod;
            rp.vertex.entryPoint = "vs_main";
            rp.fragment = &fs;
            rp.depthStencil = &ds;
            auto p = device.CreateRenderPipeline(&rp);
            ms[i] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        }
    };
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t) pool.emplace_back(work);
    for (auto& t : pool) t.join();
    double wall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    double sum = 0;
    for (size_t i = 0; i < files.size(); ++i) {
        std::fprintf(stderr, "%s %.1f\n", files[i].c_str(), ms[i]);
        sum += ms[i];
    }
    auto sorted = ms;
    std::sort(sorted.begin(), sorted.end());
    std::fprintf(stderr, "n=%zu threads=%u wall_ms=%.0f sum_ms=%.0f p50=%.1f p90=%.1f max=%.1f\n", files.size(), threads,
                 wall, sum, sorted[sorted.size() / 2], sorted[sorted.size() * 9 / 10], sorted.back());
    std::printf("NAMESPACE %s\n", dir);
    return 0;
}
