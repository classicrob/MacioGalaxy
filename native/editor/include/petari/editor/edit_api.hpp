#pragma once
// The level editor's contract between the UI (native/editor/ui: panels, camera, gizmo,
// picking) and the Runtime (native/editor/runtime: EditModel, ops and undo, SceneSync,
// reload, save). Design: docs/dev/level-editor/TECH.md section 2.
//
// Threading (TECH.md 2.10). The game, the frame seam and the ImGui overlay all run on the
// macOS main thread, in this order each frame:
//   seam (CPU baton held):  Runtime drains commands -> EditModel + SceneSync -> the UI seam hook
//   overlay (baton released; audio/loader threads may run game code): UI panels
// Consequences:
// - The EditModel is editor-only data, mutated only in the drain, so the UI may read it
//   (model()) from the overlay without locks.
// - Game objects (actors) are touched only with the baton held: by Runtime, and by the UI
//   in its seam hook (picking, ground rays, gizmo frames), which publishes plain data for
//   its panels.
// - enqueue() may be called any time on the main thread; commands take effect at the next seam.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class LiveActor;

namespace PetariEditor {

// ---- identity ---------------------------------------------------------------------------------

/// A row's stable editor id (TECH.md 1.9): survives undo, reload and re-create, and is the key
/// the patch format uses. Opaque to the UI. Not the game's l_id.
struct RowId {
    std::uint64_t v = 0;
    bool valid() const { return v != 0; }
    friend bool operator==(RowId a, RowId b) { return a.v == b.v; }
    friend bool operator!=(RowId a, RowId b) { return a.v != b.v; }
    friend bool operator<(RowId a, RowId b) { return a.v < b.v; }
};
struct RowIdHash {
    std::size_t operator()(RowId r) const { return std::hash< std::uint64_t >()(r.v); }
};

// ---- cells ------------------------------------------------------------------------------------

/// BCSV cell types (include/Game/Util/JMapInfo.hpp). Int is types 0 and 3; String is the
/// inline 32-byte string (1); StringOffset is the string pool (6).
enum class CellType : std::uint8_t { Int, Float, String, Short, Byte, StringOffset };

struct Cell {
    CellType type = CellType::Int;
    std::int32_t i = 0;  // Int, Short, Byte (as the game reads them, sign-extended)
    float f = 0.0f;      // Float
    std::string s;       // String, StringOffset (UTF-8 here, Shift-JIS on disc)
};

struct Column {
    std::uint32_t hash = 0;
    const char* name = nullptr;  // known name ("pos_x", "Obj_arg0") or null: show it as 0x%08x
    CellType type = CellType::Int;
};

/// The game's field-name hash (JGadget::getHashCode: h = c + 31*h over signed chars).
std::uint32_t columnHash(const char* pName);

enum class RowState : std::uint8_t { Original, Changed, Added, Deleted };

struct Vec3 {
    float x = 0, y = 0, z = 0;
};
/// pos_*, dir_* (degrees) and scale_* of a row, in the row's own (zone) space.
struct RowTransform {
    Vec3 pos, dirDeg, scale{1, 1, 1};
};
/// Row columns are a 3x4 row-major affine matrix (the game's TPos3f layout).
struct Mtx34 {
    float m[3][4];
};

// ---- the model (read side; main thread, outside the drain) --------------------------------------

class ModelView {
public:
    virtual ~ModelView() = default;
    /// Bumps on every applied op, undo, redo and reload.
    virtual std::uint64_t revision() const = 0;

    virtual const char* galaxy() const = 0;  // "EggStarGalaxy"
    virtual int scenario() const = 0;        // the mission being edited, 1-based

    // zones and tables
    virtual int zoneCount() const = 0;
    virtual const char* zoneName(int zone) const = 0;  // "SoramameZone" (0 = the galaxy's own archive)
    virtual bool zonePlaced(int zone) const = 0;       // placed in the current scenario
    virtual Mtx34 zoneWorldMatrix(int zone) const = 0;  // world from zone (identity for zone 0)
    /// Layers of `zone` active in `scenario` (bit 0 = Common, always set; bit n = Layer A + n - 1).
    virtual std::uint32_t activeLayers(int zone, int scenario) const = 0;
    virtual int tableCount(int zone) const = 0;
    virtual const char* tablePath(int zone, int table) const = 0;  // "jmp/placement/layera/objinfo"
    virtual const std::vector< Column >& columns(int zone, int table) const = 0;
    /// Every row, in table order, including Added and Deleted ones.
    virtual void forEachRow(int zone, int table, const std::function< void(RowId) >& fn) const = 0;

    // rows
    virtual bool locate(RowId row, int* pZone, int* pTable) const = 0;
    virtual RowState state(RowId row) const = 0;
    virtual const Cell* cell(RowId row, std::uint32_t column) const = 0;          // current; null: no such column
    virtual const Cell* originalCell(RowId row, std::uint32_t column) const = 0;  // disc value; null for Added rows
    virtual const char* objectName(RowId row) const = 0;                           // `type`, else `name`; null if none
    virtual bool hasTransform(RowId row) const = 0;
    virtual RowTransform transform(RowId row) const = 0;

    // history (Edit menu, History list)
    virtual int historySize() const = 0;    // ops recorded
    virtual int historyCursor() const = 0;  // ops applied: undo = cursor > 0, redo = cursor < size
    virtual const char* historyLabel(int index) const = 0;
};

/// null until a galaxy is loaded in an edit session.
const ModelView* model();

// ---- ops (built by the UI, applied by Runtime at the seam) -----------------------------------------

struct SetCell {
    RowId row;
    std::uint32_t column = 0;  // columnHash(); a column the table lacks is added to it
    Cell after;                // Runtime records the "before" value itself
};

/// One undo step. M1 needs cell sets and reverts; adding and deleting rows join in M2
/// (new members, so this stays source-compatible).
struct Op {
    std::string label;              // "Move Star Bunny", "Set Obj_arg1 on 3 objects", "Revert Coin"
    std::vector< SetCell > sets;
    std::vector< RowId > revertRows;  // every cell of these rows back to the disc value
};

// ---- commands (UI -> Runtime, drained in order at the seam) -----------------------------------------

enum class PlayFrom : std::uint8_t {
    Marker,        // the play marker
    Resume,        // where play was (no teleport)
    MissionStart,  // the mission's start point (a reload)
};

struct Command {
    enum class Kind : std::uint8_t {
        Apply,           // op -> undo stack, journal, SceneSync
        Undo,
        Redo,
        JumpHistory,     // index -> undo or redo until historyCursor() == index
        Preview,         // drag preview: poke the live actors of `rows` to `previews`; not recorded
        EndPreview,      // drop the preview pokes of `rows` (a cancelled drag); Apply also ends them
        Play,            // from
        Reload,          // rebuild the scene from the model; stay in Edit
        SetPlayMarker,   // markerPos (+ markerUp, markerFront)
        SaveMod,         // modName; M1 writes Full archives (TECH.md 5.1)
        SwitchScenario,  // index = the mission to edit (a reload)
    } kind;
    Op op;
    int index = 0;
    std::vector< RowId > rows;
    std::vector< RowTransform > previews;  // parallel to rows, in row (zone) space
    PlayFrom from = PlayFrom::Marker;
    Vec3 markerPos, markerUp, markerFront;
    std::string modName;
};

void enqueue(Command&& command);

// ---- seam services for the UI (main thread, baton held) ------------------------------------------

/// Called at the seam after the drain and SceneSync, before the baton is released.
void setUiSeamHook(void (*hook)());
/// The actor a row created in the current scene (null: none, dead or pending). Rebuilt after
/// re-create and reload. Valid only with the baton held.
LiveActor* actorForRow(RowId row);
RowId rowForActor(const LiveActor* pActor);
void forEachBoundRow(const std::function< void(RowId, LiveActor*) >& fn);

// ---- session (native state; survives scene reloads) --------------------------------------------

enum class Mode : std::uint8_t { Off, Editing, Reloading, Playing };
Mode mode();

/// The UI owns entering and leaving the Edit nerve (Tab), the selection and the editor
/// camera. Runtime owns Play and Reload:
/// - Play: if dirty, Mode = Reloading and reload; then leaveEdit(), teleport Mario, Mode = Playing.
///   Not dirty: leaveEdit() and teleport at once.
/// - Reload in Edit: Mode = Reloading; when the new scene is up, resumeEdit() (the UI re-enters
///   the nerve on the first permitted frame and restores camera and selection).
/// - sceneRebuilt(): actor pointers are invalid; drop caches.
struct UiCallbacks {
    void (*leaveEdit)() = nullptr;
    void (*resumeEdit)() = nullptr;
    void (*sceneRebuilt)() = nullptr;
};
void setUiCallbacks(const UiCallbacks& callbacks);

/// The UI tells Runtime when it enters or leaves Edit (Mode Off <-> Editing).
void notifyEditEntered();
void notifyEditLeft();

// ---- status for the toolbar, footer and Checks ------------------------------------------------------

struct Capacity {
    const char* name = "";  // "Gravity fields"
    int used = 0;
    int limit = 0;
};

struct Status {
    bool dirty = false;         // edits not yet in the scene that need a reload before play
    int pendingCount = 0;       // "Reload · N pending"
    bool unsaved = false;       // edits since the last SaveMod
    bool playedState = false;   // played since the last reload (the Played-state chip)
    std::vector< std::string > switchesOn;  // after a play-test, "Zone 3 · switch 13"
    std::uint32_t heapFree = 0;  // scene heap, bytes (the memory meter)
    std::uint32_t heapTotal = 0;
    std::vector< Capacity > capacities;
    std::string lastError;  // the last failed command, for a toast; cleared by the next success
    std::uint64_t errorSerial = 0;  // bumps with each new error
    std::string notice;             // the last success worth a toast ("Saved 2 archives to mods/MyMod")
    std::uint64_t noticeSerial = 0;  // bumps with each new notice
};
/// Updated at each seam.
const Status& status();

}  // namespace PetariEditor
