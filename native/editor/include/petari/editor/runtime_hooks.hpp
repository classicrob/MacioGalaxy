#pragma once
// The engine hooks the level editor needs in game code (TECH.md 2.2, 8.2). Every hook is a
// no-op while the editor is off, so the unmodded game is unchanged.
#include <cstddef>
#include <cstdint>

namespace PetariEditor::Hooks {

/// The editor is on for this session (Home menu, Mods page, "Level editor"; or
/// PETARI_EDITOR=1 for tests). Read at stage load to reserve headroom.
bool enabled();
void setEnabled(bool on);

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
/// The scene is being destroyed: forget its rows (their memory goes with the scene).
void sceneDestroyed();

}  // namespace PetariEditor::Hooks
