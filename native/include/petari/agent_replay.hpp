#pragma once
#include <string>

namespace PetariNative::App::AgentReplay {
bool start(const std::string& route, const std::string& stage, int scenario,
           unsigned long frames, bool requireStar, std::string* error);
bool active();
bool finished();
bool passed();
std::string reason();
unsigned long frame();
void cancel();
}
