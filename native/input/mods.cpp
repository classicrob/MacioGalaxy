#include "petari/mods.hpp"

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>

#include "petari/host_allocation.hpp"
#include "petari/input.hpp"

namespace PetariNative::Mods {
namespace {

constexpr int kCount = static_cast<int>(Mod::Count);
constexpr const char* kNames[kCount] = {"CollectStarBits", "ShootEnemy", "OdysseyMovement", "Cappy", "OdysseyJumpDive",
                                       "SkipCutscenes", "PointToMove", "GenericCharacter", "GenericWorld", "HideHud", "LevelEditor"};
constexpr const char* kDescriptions[kCount] = {"Collect visible Star Bits", "Fire a Star Bit at the nearest enemy",
                                               "Odyssey movement", "Cappy", "Jump during a ground pound dives",
                                               "Hold Space to skip cutscenes and dialogue", "Point to move (hold left click)",
                                               "Generic character (not Mario)", "Generic world (plain terrain and objects)", "Hide the HUD",
                                               "Level editor (Tab)"};
// The value of a mod with no line in mods.txt (a sub-option of another mod can
// default on: it does nothing while its mod is off).
constexpr bool kDefaults[kCount] = {false, false, false, false, true, false, false, false, false, false, false};
// Input::Action::Count: the mod is a mode with no button of its own.
constexpr Input::Action kActions[kCount] = {Input::Action::ModCollectStarBits, Input::Action::ModShootEnemy,
                                            Input::Action::Count, Input::Action::ModCappyThrow, Input::Action::Count,
                                            Input::Action::Count, Input::Action::Count, Input::Action::Count,
                                            Input::Action::Count, Input::Action::Count,
                                            Input::Action::Count};  // LevelEditor: the host's toggle (editor_input.cpp)

std::atomic<bool> gEnabled[kCount];
std::mutex gFileMutex;
std::filesystem::path gFile;

bool parseInto(const std::string& text, bool (&out)[kCount], std::string* error) {
    std::istringstream in(text);
    std::string line;
    int number = 0;
    while (std::getline(in, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == '#') continue;
        const auto eq = line.find('=');
        const std::string key = line.substr(start, eq == std::string::npos ? std::string::npos : eq - start);
        const std::string value = eq == std::string::npos ? "" : line.substr(eq + 1);
        // "Folder.<ModName>=on|off" belongs to the disc-file mods (platform/mod_folder.hpp).
        if (key.rfind("Folder.", 0) == 0) continue;
        int index = -1;
        for (int i = 0; i < kCount; ++i) {
            if (key == kNames[i]) index = i;
        }
        if (index < 0 || (value != "on" && value != "off")) {
            *error = "line " + std::to_string(number) + ": expected <CollectStarBits|ShootEnemy|OdysseyMovement|Cappy|OdysseyJumpDive|SkipCutscenes|PointToMove|GenericCharacter|GenericWorld|HideHud|LevelEditor>=on|off";
            return false;
        }
        out[index] = value == "on";
    }
    return true;
}

}  // namespace

const char* name(Mod mod) {
    const int i = static_cast<int>(mod);
    return i >= 0 && i < kCount ? kNames[i] : "?";
}

const char* description(Mod mod) {
    const int i = static_cast<int>(mod);
    return i >= 0 && i < kCount ? kDescriptions[i] : "?";
}

bool enabled(Mod mod) {
    const int i = static_cast<int>(mod);
    return i >= 0 && i < kCount && gEnabled[i].load(std::memory_order_relaxed);
}

void setEnabled(Mod mod, bool on) {
    const int i = static_cast<int>(mod);
    if (i >= 0 && i < kCount) gEnabled[i].store(on, std::memory_order_relaxed);
}

std::string serialize() {
    std::string text = "# Native mods (native/MODS.md). on or off.\n";
    for (int i = 0; i < kCount; ++i) {
        text += kNames[i];
        text += gEnabled[i].load(std::memory_order_relaxed) ? "=on\n" : "=off\n";
    }
    return text;
}

bool parse(const std::string& text, std::string* error) {
    bool values[kCount];
    for (int i = 0; i < kCount; ++i) values[i] = gEnabled[i].load(std::memory_order_relaxed);
    if (!parseInto(text, values, error)) return false;
    for (int i = 0; i < kCount; ++i) gEnabled[i].store(values[i], std::memory_order_relaxed);
    return true;
}

bool load(const std::filesystem::path& file, std::string* error) {
    HostAllocationScope scope;
    bool values[kCount];
    for (int i = 0; i < kCount; ++i) values[i] = kDefaults[i];
    std::ifstream in(file);
    if (in) {
        std::stringstream text;
        text << in.rdbuf();
        if (!parseInto(text.str(), values, error)) {
            *error = file.string() + ": " + *error;
            return false;
        }
    }
    if (const char* env = std::getenv("PETARI_MODS"); env != nullptr && env[0] != '\0') {
        std::string list = env;
        for (int i = 0; i < kCount; ++i) values[i] = kDefaults[i];
        std::istringstream names(list);
        std::string item;
        while (std::getline(names, item, ',')) {
            if (item.empty() || item == "none") continue;
            const bool on = item[0] != '-';
            if (!on) item.erase(0, 1);
            int index = -1;
            for (int i = 0; i < kCount; ++i) {
                if (item == kNames[i]) index = i;
            }
            if (index < 0) {
                *error = "PETARI_MODS: unknown mod " + item;
                return false;
            }
            values[index] = on;
        }
    }
    for (int i = 0; i < kCount; ++i) gEnabled[i].store(values[i], std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(gFileMutex);
    gFile = file;
    return true;
}

bool save(std::string* error) {
    HostAllocationScope scope;
    std::lock_guard<std::mutex> lock(gFileMutex);
    if (gFile.empty()) return true;
    // Keep the disc-file mods' "Folder." lines, which this file shares with them.
    std::string folderLines;
    {
        std::ifstream in(gFile);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const auto start = line.find_first_not_of(" \t");
            if (start != std::string::npos && line.compare(start, 7, "Folder.") == 0) folderLines += line.substr(start) + "\n";
        }
    }
    std::ofstream out(gFile, std::ios::trunc);
    out << serialize() << folderLines;
    if (!out) {
        *error = "cannot write " + gFile.string();
        return false;
    }
    return true;
}

void resetForTesting() {
    for (auto& value : gEnabled) value.store(false, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(gFileMutex);
    gFile.clear();
}

}  // namespace PetariNative::Mods

extern "C" bool petari_mod_take_press(int mod) {
    using namespace PetariNative;
    if (mod < 0 || mod >= static_cast<int>(Mods::Mod::Count)) return false;
    if (Mods::kActions[mod] == Input::Action::Count) return false;  // no button
    const int presses = Input::takeActionPresses(Mods::kActions[mod]);
    return presses > 0 && Mods::enabled(static_cast<Mods::Mod>(mod));
}

extern "C" bool petari_mod_held(int mod) {
    using namespace PetariNative;
    if (mod < 0 || mod >= static_cast<int>(Mods::Mod::Count)) return false;
    if (Mods::kActions[mod] == Input::Action::Count) return false;  // no button
    return Mods::enabled(static_cast<Mods::Mod>(mod)) && Input::actionHeld(Mods::kActions[mod]);
}

extern "C" bool petari_mod_enabled(int mod) {
    using namespace PetariNative;
    return mod >= 0 && mod < static_cast<int>(Mods::Mod::Count) && Mods::enabled(static_cast<Mods::Mod>(mod));
}
