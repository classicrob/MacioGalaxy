// The level editor's picking requests and answers (petari/editor_pick.hpp).
#include "petari/editor_pick.hpp"
#include "petari/host_allocation.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace PetariNative::EditorPick {
namespace {

std::atomic<bool> gHoverWanted{false};
std::atomic<int> gClick{0};
std::atomic<bool> gClear{false};
std::atomic<bool> gByNameWanted{false};
std::vector<RowRequest> gRowRequests;  // under gMutex
std::atomic<bool> gPlaceMarker{false};
std::uint64_t gPlace = 0;  // row and option under gMutex
bool gPlaceKeepLinks = false;
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
void place(std::uint64_t exampleRow, bool keepLinks) {
    std::lock_guard<std::mutex> lock(gMutex);
    gPlace = exampleRow;
    gPlaceKeepLinks = keepLinks;
}
std::uint64_t takePlace(bool* keepLinks) {
    std::lock_guard<std::mutex> lock(gMutex);
    const auto row = gPlace;
    if (keepLinks) *keepLinks = gPlaceKeepLinks;
    gPlace = 0;
    gPlaceKeepLinks = false;
    return row;
}

namespace {
PlaceClass gPlaceClass;  // under gMutex
bool gPlaceClassPending = false;
}  // namespace

void placeClass(const PlaceClass& request) {
    std::lock_guard<std::mutex> lock(gMutex);
    gPlaceClass = request;
    gPlaceClassPending = true;
}

bool takePlaceClass(PlaceClass* out) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (!gPlaceClassPending) return false;
    *out = gPlaceClass;
    gPlaceClassPending = false;
    return true;
}
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

namespace {
struct ThumbRequest {
    char name[64] = {};
    std::uint64_t rows[kThumbnailExamples] = {};
    const void* copy = nullptr;
};
std::vector<ThumbRequest> gThumbRequests, gThumbsDone;  // under gMutex
}  // namespace

void requestThumbnail(const char* className, const std::uint64_t* exampleRows, int count) {
    HostAllocationScope host;
    std::lock_guard<std::mutex> lock(gMutex);
    for (const ThumbRequest& r : gThumbRequests) {
        if (std::strcmp(r.name, className) == 0) return;
    }
    ThumbRequest r;
    std::snprintf(r.name, sizeof(r.name), "%s", className);
    for (int i = 0; i < count && i < kThumbnailExamples; i++) r.rows[i] = exampleRows[i];
    gThumbRequests.push_back(r);
}
bool takeThumbnailRequest(char* className, int size, std::uint64_t exampleRows[kThumbnailExamples]) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (gThumbRequests.empty()) return false;
    std::snprintf(className, static_cast<size_t>(size), "%s", gThumbRequests.front().name);
    for (int i = 0; i < kThumbnailExamples; i++) exampleRows[i] = gThumbRequests.front().rows[i];
    gThumbRequests.erase(gThumbRequests.begin());
    return true;
}
void publishThumbnail(const char* className, const void* copy) {
    HostAllocationScope host;
    std::lock_guard<std::mutex> lock(gMutex);
    ThumbRequest r;
    std::snprintf(r.name, sizeof(r.name), "%s", className);
    r.copy = copy;
    gThumbsDone.push_back(r);
}
bool takeThumbnail(char* className, int size, const void** copy) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (gThumbsDone.empty()) return false;
    std::snprintf(className, static_cast<size_t>(size), "%s", gThumbsDone.front().name);
    *copy = gThumbsDone.front().copy;
    gThumbsDone.erase(gThumbsDone.begin());
    return true;
}

void resetForTesting() {
    std::lock_guard<std::mutex> lock(gMutex);
    gPlace = 0;
    gPlaceKeepLinks = false;
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
