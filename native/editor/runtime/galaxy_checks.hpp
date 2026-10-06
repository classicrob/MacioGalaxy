#pragma once
// Checks for a mod galaxy (galaxy_checks.cpp): a mission with no Power Star, no start, a start in
// mid-air. Registered once by the runtime; disc galaxies are left alone.

namespace PetariEditor::Runtime {

void registerGalaxyChecks();

}  // namespace PetariEditor::Runtime
