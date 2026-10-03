#pragma once
// Model operations the editor builds Ops from (docs/dev/level-editor/TECH.md 10): pure functions
// over a ModelView, returning one Op to Apply (one undo step). Any thread with a stable model.
#include <petari/editor/edit_api.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace PetariEditor::ModelOps {

/// The missions (bit n-1 = mission n) in which a row placed in `zone`'s `layer` (0 = Common,
/// 1..16 = Layer A..P) appears: the layer active in that mission, and the zone placed in it.
std::uint32_t layerMissions(const ModelView& m, int zone, int layer);

/// The missions a row of `zone` can be given: each layer's missions, and every union of layers
/// whose missions don't overlap (a row is copied into each, sharing its l_id).
std::vector< std::uint32_t > possibleMissionMasks(const ModelView& m, int zone);

struct MissionChange {
    Op op;                       // empty when nothing changes; Apply it as one step
    std::vector< RowId > rows;   // every row the objects are now (for the selection), flattened
    std::string error;           // non-empty: impossible (nothing to apply); says what is possible
};

/// Puts the selected objects in exactly the missions of `missionMask`. An object is a row, or
/// the copies of one object split across layers (same zone, table kind, l_id and cells): it is
/// placed in the one layer with those missions, or split over layers whose missions don't
/// overlap and together make the mask (fewest layers). Copies keep the l_id, so links, paths and
/// switch nets stay; no mission ever gets two copies. An object includes its identical copies in
/// other layers even when they aren't selected (they are the same object). All or nothing: an
/// object that can't have those missions, or a mask of no missions, makes it an error.
MissionChange changeMissions(const ModelView& m, const std::vector< RowId >& rows, std::uint32_t missionMask);

/// A Star ID (Obj_arg0) for a new Power Star in `mission`: the lowest of 1..missionCount that no
/// Power Star in that mission uses (another star's ID makes one of them vanish after a restart);
/// missionCount + 1 when all are taken.
int freeStarId(const ModelView& m, int mission);

}  // namespace PetariEditor::ModelOps
