// CPU-only stage scheduling tests against the production stage implementation.
#include "../gx/pipeline_profile.hpp"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>
#include <sqlite3.h>
#include <stdexcept>
#include <petari/host_allocation.hpp>
// Scheduling-only harness; allocator routing is tested by platform allocation tests.
namespace PetariNative {
HostAllocationScope::HostAllocationScope() = default;
HostAllocationScope::~HostAllocationScope() = default;
}
namespace absl {
template<class K, class V> using flat_hash_map = std::unordered_map<K, V>;
template<class K> using flat_hash_set = std::unordered_set<K>;
}
namespace fmt {
template<class... Args> std::string format(const char* text, Args&&...) { return text; }
}
namespace aurora::gfx {
using HashType = uint64_t;
using PipelineRef = uint64_t;
namespace gx { constexpr unsigned GXPipelineConfigVersion = 13; struct PipelineConfig { unsigned version = 13, id = 0; }; }
namespace clear { constexpr unsigned ClearPipelineConfigVersion = 1; struct PipelineConfig { unsigned version = 1; bool clearColor = false, clearAlpha = false, clearDepth = false; }; }
enum class ShaderType { Clear, GX };
enum class PipelinePriority { Background, Normal, Blocking };
struct KnownPipeline { ShaderType type; std::variant<gx::PipelineConfig, clear::PipelineConfig> config; unsigned firstFrameUsed; };
struct Layout { uint64_t key; unsigned colorAttachmentCount = 2, sampleCount = 4; };
static Layout scene_render_target_layout() { return {5}; }
namespace detail { static void finalize_render_target_layout(Layout& value) { value.key = 9; } }
static uint64_t xxh3_hash(uint64_t value, uint64_t seed) { return value * 7919 + seed; }
static uint64_t xxh3_hash(const gx::PipelineConfig& value, uint64_t seed) { return value.id * 100 + seed; }
static uint64_t xxh3_hash(const clear::PipelineConfig& value, uint64_t) { return 10000 + value.clearColor + value.clearAlpha * 2 + value.clearDepth * 4; }
struct Pending { PipelineRef hash; };
struct Sample { bool drawRequested = false; };
struct Compiled { bool main = true; };
static std::mutex g_pipelineMutex;
static std::condition_variable g_pipelineReadyCv, g_pipelineQueueCv;
static std::atomic<bool> g_pipelineThreadEnd{false};
static unsigned summaryCalls = 0;
static unsigned petariActiveWorkers = 4;
static double petariCompileEwmaMs = 0;
static void petari_pipeline_summary() { ++summaryCalls; }
static std::deque<Pending> g_pipelineQueue, g_backgroundPipelineQueue;
static std::unordered_map<PipelineRef, Sample> petariPipelineSamples;
static std::unordered_map<PipelineRef, Compiled> g_pipelines;
static std::unordered_map<HashType, KnownPipeline> g_knownPipelines;
static struct { const char* resourcesPath = "/nonexistent-petari-stage-test"; } g_config;
static auto find_pending_pipeline(std::deque<Pending>& queue, PipelineRef key) {
    return std::find_if(queue.begin(), queue.end(), [=](const auto& item) { return item.hash == key; });
}
static void promote_pending_pipeline(PipelineRef key, PipelinePriority priority) {
    if (priority == PipelinePriority::Background) return;
    auto it = find_pending_pipeline(g_backgroundPipelineQueue, key);
    if (it != g_backgroundPipelineQueue.end()) {
        g_pipelineQueue.push_back(*it);
        g_backgroundPipelineQueue.erase(it);
    }
}
template<class Config>
static PipelineRef resolve_pipeline(ShaderType type, const Config& config, Layout layout, PipelinePriority priority) {
    if (priority == PipelinePriority::Blocking) throw std::runtime_error("stage begin must not block");
    const auto key = xxh3_hash(layout.key, xxh3_hash(config, static_cast<HashType>(type)));
    std::lock_guard lock(g_pipelineMutex);
    petariPipelineSamples.try_emplace(key);
    if (priority != PipelinePriority::Background) promote_pending_pipeline(key, priority);
    if (!g_pipelines.contains(key) && find_pending_pipeline(g_pipelineQueue, key) == g_pipelineQueue.end() &&
        find_pending_pipeline(g_backgroundPipelineQueue, key) == g_backgroundPipelineQueue.end())
        (priority == PipelinePriority::Background ? g_backgroundPipelineQueue : g_pipelineQueue).push_back({key});
    return key;
}
template<class Config>
static void remember_pipeline_config(ShaderType type, const Config& config, uint32_t firstUse, bool persist) {
    if (!persist || firstUse != UINT32_MAX) throw std::runtime_error("global speculation must persist as never drawn");
    g_knownPipelines.try_emplace(xxh3_hash(config, static_cast<HashType>(type)), KnownPipeline{type, config, firstUse});
}
enum class ManifestFixture { OpenFailure, QueryFailure, StepFailure, Empty, InvalidRow, Valid, HotOrdered };
static ManifestFixture manifestFixture = ManifestFixture::OpenFailure;
static sqlite3* open_pipeline_cache_seed_db(const std::string&) {
    if (manifestFixture == ManifestFixture::OpenFailure) return nullptr;
    sqlite3* db = nullptr;
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) throw std::runtime_error("fixture database open failed");
    const auto sql = [&](const char* statement) {
        if (sqlite3_exec(db, statement, nullptr, nullptr, nullptr) != SQLITE_OK)
            throw std::runtime_error("fixture SQL failed");
    };
    if (manifestFixture == ManifestFixture::QueryFailure) return db;
    if (manifestFixture == ManifestFixture::StepFailure) {
        sqlite3_create_function(db, "fail_manifest", 0, SQLITE_UTF8, nullptr,
            [](sqlite3_context* context, int, sqlite3_value**) { sqlite3_result_error(context, "injected read failure", -1); },
            nullptr, nullptr);
        sql("CREATE VIEW pipeline_cache AS SELECT 1 AS type,13 AS config_version,fail_manifest() AS config,0 AS first_frame_used");
        return db;
    }
    sql("CREATE TABLE pipeline_cache(type INTEGER,config_version INTEGER,config BLOB,first_frame_used INTEGER)");
    if (manifestFixture == ManifestFixture::InvalidRow)
        sql("INSERT INTO pipeline_cache VALUES(1,13,x'00',0)");
    const auto insertRow = [&](unsigned id, sqlite3_int64 firstUse) {
        sqlite3_stmt* insert = nullptr;
        if (sqlite3_prepare_v2(db, "INSERT INTO pipeline_cache VALUES(1,13,?,?)", -1, &insert, nullptr) != SQLITE_OK)
            throw std::runtime_error("fixture insert prepare failed");
        const gx::PipelineConfig config{13, id};
        sqlite3_bind_blob(insert, 1, &config, sizeof(config), SQLITE_TRANSIENT);
        sqlite3_bind_int64(insert, 2, firstUse);
        const auto result = sqlite3_step(insert);
        sqlite3_finalize(insert);
        if (result != SQLITE_DONE) throw std::runtime_error("fixture insert failed");
    };
    if (manifestFixture == ManifestFixture::Valid) insertRow(42, 0);
    if (manifestFixture == ManifestFixture::HotOrdered) {
        // order_pipeline_seeds.py layout: hot ranks, other observed at 1,000,000 + rank, speculative never.
        insertRow(61, 4294967295LL);
        insertRow(62, 1000000);
        insertRow(63, 0);
        insertRow(64, 1);
    }
    return db;
}
}
#include "../gx/pipeline_stage.inc"
using namespace aurora::gfx;
static void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
// The gate waits for the hot rows and clear masks only; the queue still holds the
// whole manifest in first_frame_used order (hot, other observed, speculative).
static void hotGateTest() {
    const auto root = std::filesystem::temp_directory_path() /
        ("petari-stage-hot-" + std::to_string(PetariPipeline::Clock::now().time_since_epoch().count()));
    require(std::filesystem::create_directory(root), "fixture directory failed");
    { std::ofstream marker(root / "HotStage.db"); marker << "fixture"; }
    setenv("PETARI_PIPELINE_SEED_DIR", root.c_str(), 1);
    manifestFixture = ManifestFixture::HotOrdered;
    g_pipelineQueue.clear(); g_backgroundPipelineQueue.clear(); g_pipelines.clear();
    petari_gx_pipeline_stage_begin("HotStage", nullptr);
    const auto key = [](unsigned id, uint64_t layout) {
        return xxh3_hash(layout, xxh3_hash(gx::PipelineConfig{13, id}, static_cast<HashType>(ShaderType::GX)));
    };
    const auto& stage = petariStages["HotStage"];
    require(stage.targets.size() == 24 && stage.gateTargets.size() == 20, "gate must hold clears plus hot rows only");
    for (const auto layout : {5ull, 9ull}) {
        const auto has = [&](unsigned id) {
            return std::find(stage.gateTargets.begin(), stage.gateTargets.end(), key(id, layout)) != stage.gateTargets.end();
        };
        require(has(63) && has(64) && !has(62) && !has(61), "gate targets are not the hot rows");
    }
    std::vector<PipelineRef> order;
    for (const auto& job : g_pipelineQueue) order.push_back(job.hash);
    const auto position = [&](unsigned id) { return std::find(order.begin(), order.end(), key(id, 5)) - order.begin(); };
    require(position(63) < position(64) && position(64) < position(62) && position(62) < position(61),
            "manifest not queued in first_frame_used order");
    for (const auto target : stage.gateTargets) g_pipelines[target] = {};
    const auto start = PetariPipeline::Clock::now();
    petari_gx_pipeline_stage_wait();
    require(PetariPipeline::Clock::now() - start < std::chrono::milliseconds(200),
            "gate waited for rows outside the hot set");
    unsetenv("PETARI_PIPELINE_SEED_DIR");
    std::filesystem::remove_all(root);
    petari_stage_reset();
    g_pipelineQueue.clear(); g_pipelines.clear();
}
static void manifestFailureTest() {
    const auto initial = petari_gx_pipeline_manifest_failure_count();
    petari_gx_pipeline_stage_begin("MissingOptionalSeed", nullptr);
    require(petari_gx_pipeline_manifest_failure_count() == initial, "absent optional seed counted as read failure");
    const auto root = std::filesystem::temp_directory_path() /
        ("petari-stage-manifest-" + std::to_string(PetariPipeline::Clock::now().time_since_epoch().count()));
    require(std::filesystem::create_directory(root), "fixture directory failed");
    setenv("PETARI_PIPELINE_SEED_DIR", root.c_str(), 1);
    unsigned failures = 0;
    for (const auto mode : {ManifestFixture::OpenFailure, ManifestFixture::QueryFailure, ManifestFixture::StepFailure,
                           ManifestFixture::Empty, ManifestFixture::InvalidRow, ManifestFixture::Valid}) {
        manifestFixture = mode;
        const auto stage = "ManifestCase" + std::to_string(static_cast<unsigned>(mode));
        { std::ofstream marker(root / (stage + ".db")); marker << "fixture"; }
        petari_gx_pipeline_stage_begin(stage.c_str(), nullptr);
        if (mode != ManifestFixture::Valid) ++failures;
        require(petari_gx_pipeline_manifest_failure_count() == initial + failures, "manifest failure count mismatch");
        petari_gx_pipeline_stage_begin(stage.c_str(), nullptr);
        require(petari_gx_pipeline_manifest_failure_count() == initial + failures, "idempotent begin counted failure twice");
    }
    unsetenv("PETARI_PIPELINE_SEED_DIR");
    std::filesystem::remove_all(root);
    petari_stage_reset();
    require(petari_gx_pipeline_manifest_failure_count() == initial + failures, "reset hid manifest failures");
}
int main() {
    unsetenv("PETARI_PIPELINE_SEED_DIR");
    g_knownPipelines.emplace(1, KnownPipeline{ShaderType::GX, gx::PipelineConfig{13, 1}, 0});
    unsetenv("PETARI_PIPELINE_GLOBAL_PRECOMPILE");
    require(PetariPipeline::backgroundGlobalPrecompile() && !PetariPipeline::globalPrecompile(),
            "background global preparation must be the default, without the blocking screen");
    setenv("PETARI_PIPELINE_GLOBAL_PRECOMPILE", "0", 1);
    petari_gx_pipeline_background_begin();
    require(petariStages.empty(), "explicit 0 must disable background global preparation");
    unsetenv("PETARI_PIPELINE_GLOBAL_PRECOMPILE");
    petari_gx_pipeline_stage_begin("AstroGalaxy", nullptr);
    const auto originalCount = g_pipelineQueue.size();
    require(originalCount == 18, "expected eight clear masks and one GX in two layouts");
    petari_gx_pipeline_stage_begin("AstroGalaxy", nullptr);
    require(g_pipelineQueue.size() == originalCount, "same-stage begin duplicated jobs");
    petariStages["AstroGalaxy"].prepared = true;
    petari_gx_pipeline_stage_begin("ScenarioSelect", nullptr);
    petari_gx_pipeline_stage_begin("GalaxyMap", nullptr);
    require(petariActiveStage == "AstroGalaxy" && petariStages["AstroGalaxy"].prepared,
            "overlay replaced active stage attribution or preparation status");
    require(g_backgroundPipelineQueue.empty() && g_pipelineQueue.size() == originalCount,
            "overlay demoted active jobs or duplicated them");
    petari_note_stage_config(12345, 999);
    require(petariStages["AstroGalaxy"].seen.contains(12345) && petariStages["ScenarioSelect"].seen.contains(12345),
            "first use missing active-stage or overlay attribution");
    require(petariStageLogs.size() == 3, "first-use tags missing");
    petari_note_stage_config(12345, 999);
    require(petariStageLogs.size() == 3, "duplicate first-use log");
    for (const auto& job : g_pipelineQueue) g_pipelines[job.hash] = {};
    petari_gx_pipeline_stage_wait();
    require(petariStages["AstroGalaxy"].prepared && petariStages["GalaxyMap"].prepared, "ready state not recorded");
    g_pipelineQueue.clear(); g_pipelines.clear();
    // Leave only an old speculative config and a real draw request outstanding.
    petariStages["AstroGalaxy"].targets = {100, 101};
    petariPipelineSamples[101].drawRequested = true;
    g_pipelineQueue.push_back({100}); g_pipelineQueue.push_back({101});
    petari_gx_pipeline_stage_begin("EggStarGalaxy", nullptr);
    require(petariActiveStage == "EggStarGalaxy" && petariOverlayStages.empty(), "stage switch kept obsolete overlay tags");
    require(find_pending_pipeline(g_backgroundPipelineQueue, 100) != g_backgroundPipelineQueue.end(), "old speculation not demoted");
    require(find_pending_pipeline(g_pipelineQueue, 101) != g_pipelineQueue.end(), "real draw request demoted");
    petari_stage_reset();
    require(petariActiveStage.empty() && petariStages.empty(), "shutdown retained stage state");
    g_pipelineQueue.clear(); g_backgroundPipelineQueue.clear(); g_pipelines.clear();
    petari_gx_pipeline_stage_begin("Active", nullptr);
    for (const auto& job : g_pipelineQueue) g_pipelines[job.hash] = {};
    g_pipelineQueue.clear();
    g_knownPipelines.emplace(999, KnownPipeline{ShaderType::GX, gx::PipelineConfig{13, 999}, 0});
    setenv("PETARI_PIPELINE_GLOBAL_PRECOMPILE", "background", 1);
    require(!PetariPipeline::globalPrecompile(), "background mode enabled blocking global startup");
    petari_gx_pipeline_background_begin();
    require(petariActiveStage == "Active" && petariOverlayStages.empty(), "global jobs changed stage attribution or gates");
    require(g_pipelineQueue.empty() && g_backgroundPipelineQueue.size() == 2, "global jobs were not background priority");
    petari_gx_pipeline_background_begin();
    require(g_pipelineQueue.empty() && g_backgroundPipelineQueue.size() == 2, "repeated global begin promoted or duplicated jobs");
    petari_gx_pipeline_stage_wait();
    require(petariStages["Active"].prepared, "global jobs blocked an already-ready stage gate");
    petari_note_stage_config(123456, 999999);
    require(petariStages["__global__"].seen.empty(), "global job coverage hid a stage gap");
    const gx::PipelineConfig speculative{13, 1001};
    const auto speculativeHash = xxh3_hash(speculative, static_cast<HashType>(ShaderType::GX));
    remember_pipeline_config(ShaderType::GX, speculative, UINT32_MAX, true);
    const auto speculativeScene = resolve_pipeline(ShaderType::GX, speculative, Layout{5}, PipelinePriority::Background);
    const auto speculativeOffscreen = resolve_pipeline(ShaderType::GX, speculative, Layout{9}, PipelinePriority::Background);
    petari_gx_pipeline_stage_begin("MissingManifest", nullptr);
    require(!petariStages["MissingManifest"].configs.contains(speculativeHash),
            "missing stage seed adopted speculative global configs");
    require(find_pending_pipeline(g_backgroundPipelineQueue, speculativeScene) != g_backgroundPipelineQueue.end() &&
            find_pending_pipeline(g_backgroundPipelineQueue, speculativeOffscreen) != g_backgroundPipelineQueue.end(),
            "missing stage seed promoted global background work");
    unsetenv("PETARI_PIPELINE_GLOBAL_PRECOMPILE");
    petari_stage_reset();
    // A cold stage must not freeze scene start: the gate gives up after its short budget.
    g_pipelineQueue.clear(); g_backgroundPipelineQueue.clear(); g_pipelines.clear();
    petari_gx_pipeline_stage_begin("ColdStage", nullptr);
    require(!g_pipelineQueue.empty(), "cold stage queued nothing");
    const auto gateStart = PetariPipeline::Clock::now();
    petari_gx_pipeline_stage_wait();
    const auto gateWaited = PetariPipeline::Clock::now() - gateStart;
    require(gateWaited >= PetariPipeline::stageGateBudget() - std::chrono::milliseconds(5) &&
            gateWaited < PetariPipeline::stageGateBudget() + std::chrono::milliseconds(500) &&
            PetariPipeline::stageGateBudget() <= std::chrono::milliseconds(50),
            "stage gate did not proceed after its short default budget");
    petari_stage_reset();
    // At a known cold compile cost the same gate cannot finish in budget: no wait at all.
    g_pipelineQueue.clear(); g_pipelines.clear();
    petari_gx_pipeline_stage_begin("ColdStage", nullptr);
    petariCompileEwmaMs = 1000.0 * PetariPipeline::milliseconds(PetariPipeline::stageGateBudget());
    const auto skipStart = PetariPipeline::Clock::now();
    petari_gx_pipeline_stage_wait();
    require(PetariPipeline::Clock::now() - skipStart < std::chrono::milliseconds(100), "hopeless gate still froze the scene");
    require(petariStages["ColdStage"].prepared, "skipped gate did not mark the stage prepared");
    petariCompileEwmaMs = 0;
    petari_stage_reset();
    g_pipelineQueue.clear();
    manifestFailureTest();
    hotGateTest();
    petari_gx_pipeline_report();
    require(summaryCalls == 1, "explicit pipeline report did not reach summary");
    std::puts("Stage preparation: manifest failure counter, idempotence, additive overlays, first-use tags, ready gate, short cold gate, hopeless-gate skip, hot-row gate and priority demotion pass");
}
