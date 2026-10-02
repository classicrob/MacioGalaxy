// The EditModel (edit_model.hpp; TECH.md 1.9 and 2.8).
#include "edit_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iconv.h>
#include <unordered_map>

namespace PetariEditor {
namespace {
    using Formats::Bytes;
    using Formats::CellValue;
    using Formats::ColumnType;

    const char* const kFieldNames[] = {
#include "field_names.inc"
    };

    const std::unordered_map< std::uint32_t, const char* >& nameTable() {
        static const std::unordered_map< std::uint32_t, const char* > sTable = [] {
            std::unordered_map< std::uint32_t, const char* > table;
            for (const char* pName : kFieldNames) {
                table.emplace(Formats::jmapHash(pName), pName);  // the generator already resolved collisions
            }
            return table;
        }();
        return sTable;
    }

    std::string convert(const std::string& text, const char* pFrom, const char* pTo) {
        bool ascii = std::all_of(text.begin(), text.end(), [](char c) { return static_cast< unsigned char >(c) < 0x80; });
        if (ascii) {
            return text;
        }
        iconv_t cd = iconv_open(pTo, pFrom);
        if (cd == reinterpret_cast< iconv_t >(-1)) {
            return text;
        }
        std::string out(text.size() * 4 + 4, '\0');
        char* pIn = const_cast< char* >(text.data());
        size_t inLeft = text.size();
        char* pOut = out.data();
        size_t outLeft = out.size();
        const size_t result = iconv(cd, &pIn, &inLeft, &pOut, &outLeft);
        iconv_close(cd);
        if (result == static_cast< size_t >(-1)) {
            return text;
        }
        out.resize(out.size() - outLeft);
        return out;
    }

    CellType cellTypeOf(ColumnType type) {
        switch (type) {
        case ColumnType::Float:
            return CellType::Float;
        case ColumnType::InlineString:
            return CellType::String;
        case ColumnType::StringOffset:
            return CellType::StringOffset;
        case ColumnType::Short:
            return CellType::Short;
        case ColumnType::Byte:
            return CellType::Byte;
        default:
            return CellType::Int;
        }
    }

    ColumnType columnTypeOf(CellType type) {
        switch (type) {
        case CellType::Float:
            return ColumnType::Float;
        case CellType::String:
            return ColumnType::InlineString;
        case CellType::StringOffset:
            return ColumnType::StringOffset;
        case CellType::Short:
            return ColumnType::Short;
        case CellType::Byte:
            return ColumnType::Byte;
        case CellType::Int:
            return ColumnType::Long;
        }
        return ColumnType::Long;
    }

    Cell toCell(const CellValue& value, ColumnType type) {
        Cell cell;
        cell.type = cellTypeOf(type);
        cell.i = value.i;
        cell.f = value.f;
        cell.s = sjisToUtf8(value.s);
        return cell;
    }

    CellValue toValue(const Cell& cell) {
        CellValue value;
        switch (cell.type) {
        case CellType::Float:
            value.kind = CellValue::Kind::Float;
            value.f = cell.f;
            break;
        case CellType::String:
        case CellType::StringOffset:
            value.kind = CellValue::Kind::String;
            value.s = utf8ToSjis(cell.s);
            break;
        default:
            value.kind = CellValue::Kind::Int;
            value.i = cell.i;
            break;
        }
        return value;
    }

    bool sameCell(const Cell& a, const Cell& b) {
        switch (a.type) {
        case CellType::Float:
            return b.type == CellType::Float && (a.f == b.f || (std::isnan(a.f) && std::isnan(b.f)));
        case CellType::String:
        case CellType::StringOffset:
            return a.s == b.s;
        default:
            return a.i == b.i;
        }
    }

    bool isNeutral(const Cell& cell) {
        switch (cell.type) {
        case CellType::Float:
            return cell.f == 0.0f;
        case CellType::String:
        case CellType::StringOffset:
            return cell.s.empty();
        default:
            return cell.i == -1;
        }
    }

    std::int8_t layerOf(const std::string& path) {
        // jmp/<dir>/<layer>/<table>
        const size_t a = path.find('/'), b = path.find('/', a + 1), c = path.find('/', b + 1);
        if (a == std::string::npos || b == std::string::npos || c == std::string::npos) {
            return -1;
        }
        const std::string layer = path.substr(b + 1, c - b - 1);
        if (layer == "common") {
            return 0;
        }
        if (layer.size() == 6 && layer.compare(0, 5, "layer") == 0 && layer[5] >= 'a' && layer[5] <= 'p') {
            return static_cast< std::int8_t >(layer[5] - 'a' + 1);
        }
        return -1;
    }

    // MR::makeMtxTR: R = Rz * Ry * Rx (degrees), then the translation.
    Mtx34 makeMtxTR(const Vec3& pos, const Vec3& dirDeg) {
        const float k = 3.14159265358979f / 180.0f;
        const float sx = std::sin(dirDeg.x * k), cx = std::cos(dirDeg.x * k);
        const float sy = std::sin(dirDeg.y * k), cy = std::cos(dirDeg.y * k);
        const float sz = std::sin(dirDeg.z * k), cz = std::cos(dirDeg.z * k);
        Mtx34 m{};
        m.m[0][0] = cy * cz;
        m.m[0][1] = cz * sy * sx - sz * cx;
        m.m[0][2] = cz * sy * cx + sz * sx;
        m.m[1][0] = cy * sz;
        m.m[1][1] = sz * sy * sx + cz * cx;
        m.m[1][2] = sz * sy * cx - cz * sx;
        m.m[2][0] = -sy;
        m.m[2][1] = cy * sx;
        m.m[2][2] = cy * cx;
        m.m[0][3] = pos.x;
        m.m[1][3] = pos.y;
        m.m[2][3] = pos.z;
        return m;
    }

    Mtx34 identity() {
        Mtx34 m{};
        m.m[0][0] = m.m[1][1] = m.m[2][2] = 1.0f;
        return m;
    }

    std::string stringCell(const Formats::Table& table, int row, const char* pColumn) {
        const int column = table.column(Formats::jmapHash(pColumn));
        return column < 0 ? std::string() : table.get(row, column).s;
    }

    int intCell(const Formats::Table& table, int row, const char* pColumn, int fallback) {
        const int column = table.column(Formats::jmapHash(pColumn));
        return column < 0 ? fallback : table.get(row, column).i;
    }

    float floatCell(const Formats::Table& table, int row, const char* pColumn, float fallback) {
        const int column = table.column(Formats::jmapHash(pColumn));
        return column < 0 ? fallback : table.get(row, column).f;
    }

    // Row ids: (zone + 1) << 40 | (table + 1) << 20 | (row + 1). Deterministic across sessions
    // (tables are kept in archive entry order), so the journal can name rows. Limits: 2^20 rows
    // and tables, far above the game's.
    constexpr std::uint64_t kField = (1u << 20) - 1;
}  // namespace

const char* knownColumnName(std::uint32_t hash) {
    auto it = nameTable().find(hash);
    return it == nameTable().end() ? nullptr : it->second;
}

std::string sjisToUtf8(const std::string& sjis) {
    return convert(sjis, "CP932", "UTF-8");
}

std::string utf8ToSjis(const std::string& utf8) {
    return convert(utf8, "UTF-8", "CP932");
}

// ---- loading ------------------------------------------------------------------------------------

std::unique_ptr< EditModel > EditModel::load(const std::string& galaxy, int scenario, const FileSource& source, std::string* pError) {
    auto fail = [&](const std::string& message) -> std::unique_ptr< EditModel > {
        if (pError != nullptr) {
            *pError = message;
        }
        return nullptr;
    };
    std::unique_ptr< EditModel > model(new EditModel());
    model->mGalaxy = galaxy;
    model->mScenario = scenario;

    const std::string scenarioPath = "/StageData/" + galaxy + "/" + galaxy + "Scenario.arc";
    std::optional< Bytes > scenarioRaw = source(scenarioPath);
    if (!scenarioRaw) {
        return fail("no " + scenarioPath);
    }
    std::optional< Formats::Archive > scenarioArchive = Formats::Archive::parse(*scenarioRaw);
    const Bytes* scenarioData = scenarioArchive ? scenarioArchive->file("scenariodata.bcsv") : nullptr;
    const Bytes* zoneList = scenarioArchive ? scenarioArchive->file("zonelist.bcsv") : nullptr;
    std::optional< Formats::Table > scenarios = scenarioData ? Formats::Table::parse(*scenarioData) : std::nullopt;
    std::optional< Formats::Table > zones = zoneList ? Formats::Table::parse(*zoneList) : std::nullopt;
    if (!scenarios || !zones) {
        return fail(scenarioPath + " has no readable ScenarioData or ZoneList");
    }
    model->mScenarioData = std::move(*scenarios);

    for (int z = 0; z < zones->rowCount(); z++) {
        ZoneModel zone;
        zone.name = stringCell(*zones, z, "ZoneName");
        zone.zoneId = z;
        const std::string path = "/StageData/" + zone.name + ".arc";
        std::optional< Bytes > raw = source(path);
        std::optional< Formats::Archive > archive = raw ? Formats::Archive::parse(*raw) : std::nullopt;
        if (!archive) {
            return fail("cannot read " + path);
        }
        zone.archive = std::move(*archive);
        for (const std::string& file : zone.archive.files()) {
            if (file.rfind("jmp/", 0) != 0) {
                continue;
            }
            std::optional< Formats::Table > table = Formats::Table::parse(*zone.archive.file(file));
            if (!table) {
                continue;  // not a table; kept byte for byte in the archive
            }
            TableModel model;
            model.path = file;
            model.layer = layerOf(file);
            model.original = *table;
            model.current = std::move(*table);
            for (const Formats::TableColumn& c : model.current.columns()) {
                model.columns.push_back(Column{c.hash, knownColumnName(c.hash), cellTypeOf(c.type)});
            }
            for (int r = 0; r < model.current.rowCount(); r++) {
                std::vector< Cell > row;
                for (int c = 0; c < static_cast< int >(model.columns.size()); c++) {
                    row.push_back(toCell(model.current.get(r, c), model.current.columns()[c].type));
                }
                model.cells.push_back(row);
                model.origCells.push_back(std::move(row));
            }
            zone.tables.push_back(std::move(model));
        }
        model->mZones.push_back(std::move(zone));
    }
    if (model->mZones.empty()) {
        return fail(galaxy + " has an empty ZoneList");
    }
    model->computePlacement();
    return model;
}

void EditModel::computePlacement() {
    // Zone 0 is the galaxy itself; other zones are placed by StageObjInfo rows of zone 0 in its
    // active layers (StageDataHolder::createLocalStageDataHolder, one level deep).
    for (ZoneModel& zone : mZones) {
        zone.placed = false;
        zone.world = identity();
    }
    mZones[0].placed = true;
    const std::uint32_t layers = activeLayers(0, mScenario);
    for (const TableModel& table : mZones[0].tables) {
        if (table.layer < 0 || !(layers & (1u << table.layer)) || table.path.size() < 12 ||
            table.path.compare(table.path.size() - 12, 12, "stageobjinfo") != 0) {
            continue;
        }
        for (int r = 0; r < table.current.rowCount(); r++) {
            const std::string name = stringCell(table.current, r, "name");
            for (ZoneModel& zone : mZones) {
                if (&zone != &mZones[0] && zone.name == name) {
                    zone.placed = true;
                    zone.world = makeMtxTR({floatCell(table.current, r, "pos_x", 0), floatCell(table.current, r, "pos_y", 0),
                                            floatCell(table.current, r, "pos_z", 0)},
                                           {floatCell(table.current, r, "dir_x", 0), floatCell(table.current, r, "dir_y", 0),
                                            floatCell(table.current, r, "dir_z", 0)});
                }
            }
        }
    }
}

// ---- ModelView ------------------------------------------------------------------------------------

RowId EditModel::makeId(int zone, int table, int row) {
    return RowId{static_cast< std::uint64_t >(zone + 1) << 40 | static_cast< std::uint64_t >(table + 1) << 20 |
                 static_cast< std::uint64_t >(row + 1)};
}

bool EditModel::unpack(RowId id, int* pZone, int* pTable, int* pRow) const {
    const int zone = static_cast< int >(id.v >> 40) - 1;
    const int table = static_cast< int >((id.v >> 20) & kField) - 1;
    const int row = static_cast< int >(id.v & kField) - 1;
    if (zone < 0 || zone >= zoneCount() || table < 0 || table >= tableCount(zone) || row < 0 ||
        row >= mZones[zone].tables[table].current.rowCount()) {
        return false;
    }
    *pZone = zone;
    *pTable = table;
    *pRow = row;
    return true;
}

const char* EditModel::zoneName(int zone) const {
    return zone >= 0 && zone < zoneCount() ? mZones[zone].name.c_str() : "";
}

bool EditModel::zonePlaced(int zone) const {
    return zone >= 0 && zone < zoneCount() && mZones[zone].placed;
}

Mtx34 EditModel::zoneWorldMatrix(int zone) const {
    return zone >= 0 && zone < zoneCount() ? mZones[zone].world : identity();
}

std::int32_t EditModel::zoneId(int zone) const {
    return zone >= 0 && zone < zoneCount() ? mZones[zone].zoneId : -1;
}

std::uint32_t EditModel::activeLayers(int zone, int scenario) const {
    if (zone < 0 || zone >= zoneCount()) {
        return 1;
    }
    const int column = mScenarioData.column(Formats::jmapHash(mZones[zone].name.c_str()));
    if (column < 0) {
        return 1;
    }
    for (int r = 0; r < mScenarioData.rowCount(); r++) {
        if (intCell(mScenarioData, r, "ScenarioNo", -1) == scenario) {
            // ScenarioDataParser::getCurrentScenarioLayers: the column's bits shifted past Common.
            return static_cast< std::uint32_t >(mScenarioData.get(r, column).i) << 1 | 1u;
        }
    }
    return 1;
}

int EditModel::tableCount(int zone) const {
    return zone >= 0 && zone < zoneCount() ? static_cast< int >(mZones[zone].tables.size()) : 0;
}

const char* EditModel::tablePath(int zone, int table) const {
    return table >= 0 && table < tableCount(zone) ? mZones[zone].tables[table].path.c_str() : "";
}

const std::vector< Column >& EditModel::columns(int zone, int table) const {
    static const std::vector< Column > sNone;
    return table >= 0 && table < tableCount(zone) ? mZones[zone].tables[table].columns : sNone;
}

void EditModel::forEachRow(int zone, int table, const std::function< void(RowId) >& fn) const {
    if (table < 0 || table >= tableCount(zone)) {
        return;
    }
    for (int r = 0; r < mZones[zone].tables[table].current.rowCount(); r++) {
        fn(makeId(zone, table, r));
    }
}

bool EditModel::locate(RowId row, int* pZone, int* pTable) const {
    int r;
    return unpack(row, pZone, pTable, &r);
}

RowState EditModel::state(RowId id) const {
    int zone, table, row;
    if (!unpack(id, &zone, &table, &row)) {
        return RowState::Original;
    }
    const TableModel& t = mZones[zone].tables[table];
    const std::vector< Cell >& now = t.cells[row];
    for (size_t c = 0; c < now.size(); c++) {
        const int original = t.original.column(t.columns[c].hash);
        if (original >= 0) {
            if (!sameCell(now[c], t.origCells[row][original])) {
                return RowState::Changed;
            }
        } else if (!isNeutral(now[c])) {
            // An added column at its neutral value reads as the missing column did (-1 by *WithInit).
            return RowState::Changed;
        }
    }
    return RowState::Original;
}

const Cell* EditModel::cell(RowId id, std::uint32_t column) const {
    int zone, table, row;
    if (!unpack(id, &zone, &table, &row)) {
        return nullptr;
    }
    const TableModel& t = mZones[zone].tables[table];
    const int c = t.current.column(column);
    return c < 0 ? nullptr : &t.cells[row][c];
}

const Cell* EditModel::originalCell(RowId id, std::uint32_t column) const {
    int zone, table, row;
    if (!unpack(id, &zone, &table, &row)) {
        return nullptr;
    }
    const TableModel& t = mZones[zone].tables[table];
    const int c = t.original.column(column);
    return c < 0 ? nullptr : &t.origCells[row][c];
}

const char* EditModel::objectName(RowId id) const {
    const Cell* pType = cell(id, Formats::jmapHash("type"));
    if (pType != nullptr && !pType->s.empty()) {
        return pType->s.c_str();
    }
    const Cell* pName = cell(id, Formats::jmapHash("name"));
    return pName != nullptr ? pName->s.c_str() : nullptr;
}

bool EditModel::hasTransform(RowId id) const {
    return cell(id, Formats::jmapHash("pos_x")) != nullptr;
}

RowTransform EditModel::transform(RowId id) const {
    auto get = [&](const char* pName, float fallback) {
        const Cell* pCell = cell(id, Formats::jmapHash(pName));
        return pCell != nullptr ? (pCell->type == CellType::Float ? pCell->f : static_cast< float >(pCell->i)) : fallback;
    };
    RowTransform t;
    t.pos = {get("pos_x", 0), get("pos_y", 0), get("pos_z", 0)};
    t.dirDeg = {get("dir_x", 0), get("dir_y", 0), get("dir_z", 0)};
    t.scale = {get("scale_x", 1), get("scale_y", 1), get("scale_z", 1)};
    return t;
}

const char* EditModel::historyLabel(int index) const {
    return index >= 0 && index < historySize() ? mHistory[index].label.c_str() : "";
}

// ---- editing ----------------------------------------------------------------------------------

void EditModel::writeCell(int zone, int table, int row, std::uint32_t column, const Cell& value) {
    TableModel& t = mZones[zone].tables[table];
    int c = t.current.column(column);
    if (c < 0) {
        // A column the table lacks (TECH.md 5.3 addColumn): appended with its type's neutral value
        // (-1 for integers, as the game's *WithInit readers default).
        const ColumnType type = columnTypeOf(value.type);
        CellValue initial;
        initial.kind = type == ColumnType::Float ? CellValue::Kind::Float
                       : (type == ColumnType::StringOffset || type == ColumnType::InlineString) ? CellValue::Kind::String
                                                                                              : CellValue::Kind::Int;
        initial.i = -1;
        c = t.current.addColumn(column, type, initial);
        t.columns.push_back(Column{column, knownColumnName(column), cellTypeOf(type)});
        for (int r = 0; r < t.current.rowCount(); r++) {
            t.cells[r].push_back(toCell(t.current.get(r, c), type));
        }
    }
    Cell stored = value;
    stored.type = t.columns[c].type;  // the column's type wins (an Int written to a Short column)
    t.current.set(row, c, toValue(stored));
    t.cells[row][c] = toCell(t.current.get(row, c), t.current.columns()[c].type);  // as the game will read it back
    mChanged.insert(makeId(zone, table, row));
    if (zone == 0 && t.path.size() >= 12 && t.path.compare(t.path.size() - 12, 12, "stageobjinfo") == 0) {
        computePlacement();
    }
}

bool EditModel::apply(const Op& op, std::string* pError) {
    Entry entry;
    entry.label = op.label;
    // Validate everything first: an op applies whole or not at all.
    std::vector< SetCell > sets = op.sets;
    for (RowId id : op.revertRows) {
        int zone, table, row;
        if (!unpack(id, &zone, &table, &row)) {
            if (pError != nullptr) {
                *pError = "revert: unknown row";
            }
            return false;
        }
        const TableModel& t = mZones[zone].tables[table];
        for (size_t c = 0; c < t.columns.size(); c++) {
            const int original = t.original.column(t.columns[c].hash);
            if (original >= 0 && !sameCell(t.cells[row][c], t.origCells[row][original])) {
                sets.push_back(SetCell{id, t.columns[c].hash, t.origCells[row][original]});
            }
        }
    }
    for (const SetCell& set : sets) {
        int zone, table, row;
        if (!unpack(set.row, &zone, &table, &row)) {
            if (pError != nullptr) {
                *pError = "unknown row";
            }
            return false;
        }
    }
    for (const SetCell& set : sets) {
        int zone, table, row;
        unpack(set.row, &zone, &table, &row);
        Change change;
        change.row = set.row;
        change.column = set.column;
        if (const Cell* pBefore = cell(set.row, set.column)) {
            change.before = *pBefore;
        }
        writeCell(zone, table, row, set.column, set.after);
        change.after = *cell(set.row, set.column);
        entry.changes.push_back(std::move(change));
    }
    mHistory.resize(mCursor);
    entry.serial = mNextSerial++;
    mHistory.push_back(std::move(entry));
    mCursor++;
    mRevision++;
    return true;
}

bool EditModel::undo() {
    if (mCursor == 0) {
        return false;
    }
    const Entry& entry = mHistory[--mCursor];
    for (auto it = entry.changes.rbegin(); it != entry.changes.rend(); ++it) {
        int zone, table, row;
        if (unpack(it->row, &zone, &table, &row)) {
            Cell before = it->before ? *it->before : Cell{it->after.type, -1, 0.0f, {}};
            writeCell(zone, table, row, it->column, before);
        }
    }
    mRevision++;
    return true;
}

bool EditModel::redo() {
    if (mCursor >= historySize()) {
        return false;
    }
    const Entry& entry = mHistory[mCursor++];
    for (const Change& change : entry.changes) {
        int zone, table, row;
        if (unpack(change.row, &zone, &table, &row)) {
            writeCell(zone, table, row, change.column, change.after);
        }
    }
    mRevision++;
    return true;
}

bool EditModel::jumpTo(int cursor) {
    if (cursor < 0 || cursor > historySize()) {
        return false;
    }
    while (mCursor > cursor) {
        undo();
    }
    while (mCursor < cursor) {
        redo();
    }
    return true;
}

std::set< RowId > EditModel::takeChangedRows() {
    std::set< RowId > out;
    out.swap(mChanged);
    return out;
}

void EditModel::markSaved() {
    mSavedSerial = serialAtCursor();
}

// ---- output ---------------------------------------------------------------------------------------

std::vector< std::pair< std::string, Formats::Bytes > > EditModel::changedArchives() const {
    std::vector< std::pair< std::string, Formats::Bytes > > out;
    for (const ZoneModel& zone : mZones) {
        Formats::Archive archive = zone.archive;
        bool changed = false;
        for (const TableModel& table : zone.tables) {
            Bytes now = table.current.build();
            if (now != *zone.archive.file(table.path)) {
                archive.replace(table.path, std::move(now));
                changed = true;
            }
        }
        if (changed) {
            out.emplace_back("/StageData/" + zone.name + ".arc", archive.build());
        }
    }
    return out;
}

const Formats::Table* EditModel::rowTable(RowId id, int* pRowIndex) const {
    int zone, table, row;
    if (!unpack(id, &zone, &table, &row)) {
        return nullptr;
    }
    if (pRowIndex != nullptr) {
        *pRowIndex = row;
    }
    return &mZones[zone].tables[table].current;
}

}  // namespace PetariEditor
