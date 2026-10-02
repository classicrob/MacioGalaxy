#pragma once
// Disc formats the level editor reads and writes: Yaz0, RARC archives and BCSV (JMap) tables.
// Pure C++ (no game code): used by the editor runtime, the mod loader and the tests.
// Layouts: TECH.md 1.2 and native/tools/stage_edit.py (the reference implementation, whose
// byte-for-byte rebuild of unedited archives these writers match).
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace PetariEditor::Formats {

using Bytes = std::vector< std::uint8_t >;

// ---- Yaz0 -------------------------------------------------------------------------------------

bool isYaz0(const Bytes& data);
/// The decompressed stream, or nullopt if the header or stream is broken.
std::optional< Bytes > yaz0Decode(const Bytes& data);
/// A valid Yaz0 stream (greedy LZ77 with a 4 KiB window, as stage_edit.py writes).
Bytes yaz0Encode(const Bytes& data);

// ---- RARC -------------------------------------------------------------------------------------

/// A RARC archive whose file contents can be replaced. Folder structure, names, ids and flags
/// are kept as on disc; build() rewrites only file offsets and sizes and the header sizes, so
/// an unedited archive rebuilds byte for byte.
class Archive {
public:
    /// A new archive with folders made from the paths ("jmp/placement/common/objinfo"), in the
    /// layout the game's archives use (files first, then sub-folders, then "." and ".."; files
    /// preloaded to main RAM). For new tables and galaxies, and for tests.
    static Archive create(const std::string& root, const std::vector< std::pair< std::string, Bytes > >& files);
    /// Parses a RARC (Yaz0-compressed or plain). nullopt: not a valid archive.
    static std::optional< Archive > parse(const Bytes& raw);

    bool wasCompressed() const { return mCompressed; }
    const std::string& root() const { return mRoot; }
    /// Lower-case paths below the root folder ("jmp/placement/layera/objinfo"), in entry order.
    std::vector< std::string > files() const;
    bool has(const std::string& path) const;
    /// File contents by path (case-insensitive, with or without the root folder name).
    const Bytes* file(const std::string& path) const;
    bool replace(const std::string& path, Bytes contents);
    /// The plain (uncompressed) RARC.
    Bytes build() const;

private:
    Bytes mData;  // the decompressed original
    bool mCompressed = false;
    std::uint32_t mPayload = 0;      // offset of the data section
    std::uint32_t mEntryOffset = 0;  // absolute offset of the entry table
    std::string mRoot;               // lower-case root folder name
    std::map< std::string, std::uint32_t > mPathToEntry;  // lower-case path -> entry index
    std::map< std::uint32_t, Bytes > mContents;           // entry index -> contents
};

// ---- BCSV ---------------------------------------------------------------------------------------

/// The game's field-name hash (JGadget::getHashCode).
std::uint32_t jmapHash(const char* pName);

enum class ColumnType : std::uint8_t {
    Long = 0,
    InlineString = 1,
    Float = 2,
    Long2 = 3,
    Short = 4,
    Byte = 5,
    StringOffset = 6,
    Null = 7,
};

struct TableColumn {
    std::uint32_t hash = 0;
    std::uint32_t mask = 0xFFFFFFFF;
    std::uint16_t offset = 0;
    std::uint8_t shift = 0;
    ColumnType type = ColumnType::Long;
};

/// A typed cell. Integer columns of every width read as int32 (sign-extended by width, as
/// stage_edit.py and the game's s32 readers see them). Strings are Shift-JIS bytes.
struct CellValue {
    enum class Kind : std::uint8_t { Int, Float, String } kind = Kind::Int;
    std::int32_t i = 0;
    float f = 0.0f;
    std::string s;  // raw Shift-JIS
    bool operator==(const CellValue& other) const {
        return kind == other.kind && i == other.i && (f == other.f || (f != f && other.f != other.f)) && s == other.s;
    }
    bool operator!=(const CellValue& other) const { return !(*this == other); }
};

/// A BCSV table: rows as raw records plus the string pool, so unedited tables rebuild byte
/// for byte (the header bytes are kept while the columns are unchanged).
class Table {
public:
    static std::optional< Table > parse(const Bytes& data);

    const std::vector< TableColumn >& columns() const { return mColumns; }
    int column(std::uint32_t hash) const;  // index, or -1
    int rowCount() const { return static_cast< int >(mRows.size()); }
    std::uint32_t entrySize() const { return mEntrySize; }

    CellValue get(int row, int column) const;
    void set(int row, int column, const CellValue& value);
    /// Appends a column (natural alignment, at the end of the record); every row gets
    /// `initial`. Returns its index. The existing column's index if it is already there.
    int addColumn(std::uint32_t hash, ColumnType type, const CellValue& initial);
    /// Appends a row copied from `copyOf` (or zeroed, strings empty, when -1). Returns its index.
    int addRow(int copyOf = -1);
    void removeRow(int row);
    /// A row's raw record (its strings stay valid in this table's pool, which never shrinks).
    Bytes rowRecord(int row) const { return mRows[row]; }
    /// Puts a record from rowRecord() back at `index`.
    void insertRowRecord(int index, Bytes record) { mRows.insert(mRows.begin() + index, std::move(record)); }
    /// This table's columns with no rows (a new layer's table).
    Table schemaOnly() const {
        Table t = *this;
        t.mRows.clear();
        t.mStrings.clear();
        return t;
    }

    Bytes build() const;
    /// A table with this table's columns and only row `row` (for creating one object live).
    Bytes buildSingleRow(int row) const;

private:
    Bytes mHeader;  // the original header and field records (kept while columns are unchanged)
    bool mColumnsChanged = false;
    std::vector< TableColumn > mColumns;
    std::uint32_t mEntrySize = 0;
    std::vector< Bytes > mRows;
    Bytes mStrings;  // the string pool (without the '@' padding)

    std::uint32_t poolString(const std::string& text);
};

}  // namespace PetariEditor::Formats
