#pragma once
// Master volume of the game's audio output (Home menu -> Volume slider).
// sound.txt in the user directory: Volume=0..100 in steps of 5, default 100.
// A missing file keeps the default. Nothing changes on error.

#include <algorithm>
#include <filesystem>
#include <string>

namespace PetariNative::Volume {

constexpr int kStep = 5;

// Clamps to 0..100 and rounds to the nearest step.
constexpr int snap(int percent) {
    return (std::clamp(percent, 0, 100) + kStep / 2) / kStep * kStep;
}

bool load(const std::filesystem::path& file, std::string* error);
bool save(std::string* error);  // no-op before load
std::string serialize();
bool parse(const std::string& text, std::string* error);

int percent();  // 0..100, a multiple of kStep
void setPercent(int value);  // snapped
// Sample gain for the current percent: (percent / 100)^2, so equal slider steps
// sound about equally loud. Any thread, including the audio callback.
float gain();

void resetForTesting();

}  // namespace PetariNative::Volume
