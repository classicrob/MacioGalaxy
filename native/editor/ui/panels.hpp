// The level editor's M1 panels (docs/dev/level-editor/UX.md sections 8 and 16): the Outliner, Changes and
// History tabs, and the raw-field Inspector. They read Runtime's ModelView on the main thread (between
// seams) and edit only through PetariEditor::enqueue. Internal to petari_editor_ui.
#pragma once

#include <petari/editor/edit_api.hpp>

#include <string>
#include <vector>

namespace PetariEditor::Ui {

// The left dock's window ("Galaxy": Outliner, Changes, History) and the Inspector.
void drawGalaxyWindow(const char* title);
void drawInspectorWindow(const char* title);
// Cmd+Z / Cmd+Shift+Z (and the toolbar's buttons): whether there is something to undo / redo.
bool canUndo();
bool canRedo();
void undo();
void redo();

// ---- the logic behind the panels, unit-tested without ImGui (native/tests/editor_ui_tests.cpp) ----

bool sameCell(const Cell& a, const Cell& b);
std::string cellText(const Cell& cell);
// The field's label: its known name, else its hash as 0x%08x.
std::string columnLabel(const Column& column);

// An op setting one column on every row that has it (the Inspector edits the whole selection).
Op setCellOp(const ModelView& model, const std::vector< RowId >& rows, std::uint32_t column, const Cell& value);
// An op putting one column back to its disc value on every row (the gold dot).
Op revertCellOp(const ModelView& model, const std::vector< RowId >& rows, std::uint32_t column);
// An op putting whole rows back as on the disc (the Changes panel's Revert).
Op revertRowsOp(const ModelView& model, const std::vector< RowId >& rows);

struct Change {
    RowId row;
    RowState state = RowState::Original;
    int zone = 0;
    std::string name;                      // the object's name, else the table's
    std::vector< std::string > columns;    // changed fields, by label (Changed rows)
};
// Every row that differs from the disc, in zone and table order.
std::vector< Change > collectChanges(const ModelView& model);

// The selection's rows (from the game's picking), the first one primary.
std::vector< RowId > selectedRows();

// ---- the Browser (browser.cpp, UX section 7) ----

struct BrowserEntry {
    std::string className;    // the placement name, "Kuribo"
    std::string displayName;  // the catalog's, else the class name
    std::string description;
    std::string category;     // "Enemies", ... (from the catalog, else from the table)
    std::vector< std::string > aliases;
    int count = 0;            // placed in this galaxy
    std::vector< RowId > examples;  // some of those placements (the first is what Place copies)
};
// Every class placed in the galaxy, plus the catalog's placeable classes, by display name.
std::vector< BrowserEntry > collectBrowserEntries(const ModelView& model);
// Matching a search and a category (empty: all): names first, then contains; this galaxy's first.
std::vector< const BrowserEntry* > filterEntries(const std::vector< BrowserEntry >& entries, const std::string& query, const std::string& category);
void drawBrowser(const ModelView& model);
// Stamp mode: the example row each click in the viewport places (0: off).
std::uint64_t armedPlacement();
void disarmPlacement();
extern const char* const kPlacePayload;  // ImGui drag-and-drop payload: a card dragged onto the viewport

}  // namespace PetariEditor::Ui
