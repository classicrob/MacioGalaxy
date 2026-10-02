#pragma once
// The engine hooks the level editor needs in game code (TECH.md 2.2, 8.2). Every hook is a
// no-op while the editor is off, so the unmodded game is unchanged.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace PetariEditor::Hooks {

/// The editor is on for this session (Home menu, Mods page, "Level editor"; or
/// PETARI_EDITOR=1 for tests). Read at stage load to reserve headroom.
bool enabled();
void setEnabled(bool on);
/// The Mods-page switch, at each scene start (GameScene::init): on when it is on or
/// PETARI_EDITOR=1. A switch flipped mid-stage takes effect at the next stage load for the
/// headroom; late connect covers the stage in between.
void syncSwitch(bool modSwitchOn);

/// The size to allocate for a list the game sizes to exactly its load-time count
/// (NameObjCategoryList, DrawBufferExecuter): unchanged while the editor is off, otherwise
/// room for the editor's additions (late connect still grows beyond it).
std::uint32_t withHeadroom(std::uint32_t count);

/// Placement rows the editor synthesized outside a loaded archive. The game decides a row's
/// zone by its address (StageDataHolder::findPlacedStageDataHolder), so these ranges answer
/// for their zone. Ranges are dropped when the scene ends.
void registerRows(const void* pBegin, std::size_t size, std::int32_t zoneId);
void unregisterRows(const void* pBegin);
/// The zone of a synthesized row address, or -1.
std::int32_t findSyntheticZone(std::uintptr_t rowAddress);
/// Placement made `pActor` from row `row` of the table whose data starts at `pTableData`
/// (PlacementInfoOrdered::initPlacement, editor on). SceneSync turns these into row ids.
void notePlaced(const void* pTableData, int row, void* pActor);
struct Placed {
    const void* table;
    int row;
    void* actor;
};
/// The placements noted since the scene began (cleared when it ends).
const std::vector< Placed >& placed();

/// Bumps each time a scene ends (SceneSync rebinds after a reload).
std::uint32_t sceneSerial();

/// The scene is being destroyed: forget its rows and placements (their memory goes with the scene).
void sceneDestroyed();

}  // namespace PetariEditor::Hooks
