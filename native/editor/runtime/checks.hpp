#pragma once
// The Checks engine (checks.cpp): the problems in a model, errors first.
#include <petari/editor/edit_api.hpp>

#include <vector>

namespace PetariEditor {

/// Problems in the model's rows that appear in at least one mission: unknown classes, required
/// links missing (the catalog), paths that don't exist or have fewer than 2 points, switch ids
/// out of range or unused by the class, switches something waits for but nothing writes,
/// duplicate link ids. Errors first, then by key.
std::vector< Check > computeChecks(const ModelView& model);

}  // namespace PetariEditor
