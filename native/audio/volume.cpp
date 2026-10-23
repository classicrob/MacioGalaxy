#include <petari/volume.hpp>

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>

namespace PetariNative::Volume {
namespace {

std::atomic<int> gPercent{100};
std::atomic<float> gGain{1.0f};
std::mutex gStoreLock;
std::filesystem::path gStore;  // under gStoreLock; empty before load

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

}  // namespace

int percent() { return gPercent.load(std::memory_order_relaxed); }

void setPercent(int value) {
    const int snapped = snap(value);
    const float level = static_cast<float>(snapped) / 100.0f;
    gPercent.store(snapped, std::memory_order_relaxed);
    gGain.store(level * level, std::memory_order_relaxed);
}

float gain() { return gGain.load(std::memory_order_relaxed); }

std::string serialize() {
    std::ostringstream out;
    out << "# Petari sound settings\n"
        << "Volume=" << percent() << "\n";
    return out.str();
}

bool parse(const std::string& text, std::string* error) {
    std::istringstream in(text);
    std::string line;
    int value = percent();
    int number = 0;
    while (std::getline(in, line)) {
        number++;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const auto equals = line.find('=');
        const std::string key = equals == std::string::npos ? line : trim(line.substr(0, equals));
        if (key != "Volume") continue;  // unknown lines are ignored
        const std::string raw = equals == std::string::npos ? std::string() : trim(line.substr(equals + 1));
        char* end = nullptr;
        const long parsed = std::strtol(raw.c_str(), &end, 10);
        if (raw.empty() || end == nullptr || *end != '\0' || parsed < 0 || parsed > 100) {
            if (error) *error = "line " + std::to_string(number) + ": Volume must be 0..100";
            return false;
        }
        value = static_cast<int>(parsed);
    }
    setPercent(value);
    return true;
}

bool load(const std::filesystem::path& file, std::string* error) {
    {
        std::lock_guard<std::mutex> guard(gStoreLock);
        gStore = file;
    }
    std::ifstream in(file);
    if (!in) return true;  // no file: defaults
    std::ostringstream text;
    text << in.rdbuf();
    if (!parse(text.str(), error)) {
        if (error) *error = file.string() + ": " + *error;
        return false;
    }
    return true;
}

bool save(std::string* error) {
    std::lock_guard<std::mutex> guard(gStoreLock);
    if (gStore.empty()) return true;
    const std::filesystem::path temporary = gStore.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::trunc);
        out << serialize();
        if (!out) {
            if (error) *error = "cannot write " + temporary.string();
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temporary, gStore, ec);
    if (ec) {
        if (error) *error = "cannot replace " + gStore.string() + ": " + ec.message();
        return false;
    }
    return true;
}

void resetForTesting() {
    std::lock_guard<std::mutex> guard(gStoreLock);
    gStore.clear();
    setPercent(100);
}

}  // namespace PetariNative::Volume
