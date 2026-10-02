// The EditModel (edit_model.hpp; TECH.md 1.9 and 2.8).
#include "edit_model.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
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
    return Formats::sjisToUtf8(sjis);
}

std::string utf8ToSjis(const std::string& utf8) {
    return Formats::utf8ToSjis(utf8);
}

// ---- loading ------------------------------------------------------------------------------------

std::unique_ptr< EditModel > EditModel::load(const std::string& galaxy, int scenario, const FileSource& source, std::string* pError,
                                             const FileSource& discSource) {
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
        std::optional< Formats::Archive > disc;
        if (discSource) {
            if (std::optional< Bytes > discRaw = discSource(path)) {
                disc = Formats::Archive::parse(*discRaw);
                if (disc) {
                    zone.discPlain = disc->build();
                }
            }
            zone.onDisc = disc.has_value();
        }
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
            // The disc's table is the original when its rows line up with the loaded one (a mod
            // that added or removed rows: the loaded table is its own original).
            if (disc && disc->file(file) != nullptr) {
                std::optional< Formats::Table > discTable = Formats::Table::parse(*disc->file(file));
                if (discTable && discTable->rowCount() == table->rowCount()) {
                    model.original = std::move(*discTable);
                }
            }
            model.current = std::move(*table);
            for (const Formats::TableColumn& c : model.current.columns()) {
                model.columns.push_back(Column{c.hash, knownColumnName(c.hash), cellTypeOf(c.type)});
            }
            for (int r = 0; r < model.current.rowCount(); r++) {
                std::vector< Cell > row, original;
                for (int c = 0; c < static_cast< int >(model.columns.size()); c++) {
                    row.push_back(toCell(model.current.get(r, c), model.current.columns()[c].type));
                }
                for (int c = 0; c < static_cast< int >(model.original.columns().size()); c++) {
                    original.push_back(toCell(model.original.get(r, c), model.original.columns()[c].type));
                }
                model.cells.push_back(std::move(row));
                model.origCells.push_back(std::move(original));
                model.added.push_back(false);
            }
            zone.tables.push_back(std::move(model));
        }
        for (int t = 0; t < static_cast< int >(zone.tables.size()); t++) {
            for (int r = 0; r < zone.tables[t].current.rowCount(); r++) {
                zone.tables[t].ids.push_back(makeId(static_cast< int >(model->mZones.size()), t, r));
            }
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

// Where a live row is now: its (zone, table) from the id (or the added rows' map), its index by
// search (adds and deletes shift indices). False for unknown and deleted rows.
bool EditModel::unpack(RowId id, int* pZone, int* pTable, int* pRow) const {
    int zone, table;
    if (id.v >> 63) {
        auto it = mNewRowHome.find(id);
        if (it == mNewRowHome.end()) {
            return false;
        }
        zone = it->second.first;
        table = it->second.second;
    } else {
        zone = static_cast< int >(id.v >> 40) - 1;
        table = static_cast< int >((id.v >> 20) & kField) - 1;
    }
    if (zone < 0 || zone >= zoneCount() || table < 0 || table >= tableCount(zone)) {
        return false;
    }
    const std::vector< RowId >& ids = mZones[zone].tables[table].ids;
    // Original rows usually sit at their original index: try it first.
    const int hint = (id.v >> 63) ? -1 : static_cast< int >(id.v & kField) - 1;
    int row = hint >= 0 && hint < static_cast< int >(ids.size()) && ids[hint] == id
                  ? hint
                  : static_cast< int >(std::find(ids.begin(), ids.end(), id) - ids.begin());
    if (row >= static_cast< int >(ids.size())) {
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
    for (RowId id : mZones[zone].tables[table].ids) {
        fn(id);
    }
    for (const auto& [id, record] : mDeleted) {
        if (record.zone == zone && record.table == table) {
            fn(id);
        }
    }
}

bool EditModel::locate(RowId row, int* pZone, int* pTable) const {
    int r;
    if (unpack(row, pZone, pTable, &r)) {
        return true;
    }
    auto it = mDeleted.find(row);
    if (it == mDeleted.end()) {
        return false;
    }
    *pZone = it->second.zone;
    *pTable = it->second.table;
    return true;
}

RowState EditModel::state(RowId id) const {
    if (mDeleted.count(id)) {
        return RowState::Deleted;
    }
    int zone, table, row;
    if (!unpack(id, &zone, &table, &row)) {
        return RowState::Original;
    }
    const TableModel& t = mZones[zone].tables[table];
    if (t.added[row]) {
        return RowState::Added;
    }
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
        auto it = mDeleted.find(id);  // a deleted row reads as it was
        if (it == mDeleted.end()) {
            return nullptr;
        }
        const int c = mZones[it->second.zone].tables[it->second.table].current.column(column);
        return c < 0 || c >= static_cast< int >(it->second.cells.size()) ? nullptr : &it->second.cells[c];
    }
    const TableModel& t = mZones[zone].tables[table];
    const int c = t.current.column(column);
    return c < 0 ? nullptr : &t.cells[row][c];
}

const Cell* EditModel::originalCell(RowId id, std::uint32_t column) const {
    int zone, table, row;
    const std::vector< Cell >* pOriginal = nullptr;
    if (unpack(id, &zone, &table, &row)) {
        pOriginal = &mZones[zone].tables[table].origCells[row];
    } else if (auto it = mDeleted.find(id); it != mDeleted.end()) {
        zone = it->second.zone;
        table = it->second.table;
        pOriginal = &it->second.original;
    } else {
        return nullptr;
    }
    const int c = mZones[zone].tables[table].original.column(column);
    return c < 0 || c >= static_cast< int >(pOriginal->size()) ? nullptr : &(*pOriginal)[c];
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

int EditModel::missionCount() const {
    int count = 0;
    for (int r = 0; r < mScenarioData.rowCount(); r++) {
        count = std::max(count, intCell(mScenarioData, r, "ScenarioNo", 0));
    }
    return count;
}

const char* EditModel::missionName(int mission) const {
    for (int r = 0; r < mScenarioData.rowCount(); r++) {
        if (intCell(mScenarioData, r, "ScenarioNo", -1) == mission) {
            mNameScratch = sjisToUtf8(stringCell(mScenarioData, r, "ScenarioName"));
            return mNameScratch.c_str();
        }
    }
    return "";
}

std::uint32_t EditModel::rowMissions(RowId id) const {
    int zone = 0, table = 0, row = 0;
    if (!unpack(id, &zone, &table, &row)) {
        return 0;
    }
    const int layer = mZones[zone].tables[table].layer;
    std::uint32_t missions = 0;
    for (int mission = 1; mission <= missionCount() && mission <= 32; mission++) {
        bool zoneIn = zone == 0;
        if (!zoneIn) {
            // Placed by a StageObjInfo row of the galaxy in one of this mission's layers.
            const std::uint32_t galaxyLayers = activeLayers(0, mission);
            for (const TableModel& t : mZones[0].tables) {
                if (zoneIn || t.layer < 0 || !(galaxyLayers & (1u << t.layer)) || t.path.size() < 12 ||
                    t.path.compare(t.path.size() - 12, 12, "stageobjinfo") != 0) {
                    continue;
                }
                for (int r = 0; r < t.current.rowCount() && !zoneIn; r++) {
                    zoneIn = stringCell(t.current, r, "name") == mZones[zone].name;
                }
            }
        }
        const bool layerIn = layer < 0 || (activeLayers(zone, mission) & (1u << layer));
        if (zoneIn && layerIn) {
            missions |= 1u << (mission - 1);
        }
    }
    return missions;
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
    mChanged.insert(t.ids[row]);
    if (zone == 0 && t.path.size() >= 12 && t.path.compare(t.path.size() - 12, 12, "stageobjinfo") == 0) {
        computePlacement();
    }
}

// ---- structure: rows in and out --------------------------------------------------------------

void EditModel::insertRecord(const RowRecord& record) {
    TableModel& t = mZones[record.zone].tables[record.table];
    const int index = std::min(record.index, t.current.rowCount());
    Formats::Bytes bytes = record.record;
    bytes.resize(t.current.entrySize(), 0);  // columns added since the row was taken out read as zero
    t.current.insertRowRecord(index, std::move(bytes));
    std::vector< Cell > cells;
    for (int c = 0; c < static_cast< int >(t.columns.size()); c++) {
        cells.push_back(toCell(t.current.get(index, c), t.current.columns()[c].type));
    }
    t.cells.insert(t.cells.begin() + index, std::move(cells));
    t.origCells.insert(t.origCells.begin() + index, record.original);
    t.ids.insert(t.ids.begin() + index, record.id);
    t.added.insert(t.added.begin() + index, record.added);
    if (record.id.v >> 63) {
        mNewRowHome[record.id] = {record.zone, record.table};
    }
    mDeleted.erase(record.id);
    mChanged.insert(record.id);
}

EditModel::RowRecord EditModel::removeRecord(int zone, int table, int row) {
    TableModel& t = mZones[zone].tables[table];
    RowRecord record;
    record.id = t.ids[row];
    record.zone = zone;
    record.table = table;
    record.index = row;
    record.record = t.current.rowRecord(row);
    record.cells = t.cells[row];
    record.original = t.origCells[row];
    record.added = t.added[row];
    t.current.removeRow(row);
    t.cells.erase(t.cells.begin() + row);
    t.origCells.erase(t.origCells.begin() + row);
    t.ids.erase(t.ids.begin() + row);
    t.added.erase(t.added.begin() + row);
    mChanged.insert(record.id);
    return record;
}

// The table at `path` in `zone`, created from a same-named table's columns (another layer's, in
// this zone or any) when the zone lacks it. -1: no table of that name anywhere.
int EditModel::ensureTable(int zone, const std::string& rawPath) {
    std::string path = rawPath;
    for (char& c : path) {
        c = static_cast< char >(std::tolower(static_cast< unsigned char >(c)));
    }
    for (int t = 0; t < tableCount(zone); t++) {
        if (mZones[zone].tables[t].path == path) {
            return t;
        }
    }
    const std::string leaf = path.substr(path.rfind('/') + 1);
    const TableModel* pSibling = nullptr;
    for (int z = 0; z < zoneCount() && pSibling == nullptr; z++) {
        const int first = z == 0 ? zone : (z == zone ? 0 : z);  // this zone first
        for (const TableModel& t : mZones[first].tables) {
            if (t.path.substr(t.path.rfind('/') + 1) == leaf) {
                pSibling = &t;
                break;
            }
        }
    }
    if (pSibling == nullptr || path.rfind("jmp/", 0) != 0) {
        return -1;
    }
    TableModel table;
    table.path = path;
    table.layer = layerOf(path);
    table.original = pSibling->current.schemaOnly();
    table.current = pSibling->current.schemaOnly();
    table.columns = pSibling->columns;
    mZones[zone].tables.push_back(std::move(table));
    mZones[zone].filesAdded = true;
    return tableCount(zone) - 1;
}

// One more than every l_id the zone's placement, map part and start tables use.
std::int32_t EditModel::nextLinkId(int zone) const {
    std::int32_t next = 0;
    for (const TableModel& t : mZones[zone].tables) {
        const int c = t.current.column(Formats::jmapHash("l_id"));
        if (c < 0 || (t.path.rfind("jmp/placement", 0) != 0 && t.path.rfind("jmp/mapparts", 0) != 0 && t.path.rfind("jmp/start", 0) != 0)) {
            continue;
        }
        for (int r = 0; r < t.current.rowCount(); r++) {
            next = std::max(next, t.current.get(r, c).i + 1);
        }
    }
    return next;
}

bool EditModel::addRow(const AddRow& add, Entry* pEntry, std::string* pError) {
    if (add.zone < 0 || add.zone >= zoneCount()) {
        *pError = "add: no such zone";
        return false;
    }
    const int table = ensureTable(add.zone, add.table);
    if (table < 0) {
        *pError = "add: no table like " + add.table;
        return false;
    }
    TableModel& t = mZones[add.zone].tables[table];
    // Start from the row copied (same table: its record; another table: its values by column),
    // else from neutral values.
    int index;
    int fromZone = 0, fromTable = 0, fromRow = 0;
    const bool copy = add.copyOf.valid() && unpack(add.copyOf, &fromZone, &fromTable, &fromRow);
    if (copy && fromZone == add.zone && fromTable == table) {
        index = t.current.addRow(fromRow);
    } else {
        index = t.current.addRow(-1);
        for (int c = 0; c < static_cast< int >(t.columns.size()); c++) {
            const ColumnType type = t.current.columns()[c].type;
            CellValue neutral;
            neutral.kind = type == ColumnType::Float ? CellValue::Kind::Float
                           : (type == ColumnType::StringOffset || type == ColumnType::InlineString) ? CellValue::Kind::String
                                                                                                  : CellValue::Kind::Int;
            const std::uint32_t hash = t.columns[c].hash;
            const bool scale = hash == Formats::jmapHash("scale_x") || hash == Formats::jmapHash("scale_y") || hash == Formats::jmapHash("scale_z");
            neutral.i = -1;
            neutral.f = scale ? 1.0f : 0.0f;
            if (copy) {
                if (const Cell* pFrom = cell(add.copyOf, hash)) {
                    neutral = toValue(*pFrom);
                }
            }
            t.current.set(index, c, neutral);
        }
    }
    std::vector< Cell > cells;
    for (int c = 0; c < static_cast< int >(t.columns.size()); c++) {
        cells.push_back(toCell(t.current.get(index, c), t.current.columns()[c].type));
    }
    const RowId id = add.id.valid() ? add.id : newRowId();
    t.cells.push_back(std::move(cells));
    t.origCells.emplace_back();
    t.ids.push_back(id);
    t.added.push_back(true);
    mNewRowHome[id] = {add.zone, table};
    bool lidGiven = false;
    for (const AddRow::Value& value : add.cells) {
        writeCell(add.zone, table, index, value.column, value.value);
        lidGiven = lidGiven || value.column == Formats::jmapHash("l_id");
    }
    if (!lidGiven && t.current.column(Formats::jmapHash("l_id")) >= 0) {
        writeCell(add.zone, table, index, Formats::jmapHash("l_id"), Cell{CellType::Int, nextLinkId(add.zone) , 0, {}});
    }
    mChanged.insert(id);
    // What redo needs: the finished row.
    RowRecord record;
    record.id = id;
    record.zone = add.zone;
    record.table = table;
    record.index = index;
    record.record = t.current.rowRecord(index);
    record.added = true;
    pEntry->inserted.push_back(std::move(record));
    return true;
}

void EditModel::undoEntry(const Entry& entry) {
    for (auto it = entry.removed.rbegin(); it != entry.removed.rend(); ++it) {
        insertRecord(*it);
    }
    for (auto it = entry.changes.rbegin(); it != entry.changes.rend(); ++it) {
        int zone, table, row;
        if (unpack(it->row, &zone, &table, &row)) {
            Cell before = it->before ? *it->before : Cell{it->after.type, -1, 0.0f, {}};
            writeCell(zone, table, row, it->column, before);
        }
    }
    for (auto it = entry.inserted.rbegin(); it != entry.inserted.rend(); ++it) {
        int zone, table, row;
        if (unpack(it->id, &zone, &table, &row)) {
            removeRecord(zone, table, row);
            mNewRowHome.erase(it->id);
        }
    }
}

void EditModel::redoEntry(const Entry& entry) {
    for (const RowRecord& record : entry.inserted) {
        insertRecord(record);
    }
    for (const Change& change : entry.changes) {
        int zone, table, row;
        if (unpack(change.row, &zone, &table, &row)) {
            writeCell(zone, table, row, change.column, change.after);
        }
    }
    for (const RowRecord& record : entry.removed) {
        int zone, table, row;
        if (unpack(record.id, &zone, &table, &row)) {
            mDeleted[record.id] = removeRecord(zone, table, row);
        }
    }
}

bool EditModel::apply(const Op& op, std::string* pError) {
    std::string error;
    Entry entry;
    entry.label = op.label;
    // An op applies whole or not at all: a failure part-way undoes what was done.
    auto fail = [&](const std::string& message) {
        undoEntry(entry);
        if (pError != nullptr) {
            *pError = message;
        }
        return false;
    };
    for (const AddRow& add : op.adds) {
        if (!addRow(add, &entry, &error)) {
            return fail(error);
        }
    }
    std::vector< SetCell > sets = op.sets;
    for (RowId id : op.revertRows) {
        int zone, table, row;
        if (!unpack(id, &zone, &table, &row)) {
            return fail("revert: unknown row");
        }
        const TableModel& t = mZones[zone].tables[table];
        for (size_t c = 0; c < t.columns.size() && !t.added[row]; c++) {
            const int original = t.original.column(t.columns[c].hash);
            if (original >= 0 && !sameCell(t.cells[row][c], t.origCells[row][original])) {
                sets.push_back(SetCell{id, t.columns[c].hash, t.origCells[row][original]});
            }
        }
    }
    for (const SetCell& set : sets) {
        int zone, table, row;
        if (!unpack(set.row, &zone, &table, &row)) {
            return fail("unknown row");
        }
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
    for (RowId id : op.deletes) {
        int zone, table, row;
        if (!unpack(id, &zone, &table, &row)) {
            return fail("delete: unknown row");
        }
        RowRecord record = removeRecord(zone, table, row);
        mDeleted[id] = record;
        entry.removed.push_back(std::move(record));
    }
    mHistory.resize(mCursor);
    entry.serial = mNextSerial++;
    mHistory.push_back(std::move(entry));
    mCursor++;
    mRevision++;
    return true;
}

bool EditModel::setScenario(int scenario) {
    if (scenario < 1 || scenario > missionCount()) {
        return false;
    }
    if (scenario != mScenario) {
        mScenario = scenario;
        mRevision++;  // which rows are in the scene changed
    }
    return true;
}

bool EditModel::undo() {
    if (mCursor == 0) {
        return false;
    }
    undoEntry(mHistory[--mCursor]);
    mRevision++;
    return true;
}

bool EditModel::redo() {
    if (mCursor >= historySize()) {
        return false;
    }
    redoEntry(mHistory[mCursor++]);
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
        bool edited = false;
        for (const TableModel& table : zone.tables) {
            const Bytes* pLoaded = zone.archive.file(table.path);
            if (pLoaded == nullptr) {
                continue;  // a table made in this session: added with the archive rebuild below
            }
            Bytes now = table.current.build();
            if (now != *pLoaded) {
                archive.replace(table.path, std::move(now));
                edited = true;
            }
        }
        Bytes built;
        if (zone.filesAdded) {
            // A new table: a new archive (every file, the new ones included).
            std::vector< std::pair< std::string, Bytes > > files;
            for (const std::string& path : zone.archive.files()) {
                files.emplace_back(path, *archive.file(path));
            }
            for (const TableModel& table : zone.tables) {
                if (!zone.archive.has(table.path)) {
                    files.emplace_back(table.path, table.current.build());
                }
            }
            built = Formats::Archive::create(zone.archive.root(), files).build();
            edited = true;
        } else {
            built = archive.build();
        }
        // Changed means "differs from the disc" when the disc is known (a mod's earlier edits
        // count), else "edited since loading".
        const bool changed = zone.discPlain.empty() ? edited : built != zone.discPlain;
        if (changed) {
            out.emplace_back("/StageData/" + zone.name + ".arc", std::move(built));
        }
    }
    return out;
}

RowId EditModel::rowAt(int zone, int table, int row) const {
    if (table < 0 || table >= tableCount(zone) || row < 0 || row >= mZones[zone].tables[table].current.rowCount()) {
        return {};
    }
    return mZones[zone].tables[table].ids[row];
}

int EditModel::tableLayer(int zone, int table) const {
    return table >= 0 && table < tableCount(zone) ? mZones[zone].tables[table].layer : -1;
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

// ---- patch ---------------------------------------------------------------------------------------

namespace {
    // Rows of `from` matched to rows of `to` (index in `to`, or -1): equal rows first, in order,
    // then rows with the same name and l_id (edited since the disc).
    std::vector< int > matchRows(const Formats::Table& from, const Formats::Table& to) {
        auto key = [](const Formats::Table& t, int row, const Formats::Table& columnsOf) {
            std::string k;
            for (const Formats::TableColumn& c : columnsOf.columns()) {
                const int index = t.column(c.hash);
                const CellValue v = index >= 0 ? t.get(row, index) : CellValue{};
                k += std::to_string(static_cast< int >(v.kind)) + ":" + std::to_string(v.i) + ":" + std::to_string(v.f) + ":" + v.s + "|";
            }
            return k;
        };
        std::vector< int > out(from.rowCount(), -1);
        std::vector< bool > used(to.rowCount(), false);
        std::map< std::string, std::vector< int > > byKey;
        for (int r = to.rowCount() - 1; r >= 0; r--) byKey[key(to, r, to)].push_back(r);
        for (int r = 0; r < from.rowCount(); r++) {
            auto it = byKey.find(key(from, r, to));
            if (it != byKey.end() && !it->second.empty()) {
                out[r] = it->second.back();
                used[out[r]] = true;
                it->second.pop_back();
            }
        }
        const int nameFrom = from.column(Formats::jmapHash("name")), nameTo = to.column(Formats::jmapHash("name"));
        const int lidFrom = from.column(Formats::jmapHash("l_id")), lidTo = to.column(Formats::jmapHash("l_id"));
        if (nameFrom >= 0 && nameTo >= 0 && lidFrom >= 0 && lidTo >= 0) {
            for (int r = 0; r < from.rowCount(); r++) {
                if (out[r] >= 0) continue;
                for (int d = 0; d < to.rowCount(); d++) {
                    if (!used[d] && to.get(d, nameTo) == from.get(r, nameFrom) && to.get(d, lidTo) == from.get(r, lidFrom)) {
                        out[r] = d;
                        used[d] = true;
                        break;
                    }
                }
            }
        }
        return out;
    }

    std::string patchColumnName(std::uint32_t hash) {
        if (const char* pName = knownColumnName(hash)) return pName;
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "#%08x", hash);
        return buffer;
    }

    Json::Value patchCell(const CellValue& v) {
        if (v.kind == CellValue::Kind::String) return Json::Value(sjisToUtf8(v.s));
        if (v.kind == CellValue::Kind::Float) return Json::Value(static_cast< double >(v.f));
        return Json::Value(v.i);
    }

    CellValue neutralCell(ColumnType type) {
        CellValue v;
        v.kind = type == ColumnType::Float ? CellValue::Kind::Float
                 : (type == ColumnType::StringOffset || type == ColumnType::InlineString) ? CellValue::Kind::String
                                                                                          : CellValue::Kind::Int;
        v.i = -1;
        return v;
    }

    bool sameColumns(const Formats::Table& a, const Formats::Table& b) {
        if (a.columns().size() != b.columns().size()) return false;
        for (size_t c = 0; c < a.columns().size(); c++) {
            const Formats::TableColumn &x = a.columns()[c], &y = b.columns()[c];
            if (x.hash != y.hash || x.type != y.type || x.mask != y.mask || x.shift != y.shift || x.offset != y.offset) return false;
        }
        return true;
    }
}  // namespace

EditModel::PatchOut EditModel::patch() const {
    PatchOut out;
    Json::Value archives = Json::Value::object();
    for (const ZoneModel& zone : mZones) {
        const std::string archivePath = "StageData/" + zone.name + ".arc";
        if (!zone.onDisc) {
            for (const auto& [path, bytes] : changedArchives()) {
                if (path == "/" + archivePath) out.wholeArchives.emplace_back(path, bytes);
            }
            continue;
        }
        std::optional< Formats::Archive > discArchive =
            zone.discPlain.empty() ? std::optional< Formats::Archive >(zone.archive) : Formats::Archive::parse(zone.discPlain);
        if (!discArchive) continue;
        Json::Value ops = Json::Value::array();
        for (const TableModel& t : zone.tables) {
            const Formats::Bytes* pDiscBytes = discArchive->file(t.path);
            std::optional< Formats::Table > disc = pDiscBytes ? Formats::Table::parse(*pDiscBytes) : std::nullopt;
            const Formats::Table& cur = t.current;
            auto addRowOp = [&](int r) {
                Json::Value op = Json::Value::object();
                op.set("op", "add");
                op.set("table", t.path);
                op.set("at", r);
                Json::Value cells = Json::Value::object();
                for (int c = 0; c < static_cast< int >(cur.columns().size()); c++) {
                    cells.set(patchColumnName(cur.columns()[c].hash), patchCell(cur.get(r, c)));
                }
                op.set("cells", std::move(cells));
                ops.push(std::move(op));
            };
            if (!disc) {
                if (cur.rowCount() == 0) continue;
                // A table the disc's archive lacks (a new layer): from a disc table with its columns, or the columns listed.
                Json::Value op = Json::Value::object();
                op.set("op", "addFile");
                op.set("table", t.path);
                std::string schemaOf;
                for (const std::string& path : discArchive->files()) {
                    std::optional< Formats::Table > other = Formats::Table::parse(*discArchive->file(path));
                    if (other && sameColumns(*other, cur)) {
                        schemaOf = path;
                        break;
                    }
                }
                if (!schemaOf.empty()) {
                    op.set("schemaOf", schemaOf);
                } else {
                    Json::Value columns = Json::Value::array();
                    for (const Formats::TableColumn& c : cur.columns()) {
                        Json::Value column = Json::Value::object();
                        column.set("name", patchColumnName(c.hash));
                        column.set("type", Patch::typeName(c.type));
                        columns.push(std::move(column));
                    }
                    op.set("columns", std::move(columns));
                }
                ops.push(std::move(op));
                for (int r = 0; r < cur.rowCount(); r++) addRowOp(r);
                continue;
            }
            // Each current row's disc row: its own index when the loaded table is the disc's, else matched.
            const Formats::Bytes* pSource = zone.archive.file(t.path);
            const bool sourceIsDisc = pSource != nullptr && *pSource == *pDiscBytes;
            const std::vector< int > sourceToDisc = sourceIsDisc ? std::vector< int >() : matchRows(t.original, *disc);
            std::vector< int > discOf(cur.rowCount(), -1);
            std::vector< bool > kept(disc->rowCount(), false);
            for (int r = 0; r < cur.rowCount(); r++) {
                if (t.added[r] || (t.ids[r].v >> 63)) continue;
                const int source = static_cast< int >(t.ids[r].v & kField) - 1;
                const int d = sourceIsDisc ? source : (source >= 0 && source < static_cast< int >(sourceToDisc.size()) ? sourceToDisc[source] : -1);
                if (d >= 0 && d < disc->rowCount() && !kept[d]) {
                    discOf[r] = d;
                    kept[d] = true;
                }
            }
            for (const Formats::TableColumn& c : cur.columns()) {
                if (disc->column(c.hash) >= 0) continue;
                Json::Value op = Json::Value::object();
                op.set("op", "addColumn");
                op.set("table", t.path);
                op.set("name", patchColumnName(c.hash));
                op.set("type", Patch::typeName(c.type));
                op.set("default", patchCell(neutralCell(c.type)));
                ops.push(std::move(op));
            }
            for (int r = 0; r < cur.rowCount(); r++) {
                if (discOf[r] < 0) continue;
                Json::Value cells = Json::Value::object();
                for (int c = 0; c < static_cast< int >(cur.columns().size()); c++) {
                    const int dc = disc->column(cur.columns()[c].hash);
                    const CellValue now = cur.get(r, c);
                    const CellValue before = dc >= 0 ? disc->get(discOf[r], dc) : neutralCell(cur.columns()[c].type);
                    if (now != before) cells.set(patchColumnName(cur.columns()[c].hash), patchCell(now));
                }
                if (cells.members().empty()) continue;
                Json::Value op = Json::Value::object();
                op.set("op", "set");
                op.set("table", t.path);
                op.set("row", discOf[r]);
                op.set("fp", Patch::fingerprint(*disc, discOf[r]));
                op.set("cells", std::move(cells));
                ops.push(std::move(op));
            }
            for (int d = 0; d < disc->rowCount(); d++) {
                if (kept[d]) continue;
                Json::Value op = Json::Value::object();
                op.set("op", "delete");
                op.set("table", t.path);
                op.set("row", d);
                op.set("fp", Patch::fingerprint(*disc, d));
                ops.push(std::move(op));
            }
            for (int r = 0; r < cur.rowCount(); r++) {
                if (discOf[r] < 0) addRowOp(r);
            }
        }
        if (!ops.items().empty()) {
            out.ops += static_cast< int >(ops.items().size());
            archives.set(archivePath, std::move(ops));
        }
    }
    if (out.ops > 0) {
        Json::Value doc = Json::Value::object();
        doc.set("format", 1);
        doc.set("galaxy", mGalaxy);
        doc.set("archives", std::move(archives));
        out.json = Json::write(doc, true) + "\n";
    }
    return out;
}

}  // namespace PetariEditor
