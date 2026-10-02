// The editor's engine hooks (petari/editor/runtime_hooks.hpp): the on switch, headroom, and the
// synthesized-row registry.
#include "petari/editor/runtime_hooks.hpp"

#include <atomic>
#include <cstdlib>
#include <vector>

namespace PetariEditor::Hooks {
namespace {
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

std::uint32_t withHeadroom(std::uint32_t count) {
    return enabled() ? count + count / 2 + 16 : count;
}

void registerRows(const void* pBegin, std::size_t size, std::int32_t zoneId) {
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
    placedList().push_back({pTableData, row, pActor});
}

const std::vector< Placed >& placed() {
    return placedList();
}

namespace {
    std::uint32_t gSceneSerial = 1;
}

std::uint32_t sceneSerial() {
    return gSceneSerial;
}

void sceneDestroyed() {
    ranges().clear();
    placedList().clear();
    gSceneSerial++;
}

}  // namespace PetariEditor::Hooks
