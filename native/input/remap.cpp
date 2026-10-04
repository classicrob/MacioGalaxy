// Remapping from the Home menu and controls.txt (petari/input_remap.hpp).
// Capture itself lives with the host events in wpad_host.cpp.

#include <algorithm>
#include <fstream>
#include <mutex>
#include <sstream>
#include <system_error>

#include "petari/input_remap.hpp"

namespace PetariNative::Input::Remap {
namespace {

bool isPad(const Binding& input) {
    return input.device == Binding::Device::Pad;
}

// Positions in `inputs` of the inputs of one kind, in order.
std::vector<size_t> positions(const std::vector<Binding>& inputs, bool pad) {
    std::vector<size_t> result;
    for (size_t i = 0; i < inputs.size(); ++i) {
        if (isPad(inputs[i]) == pad) result.push_back(i);
    }
    return result;
}

void setInputs(Bindings& bindings, Action action, const std::vector<Binding>& inputs) {
    bindings.clear(action);
    for (const Binding& input : inputs) bindings.bind(action, input);
}

bool contains(const std::vector<Binding>& inputs, const Binding& input) {
    return std::find(inputs.begin(), inputs.end(), input) != inputs.end();
}

// Applies a change unless it would lock the player out of the Home menu.
Result commit(Bindings& bindings, const Bindings& next) {
    if (!homeReachable(next)) return Result::HomeUnreachable;
    bindings = next;
    return Result::Done;
}

std::mutex gFileMutex;
std::filesystem::path gFile;
std::string gLoadMessage;
bool gReplaceBad = false;  // the loaded file was invalid: keep it as .bad when first replaced

}  // namespace

bool slotInput(const Bindings& bindings, Action action, int slot, Binding* input) {
    if (slot < 0 || slot >= kSlots) return false;
    const std::vector<Binding>& inputs = bindings.inputs(action);
    const std::vector<size_t> at = positions(inputs, padSlot(slot));
    const size_t k = static_cast<size_t>(slot % kFirstPadSlot);
    if (k >= at.size()) return false;
    *input = inputs[at[k]];
    return true;
}

std::vector<Action> boundElsewhere(const Bindings& bindings, Action action, Binding input) {
    std::vector<Action> result;
    for (int a = 0; a < static_cast<int>(Action::Count); ++a) {
        const Action other = static_cast<Action>(a);
        if (other != action && contains(bindings.inputs(other), input)) result.push_back(other);
    }
    return result;
}

std::vector<Action> conflicts(const Bindings& bindings, Action action, Binding input) {
    static const Bindings kDefaults = Bindings::defaults();
    std::vector<Action> result;
    for (const Action other : boundElsewhere(bindings, action, input)) {
        const bool sharedByDefault = contains(kDefaults.inputs(action), input) && contains(kDefaults.inputs(other), input);
        if (!sharedByDefault) result.push_back(other);
    }
    return result;
}

// Files saved before the B key became a default (2026-10-04, the Point to move mod) list
// B as the old default. Such a line gets the B key, unless the player bound it elsewhere.
void migrateBKey(Bindings& bindings) {
    const std::vector<Binding> oldDefault = {Binding::mouse(MouseButton::Left), Binding::key(Key::Backspace),
                                             Binding::pad(PadButton::East), Binding::pad(PadButton::RightTrigger)};
    if (bindings.inputs(Action::B) != oldDefault) return;
    for (int i = 0; i < static_cast<int>(Action::Count); ++i) {
        for (const Binding& input : bindings.inputs(static_cast<Action>(i))) {
            if (input == Binding::key(Key::B)) return;
        }
    }
    setInputs(bindings, Action::B, Bindings::defaults().inputs(Action::B));
}

bool homeReachable(const Bindings& bindings) {
    for (const Binding& input : bindings.inputs(Action::Home)) {
        if (!isPad(input)) return true;
    }
    return false;
}

Result assign(Bindings& bindings, Action action, int slot, Binding input, Resolve resolve) {
    if (slot < 0 || slot >= kSlots) return Result::Unchanged;
    const bool pad = isPad(input);
    const size_t k = static_cast<size_t>(slot % kFirstPadSlot);  // the same-numbered slot of the input's own kind
    std::vector<Binding> inputs = bindings.inputs(action);
    const std::vector<size_t> at = positions(inputs, pad);
    const bool hasOld = k < at.size();
    const Binding old = hasOld ? inputs[at[k]] : Binding{};
    if (hasOld && old == input) return Result::Unchanged;

    Bindings next = bindings;
    const auto existing = std::find(inputs.begin(), inputs.end(), input);
    if (existing != inputs.end()) {
        // Already in this action's other slot: the two slots trade places.
        if (!hasOld) return Result::Unchanged;
        std::iter_swap(existing, inputs.begin() + static_cast<std::ptrdiff_t>(at[k]));
        setInputs(next, action, inputs);
        return commit(bindings, next);
    }
    if (hasOld) {
        inputs[at[k]] = input;
    } else {
        inputs.push_back(input);  // the first empty slot of its kind
    }
    setInputs(next, action, inputs);

    const std::vector<Action> others = boundElsewhere(bindings, action, input);
    if (!others.empty()) {
        if (resolve == Resolve::Check) return Result::Conflict;
        if (resolve == Resolve::Swap) {
            for (const Action other : others) {
                std::vector<Binding> theirs = bindings.inputs(other);
                const auto it = std::find(theirs.begin(), theirs.end(), input);
                if (hasOld && !contains(theirs, old)) {
                    *it = old;
                } else {
                    theirs.erase(it);
                }
                setInputs(next, other, theirs);
            }
        }
    }
    return commit(bindings, next);
}

Result clearSlot(Bindings& bindings, Action action, int slot) {
    Binding input;
    if (!slotInput(bindings, action, slot, &input)) return Result::Unchanged;
    Bindings next = bindings;
    next.unbind(action, input);
    return commit(bindings, next);
}

Result resetAction(Bindings& bindings, Action action) {
    const Bindings defaults = Bindings::defaults();
    if (bindings.inputs(action) == defaults.inputs(action)) return Result::Unchanged;
    Bindings next = bindings;
    setInputs(next, action, defaults.inputs(action));
    return commit(bindings, next);
}

bool load(const std::filesystem::path& path, Bindings* bindings, std::string* message) {
    std::lock_guard<std::mutex> lock(gFileMutex);
    gFile = path;
    gLoadMessage.clear();
    gReplaceBad = false;
    *bindings = Bindings::defaults();
    message->clear();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return true;
    }
    std::ifstream in(path);
    std::stringstream text;
    if (in) text << in.rdbuf();
    Bindings parsed = Bindings::defaults();
    std::string error;
    if (!in) {
        error = "cannot be read";
    } else if (!parsed.parse(text.str(), &error)) {
        error = "has an error (" + error + ")";
    }
    if (!error.empty()) {
        *message = path.filename().string() + " " + error + "; the default controls are in use";
        gLoadMessage = *message;
        gReplaceBad = true;
        return false;
    }
    migrateBKey(parsed);
    if (!homeReachable(parsed)) {
        const Bindings defaults = Bindings::defaults();
        for (const Binding& input : defaults.inputs(Action::Home)) {
            if (!isPad(input)) parsed.bind(Action::Home, input);
        }
        *message = path.filename().string() + " left this menu without a key; F1 and ` open it again";
        gLoadMessage = *message;
    }
    *bindings = parsed;
    return true;
}

bool save(const Bindings& bindings, std::string* error) {
    std::lock_guard<std::mutex> lock(gFileMutex);
    if (gFile.empty()) {
        *error = "no controls file";
        return false;
    }
    std::error_code ec;
    if (gReplaceBad && std::filesystem::exists(gFile, ec)) {
        std::filesystem::path bad = gFile;
        bad += ".bad";
        std::filesystem::rename(gFile, bad, ec);  // best effort: keep the player's text
    }
    gReplaceBad = false;
    std::filesystem::path temporary = gFile;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::trunc);
        out << "# Petari controls. The Home menu's Controls page rewrites this file.\n"
               "# One Action=Input,Input line per action (native/CONTROLS.md).\n"
            << bindings.serialize();
        if (!out) {
            *error = "cannot write " + temporary.string();
            return false;
        }
    }
    std::filesystem::rename(temporary, gFile, ec);
    if (ec) {
        *error = "cannot replace " + gFile.string() + ": " + ec.message();
        return false;
    }
    gLoadMessage.clear();
    return true;
}

std::filesystem::path file() {
    std::lock_guard<std::mutex> lock(gFileMutex);
    return gFile;
}

std::string loadMessage() {
    std::lock_guard<std::mutex> lock(gFileMutex);
    return gLoadMessage;
}

}  // namespace PetariNative::Input::Remap
