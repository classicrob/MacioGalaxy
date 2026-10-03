// The level editor's M1 panels (docs/dev/level-editor/UX.md sections 8 and 16): the Outliner, Changes and
// History tabs, and the raw-field Inspector. They read Runtime's ModelView on the main thread (between
// seams) and edit only through PetariEditor::enqueue. Internal to petari_editor_ui.
#pragma once

#include <petari/editor/catalog.hpp>
#include <petari/editor/edit_api.hpp>

#include <functional>
#include <string>
#include <vector>

namespace PetariEditor::Ui {

// The left dock's window ("Galaxy": Outliner, Browser, Changes, History, Checks) and the Inspector.
void drawGalaxyWindow(const char* title);
void drawInspectorWindow(const char* title);
// Cmd+Z / Cmd+Shift+Z (and the toolbar's buttons): whether there is something to undo / redo.
void deleteSelection();  // Delete: one op removing the selected rows
bool canUndo();
// A mission's name to show: Runtime's (the pause menu's, "Bunnies in the Wind"), else "Mission <n>".
std::string missionLabel(const ModelView& model, int mission);
// ---- sheets (sheets.cpp, UX section 17) ----
// Cmd+S: save the session's mod again, or open the Save sheet the first time (chooseAgain: always the sheet).
void requestSave(bool chooseAgain);
void drawSaveSheet();
const std::string& sessionModName();  // empty until the first save
// A folder-safe mod name (letters, digits, spaces, - and _; trimmed; at most 40 characters).
std::string modNameFrom(const std::string& text);
std::string defaultModName(const char* galaxy);  // "CosmosGardenGalaxy" -> "CosmosGarden edit"

// A case-insensitive "contains" (the Outliner's Find; empty matches all).
bool matchesQuery(const std::string& text, const std::string& query);
// Bring a Galaxy tab forward ("Checks": the toolbar's badge).
void openTab(const char* name);
struct CheckCounts {
    int errors = 0;
    int warnings = 0;
};
CheckCounts countChecks(const std::vector< Check >& all);
// Errors first, then warnings, each in Runtime's order.
std::vector< const Check* > orderChecks(const std::vector< Check >& all);
bool canRedo();
void undo();
void redo();

// Records the last drawn item's rectangle under a name, for testTargetRect (input tests).
void noteTestTarget(const char* name);

// ---- for the tool modules' Inspector sections and viewport tools (M3: paths.cpp, gravity.cpp) ----

// An Inspector section header: title on the left, a dim note on the right ("named", "edits apply to all").
void drawSection(const char* title, const char* right = nullptr);
// Records one undo step (op.label names it in History: "Moved 3 path points").
void applyOp(Op op);
// The Inspector's help strip for this frame: assign or append while an item is hovered.
std::string& helpText();
// A row's name for people: the catalog's display name, else the placement name, else the table's.
std::string displayName(const ModelView& model, RowId row);

// ---- the logic behind the panels, unit-tested without ImGui (native/tests/editor_ui_tests.cpp) ----

bool sameCell(const Cell& a, const Cell& b);
std::string cellText(const Cell& cell);
// A named setting's value in words: the enum's label, "on"/"off", or the number with its unit; -1 is "default"
// unless the enum names it.
std::string argValueText(const Catalog::Arg& arg, std::int32_t value);
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
extern const char* const kPlacePayload;
// A card's name on at most two lines of `width`: split after a space or at a CamelCase boundary, the second
// line ellipsized if it still doesn't fit. measure: a string's width.
std::vector< std::string > cardLabelLines(const std::string& text, float width, const std::function< float(const std::string&) >& measure);
// A thumbnail's drawn part (alpha > 0) as a padded square, in UVs (u0, v0, u1, v1); false if nothing was drawn.
bool thumbnailCrop(const unsigned char* rgba, unsigned width, unsigned height, float uv[4]);  // ImGui drag-and-drop payload: a card dragged onto the viewport

}  // namespace PetariEditor::Ui
