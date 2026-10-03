#pragma once
#ifdef PETARI_NATIVE
#include "Game/Scene/TestServerEditor.hpp"

namespace TestServerReplay {
bool run(const std::string& name, const std::vector<std::string>& words, bool playable, const TestServerEditor::Reply& reply);
bool controlsActive();
}
#endif
