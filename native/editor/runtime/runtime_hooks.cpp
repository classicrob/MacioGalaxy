// The editor's engine hooks (petari/editor/runtime_hooks.hpp): the on switch, headroom, and the
// synthesized-row registry.
#include "petari/editor/runtime_hooks.hpp"
#include "runtime_core.hpp"
#include <petari/host_allocation.hpp>

#include <petari/smoke_phase.hpp>

#include <atomic>
#include <map>
#include <string>
#include <cstdlib>
#include <vector>

namespace PetariEditor::Hooks {
namespace {
    std::map< int, RailTables > gRailTables;
    std::atomic< int > gEnabled{-1};  // -1: not yet read from the environment

    struct Range {
        std::uintptr_t begin, end;
        std::int32_t zone;
    };
    std::vector< Range >& ranges() {
        static std::vector< Range > sRanges;
        return sRanges;
    }
}  // namespace

bool enabled() {
    int value = gEnabled.load(std::memory_order_relaxed);
    if (value < 0) {
        const char* pEnv = std::getenv("PETARI_EDITOR");
        value = pEnv != nullptr && pEnv[0] == '1' ? 1 : 0;
        gEnabled.store(value, std::memory_order_relaxed);
    }
    return value == 1;
}

void setEnabled(bool on) {
    gEnabled.store(on ? 1 : 0, std::memory_order_relaxed);
}

void syncSwitch(bool modSwitchOn) {
    const char* pEnv = std::getenv("PETARI_EDITOR");
    setEnabled(modSwitchOn || (pEnv != nullptr && pEnv[0] == '1'));
}

std::uint32_t withHeadroom(std::uint32_t count) {
    return enabled() ? count + count / 2 + 16 : count;
}

void registerRows(const void* pBegin, std::size_t size, std::int32_t zoneId) {
    PetariNative::HostAllocationScope host;  // editor bookkeeping, called from game code
    const auto begin = reinterpret_cast< std::uintptr_t >(pBegin);
    ranges().push_back({begin, begin + size, zoneId});
}

void unregisterRows(const void* pBegin) {
    const auto begin = reinterpret_cast< std::uintptr_t >(pBegin);
    std::vector< Range >& list = ranges();
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (it->begin == begin) {
            list.erase(it);
            return;
        }
    }
}

std::int32_t findSyntheticZone(std::uintptr_t rowAddress) {
    for (const Range& range : ranges()) {
        if (range.begin <= rowAddress && rowAddress < range.end) {
            return range.zone;
        }
    }
    return -1;
}

namespace {
    std::vector< Placed >& placedList() {
        static std::vector< Placed > sPlaced;
        return sPlaced;
    }
}  // namespace

void notePlaced(const void* pTableData, int row, void* pActor) {
    PetariNative::HostAllocationScope host;  // editor bookkeeping, called from game code
    placedList().push_back({pTableData, row, pActor});
}

const std::vector< Placed >& placed() {
    return placedList();
}

namespace {
    std::uint32_t gSceneSerial = 1;
    bool gEditorReload = false;

    std::string lowerPath(const char* pPath) {
        std::string out(pPath);
        for (char& c : out) {
            c = static_cast< char >(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        }
        return out;
    }
    std::map< std::string, std::pair< std::string, std::vector< std::uint8_t > > >& overrides() {
        static std::map< std::string, std::pair< std::string, std::vector< std::uint8_t > > > sOverrides;  // lower -> (path, bytes)
        return sOverrides;
    }
}  // namespace

void setArchiveOverride(const std::string& path, std::vector< std::uint8_t > bytes) {
    PetariNative::HostAllocationScope host;  // editor bookkeeping, called from game code
    overrides()[lowerPath(path.c_str())] = {path, std::move(bytes)};
}

void clearArchiveOverrides() {
    overrides().clear();
}

void removeArchiveOverride(const std::string& path) {
    overrides().erase(lowerPath(path.c_str()));
}

const std::vector< std::uint8_t >* archiveOverride(const char* pPath) {
    if (overrides().empty()) {
        return nullptr;  // asked for every file lookup (MR::isFileExist): no allocation when unused
    }
    PetariNative::HostAllocationScope host;  // editor bookkeeping, called from game code
    auto it = overrides().find(lowerPath(pPath));
    return it == overrides().end() ? nullptr : &it->second.second;
}

std::vector< std::string > archiveOverridePaths() {
    PetariNative::HostAllocationScope host;  // editor bookkeeping, called from game code
    std::vector< std::string > out;
    for (const auto& [key, entry] : overrides()) {
        out.push_back(entry.first);
    }
    return out;
}

void reportError(const char* pMessage) {
    PetariNative::HostAllocationScope host;  // editor bookkeeping, called from game code
    Core::reportError(pMessage);
}

void setEditorReload(bool active) {
    gEditorReload = active;
    PetariNative::SmokePhase::editorReloading.store(active);
}

bool editorReload() {
    return gEditorReload;
}

std::uint32_t sceneSerial() {
    return gSceneSerial;
}

void setRailTables(int zoneId, RailTables tables) {
    PetariNative::HostAllocationScope host;
    gRailTables[zoneId] = std::move(tables);
}
const RailTables* railTables(int zoneId) {
    if (!enabled())
        return nullptr;
    const auto it = gRailTables.find(zoneId);
    return it == gRailTables.end() ? nullptr : &it->second;
}
void clearRailTables() {
    gRailTables.clear();
}

void sceneDestroyed() {
    clearRailTables();
    ranges().clear();
    placedList().clear();
    gSceneSerial++;
}

}  // namespace PetariEditor::Hooks
