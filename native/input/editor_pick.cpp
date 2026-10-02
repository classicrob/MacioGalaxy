// The level editor's picking requests and answers (petari/editor_pick.hpp).
#include "petari/editor_pick.hpp"
#include "petari/host_allocation.hpp"

#include <atomic>
#include <mutex>

namespace PetariNative::EditorPick {
namespace {

std::atomic<bool> gHoverWanted{false};
std::atomic<int> gClick{0};
std::atomic<bool> gClear{false};
std::mutex gMutex;
bool gHoverValid = false;
Item gHover;
std::vector<Item> gSelection;

}  // namespace

void setHoverWanted(bool wanted) { gHoverWanted.store(wanted, std::memory_order_relaxed); }
void click(Click mode) { gClick.store(static_cast<int>(mode), std::memory_order_relaxed); }
void clearSelection() { gClear.store(true, std::memory_order_relaxed); }

bool hoverWanted() { return gHoverWanted.load(std::memory_order_relaxed); }
Click takeClick() { return static_cast<Click>(gClick.exchange(0, std::memory_order_relaxed)); }
bool takeClear() { return gClear.exchange(false, std::memory_order_relaxed); }

void publish(const Item* hover, const std::vector<Item>& selection) {
    HostAllocationScope host;  // the game thread's heap is the game's
    std::lock_guard<std::mutex> lock(gMutex);
    gHoverValid = hover != nullptr;
    if (hover != nullptr) gHover = *hover;
    gSelection = selection;
}

bool hover(Item* out) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (gHoverValid) *out = gHover;
    return gHoverValid;
}

std::vector<Item> selection() {
    HostAllocationScope host;
    std::lock_guard<std::mutex> lock(gMutex);
    return gSelection;
}

void resetForTesting() {
    std::lock_guard<std::mutex> lock(gMutex);
    gHoverWanted.store(false);
    gClick.store(0);
    gClear.store(false);
    gHoverValid = false;
    gSelection.clear();
}

}  // namespace PetariNative::EditorPick
