#pragma once
// The EditModel (TECH.md 1.9): one galaxy's placement data, parsed once from its archives,
// with the operation log that undo, the journal, SceneSync and saving are built on.
// Pure C++ (no game code), so it is tested without the game.
#include <petari/editor/edit_api.hpp>
#include <petari/editor/formats/formats.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace PetariEditor {

/// Reads a disc file by its path ("/StageData/EggStarGalaxy.arc"), through the mod overlay in
/// the game. nullopt: no such file.
using FileSource = std::function< std::optional< Formats::Bytes >(const std::string& discPath) >;

/// The name of a known column hash, or null (generated from the decompilation's literals).
const char* knownColumnName(std::uint32_t hash);

/// Shift-JIS (disc) <-> UTF-8 (editor). Invalid input is kept byte for byte.
std::string sjisToUtf8(const std::string& sjis);
std::string utf8ToSjis(const std::string& utf8);

class EditModel final : public ModelView {
public:
    /// Loads `galaxy` (its Scenario archive and every zone in its ZoneList). nullptr and
    /// `*pError` on failure.
    static std::unique_ptr< EditModel > load(const std::string& galaxy, int scenario, const FileSource& source, std::string* pError);

    // ModelView
    std::uint64_t revision() const override { return mRevision; }
    const char* galaxy() const override { return mGalaxy.c_str(); }
    int scenario() const override { return mScenario; }
    int zoneCount() const override { return static_cast< int >(mZones.size()); }
    const char* zoneName(int zone) const override;
    bool zonePlaced(int zone) const override;
    Mtx34 zoneWorldMatrix(int zone) const override;
    std::uint32_t activeLayers(int zone, int scenario) const override;
    int tableCount(int zone) const override;
    const char* tablePath(int zone, int table) const override;
    const std::vector< Column >& columns(int zone, int table) const override;
    void forEachRow(int zone, int table, const std::function< void(RowId) >& fn) const override;
    bool locate(RowId row, int* pZone, int* pTable) const override;
    RowState state(RowId row) const override;
    const Cell* cell(RowId row, std::uint32_t column) const override;
    const Cell* originalCell(RowId row, std::uint32_t column) const override;
    const char* objectName(RowId row) const override;
    bool hasTransform(RowId row) const override;
    RowTransform transform(RowId row) const override;
    int historySize() const override { return static_cast< int >(mHistory.size()); }
    int historyCursor() const override { return mCursor; }
    const char* historyLabel(int index) const override;

    // editing (the seam)
    /// Applies `op` as one undo step (drops any redo tail). False and `*pError`: nothing changed.
    bool apply(const Op& op, std::string* pError);
    bool undo();
    bool redo();
    /// Undoes or redoes until historyCursor() == cursor.
    bool jumpTo(int cursor);
    /// Rows whose cells changed since the last call (SceneSync re-creates their actors).
    std::set< RowId > takeChangedRows();
    /// Edits since the last markSaved().
    bool unsaved() const { return serialAtCursor() != mSavedSerial; }
    void markSaved();

    // output
    /// Every zone archive that differs from the disc: (disc path, plain RARC bytes).
    std::vector< std::pair< std::string, Formats::Bytes > > changedArchives() const;
    /// The current table a row lives in, and its row index (SceneSync synthesizes rows from it).
    const Formats::Table* rowTable(RowId row, int* pRowIndex) const;
    /// The game's zone id of a zone (its ZoneList index).
    std::int32_t zoneId(int zone) const;

private:
    struct TableModel {
        std::string path;  // lower case, below the archive root
        std::int8_t layer = -1;  // 0 = Common, 1..16 = LayerA..P, -1 = not layered
        Formats::Table original;
        Formats::Table current;
        std::vector< Column > columns;
        std::vector< std::vector< Cell > > cells;     // current, decoded [row][column]
        std::vector< std::vector< Cell > > origCells;  // as on disc [row][column in original]
    };
    struct ZoneModel {
        std::string name;
        std::int32_t zoneId = 0;
        Formats::Archive archive;
        std::vector< TableModel > tables;
        bool placed = false;
        Mtx34 world{};
    };
    struct Change {
        RowId row;
        std::uint32_t column = 0;
        std::optional< Cell > before;  // nullopt: the column was added by this change
        Cell after;
    };
    struct Entry {
        std::uint64_t serial = 0;
        std::string label;
        std::vector< Change > changes;
    };

    EditModel() = default;
    static RowId makeId(int zone, int table, int row);
    bool unpack(RowId id, int* pZone, int* pTable, int* pRow) const;
    void writeCell(int zone, int table, int row, std::uint32_t column, const Cell& value);
    void computePlacement();
    std::uint64_t serialAtCursor() const { return mCursor > 0 ? mHistory[mCursor - 1].serial : 0; }

    std::string mGalaxy;
    int mScenario = 1;
    Formats::Table mScenarioData;
    std::vector< ZoneModel > mZones;
    std::vector< Entry > mHistory;
    int mCursor = 0;
    std::uint64_t mSavedSerial = 0;  // serialAtCursor() when last saved
    std::uint64_t mNextSerial = 1;
    std::uint64_t mRevision = 1;
    std::set< RowId > mChanged;
    mutable std::string mNameScratch;
};

}  // namespace PetariEditor
