// Low-level ops as JSON (level_api.hpp): the journal's format and what live mode sends to a running game.
#include "level_internal.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace PetariEditor::LevelApi {

namespace {
    constexpr std::uint64_t kNewBit = 1ull << 63;
    constexpr std::uint64_t kApiBit = 1ull << 62;
    constexpr std::uint64_t kField = (1ull << 20) - 1;

    int findZone(const ModelView& model, const std::string& name) {
        for (int z = 0; z < model.zoneCount(); z++) {
            if (lower(model.zoneName(z)) == lower(name)) {
                return z;
            }
        }
        // A zone index is accepted too.
        char* end = nullptr;
        const long index = std::strtol(name.c_str(), &end, 10);
        if (!name.empty() && end != nullptr && *end == '\0' && index >= 0 && index < model.zoneCount()) {
            return static_cast< int >(index);
        }
        return -1;
    }

    int findTable(const ModelView& model, int zone, const std::string& name) {
        const std::string want = lower(name);
        for (int t = 0; t < model.tableCount(zone); t++) {
            const std::string path = model.tablePath(zone, t);
            if (path == want || shortTablePath(path) == want) {
                return t;
            }
        }
        return -1;
    }

    Json::Value cellJson(const Cell& cell) {
        switch (cell.type) {
        case CellType::Float:
            return Json::Value(static_cast< double >(cell.f));
        case CellType::String:
        case CellType::StringOffset:
            return Json::Value(cell.s);
        default:
            return Json::Value(static_cast< double >(cell.i));
        }
    }

    bool isFloatName(const std::string& name) {
        return name.rfind("pos_", 0) == 0 || name.rfind("dir_", 0) == 0 || name.rfind("scale_", 0) == 0;
    }

    std::optional< CellType > typeFromName(const std::string& type) {
        if (type == "float") return CellType::Float;
        if (type == "long" || type == "int" || type == "long2") return CellType::Int;
        if (type == "short") return CellType::Short;
        if (type == "byte") return CellType::Byte;
        if (type == "string") return CellType::String;
        if (type == "stringOffset") return CellType::StringOffset;
        return std::nullopt;
    }
}  // namespace

std::string shortTablePath(const std::string& path) {
    if (path.rfind("jmp/placement/", 0) == 0) return path.substr(14);
    if (path.rfind("jmp/", 0) == 0) return path.substr(4);
    return path;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast< char >(std::tolower(static_cast< unsigned char >(c)));
    return s;
}

std::string columnLabel(std::uint32_t hash) {
    if (const char* pName = knownColumnName(hash)) return pName;
    char text[16];
    std::snprintf(text, sizeof(text), "#%08x", hash);
    return text;
}

std::uint32_t columnHashOfName(const std::string& name) {
    if (!name.empty() && name[0] == '#') return static_cast< std::uint32_t >(std::strtoul(name.c_str() + 1, nullptr, 16));
    return columnHash(name.c_str());
}

RowId newRowIdFor(std::uint64_t n) {
    return RowId{kNewBit | kApiBit | n};
}

std::string rowRef(const ModelView& model, RowId row) {
    if (!row.valid()) {
        return "";
    }
    if (row.v & kNewBit) {
        // The API's own new rows, and rows the in-game editor added (newRowId(): bit 63 only).
        return (row.v & kApiBit) ? "new:" + std::to_string(row.v & ~(kNewBit | kApiBit)) : "edit:" + std::to_string(row.v & ~kNewBit);
    }
    const int zone = static_cast< int >(row.v >> 40) - 1;
    const int table = static_cast< int >((row.v >> 20) & kField) - 1;
    const int index = static_cast< int >(row.v & kField) - 1;
    if (zone < 0 || zone >= model.zoneCount() || table < 0 || table >= model.tableCount(zone)) {
        return "";
    }
    return std::string(model.zoneName(zone)) + ":" + shortTablePath(model.tablePath(zone, table)) + ":" + std::to_string(index);
}

RowId parseRowRef(const ModelView& model, const std::string& ref) {
    auto digits = [](const std::string& s, std::uint64_t* pOut) {
        if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos) {
            return false;
        }
        *pOut = std::strtoull(s.c_str(), nullptr, 10);
        return true;
    };
    std::uint64_t n = 0;
    if (ref.rfind("new:", 0) == 0 && digits(ref.substr(4), &n)) {
        return newRowIdFor(n);
    }
    if (ref.rfind("edit:", 0) == 0 && digits(ref.substr(5), &n)) {
        return RowId{kNewBit | n};
    }
    const std::size_t a = ref.find(':');
    const std::size_t b = ref.rfind(':');
    if (a == std::string::npos || a == b || !digits(ref.substr(b + 1), &n)) {
        return {};
    }
    const int zone = findZone(model, ref.substr(0, a));
    const int table = zone >= 0 ? findTable(model, zone, ref.substr(a + 1, b - a - 1)) : -1;
    if (table < 0 || n >= kField) {
        return {};
    }
    return RowId{static_cast< std::uint64_t >(zone + 1) << 40 | static_cast< std::uint64_t >(table + 1) << 20 | (n + 1)};
}

Json::Value opToJson(const ModelView& model, const Op& op) {
    Json::Value out = Json::Value::object();
    out.set("label", op.label);
    if (!op.adds.empty()) {
        Json::Value adds = Json::Value::array();
        for (const AddRow& add : op.adds) {
            Json::Value a = Json::Value::object();
            a.set("id", rowRef(model, add.id));
            a.set("zone", model.zoneName(add.zone));
            a.set("table", add.table);
            if (add.copyOf.valid()) {
                a.set("copyOf", rowRef(model, add.copyOf));
            }
            Json::Value cells = Json::Value::object();
            for (const AddRow::Value& v : add.cells) {
                cells.set(columnLabel(v.column), cellJson(v.value));
            }
            a.set("cells", std::move(cells));
            adds.push(std::move(a));
        }
        out.set("adds", std::move(adds));
    }
    if (!op.sets.empty()) {
        // Grouped by row, in first-seen order.
        Json::Value sets = Json::Value::array();
        std::vector< RowId > order;
        for (const SetCell& s : op.sets) {
            if (std::find(order.begin(), order.end(), s.row) == order.end()) {
                order.push_back(s.row);
            }
        }
        for (RowId row : order) {
            Json::Value cells = Json::Value::object();
            for (const SetCell& s : op.sets) {
                if (s.row == row) {
                    cells.set(columnLabel(s.column), cellJson(s.after));
                }
            }
            Json::Value set = Json::Value::object();
            set.set("row", rowRef(model, row));
            set.set("cells", std::move(cells));
            sets.push(std::move(set));
        }
        out.set("sets", std::move(sets));
    }
    auto refs = [&](const std::vector< RowId >& rows) {
        Json::Value list = Json::Value::array();
        for (RowId row : rows) {
            list.push(rowRef(model, row));
        }
        return list;
    };
    if (!op.deletes.empty()) {
        out.set("deletes", refs(op.deletes));
    }
    if (!op.revertRows.empty()) {
        out.set("reverts", refs(op.revertRows));
    }
    return out;
}

namespace {
    // A cell for `column` of a table with `columns` (empty: the type is inferred from the value and name).
    bool cellFromJson(const Json::Value& value, const std::string& name, const std::vector< Column >* pColumns, Cell* pCell,
                      std::string* pError) {
        const std::uint32_t hash = columnHashOfName(name);
        std::optional< CellType > type;
        const Json::Value* pValue = &value;
        if (value.isObject()) {
            // {"type": "float", "value": 1}: a column the table lacks, with its type.
            type = typeFromName(value["type"].asString());
            pValue = &value["value"];
        }
        if (!type && pColumns != nullptr) {
            for (const Column& c : *pColumns) {
                if (c.hash == hash) {
                    type = c.type;
                }
            }
        }
        if (!type) {
            if (pValue->type() == Json::Value::Type::String) {
                type = CellType::StringOffset;
            } else {
                const double n = pValue->asNumber();
                type = (isFloatName(name) || n != std::floor(n)) ? CellType::Float : CellType::Int;
            }
        }
        Cell cell;
        cell.type = *type;
        if (*type == CellType::String || *type == CellType::StringOffset) {
            if (pValue->type() != Json::Value::Type::String) {
                *pError = name + " is a text field";
                return false;
            }
            cell.s = pValue->asString();
        } else {
            if (pValue->type() != Json::Value::Type::Number) {
                *pError = name + " is a number field";
                return false;
            }
            if (*type == CellType::Float) {
                cell.f = static_cast< float >(pValue->asNumber());
            } else {
                const double n = pValue->asNumber();
                if (n != std::floor(n)) {
                    *pError = name + " is a whole-number field";
                    return false;
                }
                cell.i = static_cast< std::int32_t >(n);
            }
        }
        *pCell = std::move(cell);
        return true;
    }

    // The columns a new row in `table` of `zone` will have: the table's, else a same-named table's
    // elsewhere (EditModel::ensureTable copies that schema).
    const std::vector< Column >* columnsFor(const ModelView& model, int zone, const std::string& path) {
        const std::string want = lower(path);
        const std::string leaf = want.substr(want.rfind('/') + 1);
        for (int t = 0; t < model.tableCount(zone); t++) {
            if (model.tablePath(zone, t) == want) {
                return &model.columns(zone, t);
            }
        }
        for (int z = 0; z < model.zoneCount(); z++) {
            for (int t = 0; t < model.tableCount(z); t++) {
                const std::string p = model.tablePath(z, t);
                if (p.substr(p.rfind('/') + 1) == leaf) {
                    return &model.columns(z, t);
                }
            }
        }
        return nullptr;
    }
}  // namespace

bool parseCell(const Json::Value& value, const std::string& name, const std::vector< Column >* pColumns, Cell* pCell, std::string* pError) {
    return cellFromJson(value, name, pColumns, pCell, pError);
}

bool opFromJson(const ModelView& model, const Json::Value& json, Op* pOp, std::string* pError) {
    Op op;
    op.label = json["label"].asString();
    // New rows first, so sets and deletes in the same op can name them.
    std::map< RowId, std::pair< int, std::string > > added;
    for (const Json::Value& a : json["adds"].items()) {
        AddRow add;
        add.id = parseRowRef(model, a["id"].asString());
        if (!add.id.valid() || !(add.id.v & kNewBit)) {
            *pError = "adds: bad id '" + a["id"].asString() + "' (want new:<n>)";
            return false;
        }
        add.zone = findZone(model, a["zone"].asString());
        if (add.zone < 0) {
            *pError = "adds: no zone '" + a["zone"].asString() + "'";
            return false;
        }
        add.table = a["table"].asString();
        if (a.has("copyOf")) {
            add.copyOf = parseRowRef(model, a["copyOf"].asString());
        }
        const std::vector< Column >* pColumns = columnsFor(model, add.zone, add.table);
        for (const auto& [name, value] : a["cells"].members()) {
            Cell cell;
            if (!cellFromJson(value, name, pColumns, &cell, pError)) {
                return false;
            }
            add.cells.push_back({columnHashOfName(name), std::move(cell)});
        }
        added[add.id] = {add.zone, add.table};
        op.adds.push_back(std::move(add));
    }
    auto rowOf = [&](const std::string& ref, RowId* pRow) {
        *pRow = parseRowRef(model, ref);
        int zone = 0, table = 0;
        // New rows may come from an earlier op not applied yet (live mode queues several): the model checks them.
        if (!pRow->valid() || (!added.count(*pRow) && !(pRow->v & kNewBit) && !model.locate(*pRow, &zone, &table))) {
            *pError = "no row '" + ref + "'";
            return false;
        }
        return true;
    };
    for (const Json::Value& s : json["sets"].items()) {
        RowId row;
        if (!rowOf(s["row"].asString(), &row)) {
            return false;
        }
        const std::vector< Column >* pColumns = nullptr;
        int zone = 0, table = 0;
        if (model.locate(row, &zone, &table)) {
            pColumns = &model.columns(zone, table);
        } else if (added.count(row)) {
            pColumns = columnsFor(model, added[row].first, added[row].second);
        }
        for (const auto& [name, value] : s["cells"].members()) {
            Cell cell;
            if (!cellFromJson(value, name, pColumns, &cell, pError)) {
                return false;
            }
            op.sets.push_back({row, columnHashOfName(name), std::move(cell)});
        }
    }
    for (const Json::Value& d : json["deletes"].items()) {
        RowId row;
        if (!rowOf(d.asString(), &row)) {
            return false;
        }
        op.deletes.push_back(row);
    }
    for (const Json::Value& r : json["reverts"].items()) {
        RowId row;
        if (!rowOf(r.asString(), &row)) {
            return false;
        }
        op.revertRows.push_back(row);
    }
    *pOp = std::move(op);
    return true;
}

}  // namespace PetariEditor::LevelApi
