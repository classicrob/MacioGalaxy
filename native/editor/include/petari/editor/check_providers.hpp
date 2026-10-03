#pragma once
// More Checks from other parts of the editor (gravity, paths, terrain): providers run after the
// Checks engine's own, on every recompute (each model revision, and Runtime::invalidateChecks()).
// The first pass of a session is the baseline: what a provider reports for the galaxy as loaded is
// not shown. Keep keys stable (they decide what counts as new).
#include <petari/editor/edit_api.hpp>

#include <vector>

namespace PetariEditor {

using CheckProvider = void (*)(const ModelView& model, std::vector< Check >& out);
/// Register once (at start-up or before the first edit session); registering again is ignored.
void registerCheckProvider(CheckProvider provider);

}  // namespace PetariEditor
