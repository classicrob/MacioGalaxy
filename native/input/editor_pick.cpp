// The level editor's picking requests and answers (petari/editor_pick.hpp).
#include "petari/editor_pick.hpp"
#include "petari/host_allocation.hpp"

#include <atomic>
#include <cstdio>
#include <mutex>

namespace PetariNative::EditorPick {
namespace {

std::atomic<bool> gHoverWanted{false};
std::atomic<int> gClick{0};
std::atomic<bool> gClear{false};
std::atomic<bool> gByNameWanted{false};
std::vector<RowRequest> gRowRequests;  // under gMutex
std::atomic<bool> gPlaceMarker{false};
std::atomic<std::uint64_t> gPlace{0};
std::atomic<bool> gDuplicate{false};
char gByName[64] = {};
std::mutex gMutex;
bool gHoverValid = false;
Item gHover;
std::vector<Item> gSelection;

}  // namespace

void setHoverWanted(bool wanted) { gHoverWanted.store(wanted, std::memory_order_relaxed); }
void click(Click mode) { gClick.store(static_cast<int>(mode), std::memory_order_relaxed); }
void clearSelection() { gClear.store(true, std::memory_order_relaxed); }
void selectByName(const char* name) {
    std::lock_guard<std::mutex> lock(gMutex);
    std::snprintf(gByName, sizeof(gByName), "%s", name != nullptr ? name : "");
    gByNameWanted.store(true, std::memory_order_release);
}
void placeMarker() { gPlaceMarker.store(true, std::memory_order_relaxed); }
void place(std::uint64_t exampleRow) { gPlace.store(exampleRow, std::memory_order_relaxed); }
std::uint64_t takePlace() { return gPlace.exchange(0, std::memory_order_relaxed); }
void duplicate() { gDuplicate.store(true, std::memory_order_relaxed); }
bool takeDuplicate() { return gDuplicate.exchange(false, std::memory_order_relaxed); }
bool takePlaceMarker() { return gPlaceMarker.exchange(false, std::memory_order_relaxed); }

void selectRow(std::uint64_t row, Click mode) {
    HostAllocationScope host;
    std::lock_guard<std::mutex> lock(gMutex);
    gRowRequests.push_back({row, mode});
}
std::vector<RowRequest> takeSelectRows() {
    HostAllocationScope host;
    std::lock_guard<std::mutex> lock(gMutex);
    std::vector<RowRequest> out;
    out.swap(gRowRequests);
    return out;
}

bool takeSelectByName(char* name, int size) {
    if (!gByNameWanted.exchange(false, std::memory_order_acquire)) return false;
    std::lock_guard<std::mutex> lock(gMutex);
    std::snprintf(name, static_cast<size_t>(size), "%s", gByName);
    return true;
}

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

namespace PetariNative::EditorGizmo {
namespace {

std::atomic<int> gMode{static_cast<int>(Mode::Move)};
std::atomic<int> gFrame{static_cast<int>(Frame::Gravity)};
std::atomic<bool> gPressed{false}, gReleased{false}, gSnapSuspended{false};
std::atomic<int> gGround{0};
std::atomic<bool> gShowGhosts{true};
std::mutex gViewMutex;
View gView;

}  // namespace

void setMode(Mode mode) { gMode.store(static_cast<int>(mode), std::memory_order_relaxed); }
Mode mode() { return static_cast<Mode>(gMode.load(std::memory_order_relaxed)); }
void setFrame(Frame frame) { gFrame.store(static_cast<int>(frame), std::memory_order_relaxed); }
Frame frame() { return static_cast<Frame>(gFrame.load(std::memory_order_relaxed)); }
void press() { gPressed.store(true, std::memory_order_relaxed); }
void release() { gReleased.store(true, std::memory_order_relaxed); }
void setSnapSuspended(bool suspended) { gSnapSuspended.store(suspended, std::memory_order_relaxed); }
void ground(Ground what) { gGround.store(static_cast<int>(what), std::memory_order_relaxed); }
void setShowGhosts(bool show) { gShowGhosts.store(show, std::memory_order_relaxed); }
bool showGhosts() { return gShowGhosts.load(std::memory_order_relaxed); }

Take take() {
    Take t;
    t.pressed = gPressed.exchange(false, std::memory_order_relaxed);
    t.released = gReleased.exchange(false, std::memory_order_relaxed);
    t.snapSuspended = gSnapSuspended.load(std::memory_order_relaxed);
    t.ground = gGround.exchange(0, std::memory_order_relaxed);
    return t;
}

void publish(const View& view) {
    HostAllocationScope host;
    std::lock_guard<std::mutex> lock(gViewMutex);
    gView = view;
}

View view() {
    HostAllocationScope host;
    std::lock_guard<std::mutex> lock(gViewMutex);
    return gView;
}

void resetForTesting() {
    gMode.store(static_cast<int>(Mode::Move));
    gFrame.store(static_cast<int>(Frame::Gravity));
    gPressed.store(false);
    gReleased.store(false);
    gSnapSuspended.store(false);
    gGround.store(0);
    std::lock_guard<std::mutex> lock(gViewMutex);
    gView = View{};
}

}  // namespace PetariNative::EditorGizmo
