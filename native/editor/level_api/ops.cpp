// Batches (level_api.hpp): high-level ops applied one by one to the EditModel, then merged into ONE op
// (one undo step, one journal entry), all or nothing.
#include "level_internal.hpp"

#include "checks.hpp"
#include "world.hpp"

#include <petari/editor/paths.hpp>
#include <petari/editor/gravity.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace PetariEditor::LevelApi {

namespace EditorGravity = PetariEditor::Gravity;

namespace {
    // ---- snapshots and the merged op ----------------------------------------------------------------

    struct RowSnap {
        int zone = 0, table = 0, index = 0;
        std::vector< std::pair< std::uint32_t, Cell > > cells;
    };
    using Snapshot = std::map< RowId, RowSnap >;

    bool sameCell(const Cell& a, const Cell& b) {
        if (a.type != b.type)
            return false;
        switch (a.type) {
        case CellType::Float:
            return a.f == b.f || (a.f != a.f && b.f != b.f);
        case CellType::String:
        case CellType::StringOffset:
            return a.s == b.s;
        default:
            return a.i == b.i;
        }
    }

    Snapshot snapshot(const EditModel& model) {
        Snapshot snap;
        for (int z = 0; z < model.zoneCount(); z++) {
            for (int t = 0; t < model.tableCount(z); t++) {
                int index = 0;
                model.forEachRow(z, t, [&](RowId row) {
                    if (model.state(row) == RowState::Deleted)
                        return;
                    RowSnap s;
                    s.zone = z;
                    s.table = t;
                    s.index = index++;
                    for (const Column& c : model.columns(z, t)) {
                        if (const Cell* cell = model.cell(row, c.hash))
                            s.cells.push_back({c.hash, *cell});
                    }
                    snap[row] = std::move(s);
                });
            }
        }
        return snap;
    }

    // The net change from `before` to `after` as one op: adds (every cell given), sets, deletes.
    Op mergedOp(const EditModel& model, const Snapshot& before, const Snapshot& after, const std::string& label) {
        Op op;
        op.label = label;
        std::vector< std::pair< std::tuple< int, int, int >, RowId > > added;
        for (const auto& [row, a] : after) {
            auto it = before.find(row);
            if (it == before.end()) {
                added.push_back({{a.zone, a.table, a.index}, row});
                continue;
            }
            for (const auto& [hash, cell] : a.cells) {
                const Cell* old = nullptr;
                for (const auto& [h, c] : it->second.cells) {
                    if (h == hash)
                        old = &c;
                }
                if (old == nullptr || !sameCell(*old, cell))
                    op.sets.push_back({row, hash, cell});
            }
        }
        std::sort(added.begin(), added.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
        // Core applies adds before deletes. Keep deleted rows in this simulation while
        // inserting immediately before the next surviving original row of each table.
        std::map< std::pair< int, int >, std::vector< std::pair< int, RowId > > > original;
        for (const auto& [row, b] : before)
            original[{b.zone, b.table}].push_back({b.index, row});
        std::map< std::pair< int, int >, std::vector< RowId > > order;
        for (auto& [table, rows] : original) {
            std::sort(rows.begin(), rows.end());
            for (const auto& [index, row] : rows)
                order[table].push_back(row);
        }
        for (const auto& [where, row] : added) {
            const RowSnap& a = after.at(row);
            AddRow add;
            add.id = row;
            add.zone = a.zone;
            add.table = model.tablePath(a.zone, a.table);
            auto& rows = order[{a.zone, a.table}];
            RowId anchor;
            int nextIndex = std::numeric_limits< int >::max();
            for (const auto& [candidate, b] : after) {
                if (before.count(candidate) && b.zone == a.zone && b.table == a.table && b.index > a.index && b.index < nextIndex) {
                    anchor = candidate;
                    nextIndex = b.index;
                }
            }
            const auto at = anchor.valid() ? std::find(rows.begin(), rows.end(), anchor) : rows.end();
            add.at = static_cast< int >(at - rows.begin());
            rows.insert(at, row);
            for (const auto& [hash, cell] : a.cells)
                add.cells.push_back({hash, cell});
            op.adds.push_back(std::move(add));
        }
        for (const auto& [row, b] : before) {
            if (!after.count(row))
                op.deletes.push_back(row);
        }
        return op;
    }

    // ---- helpers -------------------------------------------------------------------------------

    void checkKeys(const Json::Value& op, const std::vector< std::string >& allowed, const std::string& where) {
        for (const auto& [key, value] : op.members()) {
            if (std::find(allowed.begin(), allowed.end(), key) != allowed.end())
                continue;
            std::vector< std::string > close;
            for (const std::string& a : allowed) {
                if (lower(a).find(lower(key).substr(0, 3)) != std::string::npos || lower(key).find(lower(a)) != std::string::npos)
                    close.push_back(a);
            }
            fail("bad-args", where + ": unknown key '" + key + "'", "allowed: see `help --op " + op["op"].asString() + "`",
                 close.empty() ? allowed : close, where + "." + key);
        }
    }

    const std::vector< std::string > kEditKeys = {"pos", "zone_pos", "by", "rot", "zone_rot", "up", "front", "turn", "scale",
                                                  "args", "switches", "links", "fields", "on_ground", "height", "down", "align"};

    // The ground below `point` along `down` (front faces only), or a failure an agent can act on: the point
    // is inside a collider (say how far to raise it), or nothing is below.
    constexpr float kInsideTolerance = 5.0f;  // numerical slack only: a point inside a collider is an error

    Hit groundBelow(World& world, const ModelView& model, G::V3 point, G::V3 down, const std::string& gravitySource, const std::string& where,
                    float* pLifted = nullptr) {
        if (std::optional< Hit > inside = world.insideOf(point, down); inside && inside->distance <= kInsideTolerance) {
            if (pLifted != nullptr)
                *pLifted = inside->distance;
            return *inside;  // the surface just above (it faces up, away from the point)
        }
        if (std::optional< Hit > inside = world.insideOf(point, down)) {
            const char* name = model.objectName(inside->row);
            char text[64];
            std::snprintf(text, sizeof(text), "%.0f", inside->distance);
            fail("inside-collision",
                 where + ": " + Json::write(vecJson(point, 0)) + " is inside " + (name != nullptr ? name : "a collider") + " (" +
                     rowRef(model, inside->row) + "), " + text + " units below its surface",
                 std::string("raise the point at least ") + text + " units against gravity (" + gravitySource +
                     "), or start from above the ground",
                 {rowRef(model, inside->row)}, where);
        }
        std::optional< Hit > hit = world.ray(point, down, 50000.0f);
        if (!hit) {
            fail("no-ground",
                 where + ": no ground below " + Json::write(vecJson(point, 0)) + " along gravity " + Json::write(vecJson(down, 3)) + " (" +
                                  gravitySource + ")",
                 "move the point over a planet, give \"down\": [x,y,z], or check `colliders`", {}, where);
        }
        return *hit;
    }

    std::vector< std::string > with(std::vector< std::string > a, const std::vector< std::string >& b) {
        a.insert(a.end(), b.begin(), b.end());
        return a;
    }

    Cell intCell(std::int32_t v) {
        Cell c;
        c.type = CellType::Int;
        c.i = v;
        return c;
    }

    Cell floatCell(float v) {
        Cell c;
        c.type = CellType::Float;
        c.f = v;
        return c;
    }

    bool hasColumn(const ModelView& model, int zone, int table, std::uint32_t hash) {
        for (const Column& c : model.columns(zone, table)) {
            if (c.hash == hash)
                return true;
        }
        return false;
    }

    std::int32_t wholeNumber(const Json::Value& v, const std::string& where) {
        const double n = v.asNumber();
        if (v.type() != Json::Value::Type::Number || !std::isfinite(n) || n != std::floor(n) ||
            n < std::numeric_limits<std::int32_t>::min() || n > std::numeric_limits<std::int32_t>::max())
            fail("bad-value", where + " must be a whole number", "", {}, where);
        return static_cast< std::int32_t >(n);
    }

    std::string layerTableName(const std::string& letter) {
        return letter == "common" ? "common" : "layer" + letter;
    }
}  // namespace

std::vector< Check > Level::Impl::newChecks() const {
    std::vector< Check > out;
    for (Check& c : computeChecks(*model)) {
        if (!isBaseline(c))
            out.push_back(std::move(c));
    }
    return out;
}

Json::Value Level::Impl::checksJson(bool all) const {
    Json::Value list = Json::Value::array();
    int errors = 0, warnings = 0;
    for (const Check& c : computeChecks(*model)) {
        const bool known = isBaseline(c);
        if (known && !all)
            continue;
        Json::Value j = Json::Value::object();
        j.set("key", c.key);
        j.set("severity", c.severity == Severity::Error ? "error" : "warning");
        j.set("message", c.message);
        Json::Value rows = Json::Value::array();
        for (RowId row : c.rows) {
            Json::Value r = Json::Value::object();
            r.set("ref", rowRef(*model, row));
            const char* name = model->objectName(row);
            r.set("class", name != nullptr ? name : "");
            rows.push(std::move(r));
        }
        j.set("rows", std::move(rows));
        if (!c.fixLabel.empty())
            j.set("fix", c.fixLabel);
        if (!c.fixKind.empty())
            j.set("fix_kind", c.fixKind);
        if (all)
            j.set("shipped", known);
        (c.severity == Severity::Error ? errors : warnings)++;
        list.push(std::move(j));
    }
    Json::Value out = Json::Value::object();
    out.set("errors", errors);
    out.set("warnings", warnings);
    out.set("checks", std::move(list));
    return out;
}

namespace {
    struct Batch {
        Level::Impl& impl;
        EditModel& model;
        int steps = 0;
        std::vector< RowId > created;
        Json::Value groundInfo;  // the last on_ground placement (reported per created row)
        Json::Value placements = Json::Value::array();
        Json::Value allocatedSwitches = Json::Value::array();
        std::map<int, std::set<std::int32_t>> reservedSwitches;

        void apply(const Op& op, const std::string& where) {
            std::string error;
            if (!model.apply(op, &error))
                fail("op-failed", where + ": " + error, "", {}, where);
            steps++;
        }

        int zoneOf(const Json::Value& v, const std::string& where) {
            const std::string want = v.type() == Json::Value::Type::Number ? std::to_string(v.asInt()) : v.asString();
            std::vector< std::string > zones;
            for (int z = 0; z < model.zoneCount(); z++) {
                if (lower(model.zoneName(z)) == lower(want) || std::to_string(z) == want)
                    return z;
                zones.push_back(model.zoneName(z));
            }
            fail("bad-value", where + ": no zone '" + want + "'", "", zones, where);
        }

        // "common", "a".."p", "layera"; or the missions a layer must be active in (exactly).
        std::string layerFor(const Json::Value& op, int zone, const std::string& where) {
            if (op.has("layer")) {
                std::string l = lower(op["layer"].asString());
                if (l.rfind("layer", 0) == 0)
                    l = l.substr(5);
                if (l == "common" || (l.size() == 1 && l[0] >= 'a' && l[0] <= 'p'))
                    return l;
                fail("bad-value", where + ".layer must be \"common\" or a letter a-p", "", {"common", "a", "b", "c"}, where + ".layer");
            }
            if (op.has("missions")) {
                std::uint32_t want = 0;
                for (const Json::Value& m : op["missions"].items()) {
                    const int n = m.asInt();
                    if (n < 1 || n > model.missionCount())
                        fail("bad-value", where + ".missions: no mission " + std::to_string(n), "", {}, where + ".missions");
                    want |= 1u << (n - 1);
                }
                const std::uint32_t everyMission = model.missionCount() >= 32 ? ~0u : (1u << model.missionCount()) - 1;
                if (want == everyMission)
                    return "common";
                std::vector< std::string > candidates;
                for (int layer = 1; layer <= 16; layer++) {
                    std::uint32_t in = 0;
                    for (int m = 1; m <= model.missionCount(); m++) {
                        if (model.activeLayers(zone, m) & (1u << layer))
                            in |= 1u << (m - 1);
                    }
                    const std::string letter(1, static_cast< char >('a' + layer - 1));
                    if (in == want)
                        return letter;
                    if (in != 0) {
                        std::string text = letter + ": missions";
                        for (int m = 1; m <= model.missionCount(); m++) {
                            if (in & (1u << (m - 1)))
                                text += " " + std::to_string(m);
                        }
                        candidates.push_back(text);
                    }
                }
                fail("bad-value", where + ".missions: no layer of " + std::string(model.zoneName(zone)) + " is active in exactly those missions",
                     "use \"layer\" with one of these, or \"common\" (every mission)", candidates, where + ".missions");
            }
            return "common";
        }

        // pos/rot/scale/args/switches/links/fields on one row, as one op.
        void edit(RowId row, const Json::Value& op, const std::string& where) {
            int zone = 0, table = 0;
            model.locate(row, &zone, &table);
            Op set;
            auto put = [&](const char* column, Cell cell, const std::string& at) {
                const std::uint32_t hash = columnHash(column);
                if (!hasColumn(model, zone, table, hash)) {
                    fail("bad-args", at + ": " + rowRef(model, row) + "'s table has no " + column, "", {}, at);
                }
                set.sets.push_back({row, hash, std::move(cell)});
            };

            const bool ground = op["on_ground"].asBool();
            const bool moves = op.has("pos") || op.has("zone_pos") || op.has("by") || ground;
            const bool align = op.has("align") ? op["align"].asBool() : ground;
            const bool turns = op.has("rot") || op.has("zone_rot") || op.has("up") || op.has("front") || op.has("turn") || align;
            if (moves || turns || op.has("scale")) {
                if (!model.hasTransform(row))
                    fail("bad-args", where + ": " + rowRef(model, row) + " has no transform", "", {}, where);
                const RowTransform t = model.transform(row);
                const Frame f = zoneFrame(model, zone);
                G::V3 world = toWorld(f, v3(t.pos));
                if (op.has("pos"))
                    world = parseVec(op["pos"], where + ".pos");
                if (op.has("zone_pos"))
                    world = toWorld(f, parseVec(op["zone_pos"], where + ".zone_pos"));
                if (op.has("by"))
                    world = world + parseVec(op["by"], where + ".by");
                // Gravity at the point: given ("down"), else the world's (approximate headless; see world.hpp).
                G::V3 down{0, -1, 0};
                std::string gravitySource = "down";
                if (op.has("down")) {
                    down = G::normalized(parseVec(op["down"], where + ".down"), {0, -1, 0});
                } else if (ground || align) {
                    const Gravity g = impl.world().gravityAt(model, world);
                    down = g.down;
                    gravitySource = g.source;
                }
                if (ground) {
                    // The ground below the point along gravity; from a little above it too, for points just under the surface.
                    float lifted = 0;
                    const Hit ground = groundBelow(impl.world(), model, world, down, gravitySource, where + ".on_ground", &lifted);
                    const Hit* hit = &ground;
                    const float height = op.has("height") ? static_cast< float >(op["height"].asNumber()) : 0.0f;
                    world = hit->point - down * height;
                    groundInfo = Json::Value::object();
                    groundInfo.set("ground", vecJson(hit->point, 1));
                    groundInfo.set("on", rowRef(model, hit->row));
                    groundInfo.set("gravity", gravitySource);
                    if (lifted > 0)
                        groundInfo.set("lifted", std::round(lifted * 10.0f) / 10.0);
                    groundInfo.set("ref", rowRef(model, row));
                    placements.push(groundInfo);
                }
                if (moves) {
                    const G::V3 local = toLocal(f, world);
                    put("pos_x", floatCell(local.x), where + ".pos");
                    put("pos_y", floatCell(local.y), where + ".pos");
                    put("pos_z", floatCell(local.z), where + ".pos");
                }
                if (turns) {
                    G::Basis basis = basisToWorld(f, G::fromEulerDegrees(v3(t.dirDeg)));
                    if (op.has("rot"))
                        basis = G::fromEulerDegrees(parseVec(op["rot"], where + ".rot"));
                    if (op.has("zone_rot"))
                        basis = basisToWorld(f, G::fromEulerDegrees(parseVec(op["zone_rot"], where + ".zone_rot")));
                    if (align && !op.has("up"))
                        basis = G::alignUp(basis, down * -1.0f);
                    if (op.has("up") || op.has("front")) {
                        const G::V3 up = op.has("up") ? G::normalized(parseVec(op["up"], where + ".up")) : basis.y;
                        const G::V3 front = op.has("front") ? parseVec(op["front"], where + ".front") : basis.z;
                        basis = G::gravityFrame(up, front);
                    }
                    if (op.has("turn"))
                        basis = G::rotate(basis, basis.y, static_cast< float >(op["turn"].asNumber()) * G::kDeg);
                    const G::V3 deg = G::toEulerDegrees(basisToLocal(f, basis));
                    put("dir_x", floatCell(deg.x), where + ".rot");
                    put("dir_y", floatCell(deg.y), where + ".rot");
                    put("dir_z", floatCell(deg.z), where + ".rot");
                }
                if (op.has("scale")) {
                    G::V3 s;
                    if (op["scale"].type() == Json::Value::Type::Number) {
                        const float k = static_cast< float >(op["scale"].asNumber());
                        s = {k, k, k};
                    } else {
                        s = parseVec(op["scale"], where + ".scale");
                    }
                    put("scale_x", floatCell(s.x), where + ".scale");
                    put("scale_y", floatCell(s.y), where + ".scale");
                    put("scale_z", floatCell(s.z), where + ".scale");
                }
            }

            const char* pName = model.objectName(row);
            const Catalog::ClassInfo* info = pName != nullptr ? Catalog::find(pName) : nullptr;
            for (const auto& [key, value] : op["args"].members()) {
                const std::string at = where + ".args." + key;
                const Catalog::Arg* arg = nullptr;
                int index = -1;
                if (info != nullptr) {
                    for (const Catalog::Arg& a : info->args) {
                        if (!a.name.empty() && lower(a.name) == lower(key))
                            arg = &a;
                    }
                }
                if (arg == nullptr) {
                    std::string k = lower(key);
                    if (k.rfind("obj_arg", 0) == 0)
                        k = k.substr(7);
                    if (!k.empty() && k.find_first_not_of("0123456789") == std::string::npos)
                        index = std::atoi(k.c_str());
                    if (info != nullptr) {
                        for (const Catalog::Arg& a : info->args) {
                            if (a.index == index)
                                arg = &a;
                        }
                    }
                } else {
                    index = arg->index;
                }
                if (index < 0 || index > 7) {
                    std::vector< std::string > names;
                    if (info != nullptr) {
                        for (const Catalog::Arg& a : info->args)
                            names.push_back(a.name.empty() ? "Obj_arg" + std::to_string(a.index) : a.name);
                    }
                    fail("bad-arg", at + ": " + (pName != nullptr ? std::string(pName) : "the row") + " has no arg '" + key + "'",
                         "args are named as in `class " + std::string(pName != nullptr ? pName : "?") + "`, or Obj_arg0..7", names, at);
                }
                std::int32_t v = -1;
                if (value.isNull() || (value.type() == Json::Value::Type::String && lower(value.asString()) == "default")) {
                    v = -1;
                } else if (value.type() == Json::Value::Type::Bool) {
                    v = value.asBool() ? 1 : -1;  // the game's bool getter: -1 is off, anything else on
                } else if (value.type() == Json::Value::Type::String) {
                    std::vector< std::string > labels;
                    bool found = false;
                    if (arg != nullptr) {
                        for (const Catalog::EnumChoice& c : arg->choices) {
                            labels.push_back(c.label);
                            if (lower(c.label) == lower(value.asString())) {
                                v = c.value;
                                found = true;
                            }
                        }
                    }
                    if (!found)
                        fail("bad-value", at + ": '" + value.asString() + "' is not a choice", "use one of the labels, or a number", labels, at);
                } else {
                    v = wholeNumber(value, at);
                }
                put(("Obj_arg" + std::to_string(index)).c_str(), intCell(v), at);
            }
            for (const auto& [key, value] : op["switches"].members()) {
                const std::string at = where + ".switches." + key;
                std::int32_t v = -1;
                if (value.type() == Json::Value::Type::String && lower(value.asString()) == "new") {
                    v = freeSwitch(zone);
                } else if (value.type() == Json::Value::Type::String) {
                    // "<ref>.<SW_FIELD>": the same switch as another row's (e.g. "@goomba.SW_DEAD").
                    const std::string text = value.asString();
                    const std::size_t dot = text.rfind('.');
                    if (dot == std::string::npos || lower(text.substr(dot + 1)).rfind("sw_", 0) != 0) {
                        fail("bad-value", at + ": '" + text + "' is not a switch",
                             "a number, null, \"new\" or \"<ref>.SW_FIELD\" (e.g. \"@goomba.SW_DEAD\")", {}, at);
                    }
                    const RowId other = impl.resolveRef(Json::Value(text.substr(0, dot)), at);
                    std::string field = text.substr(dot + 1);
                    for (char& ch : field)
                        ch = static_cast< char >(std::toupper(static_cast< unsigned char >(ch)));
                    const Cell* cell = model.cell(other, columnHash(field.c_str()));
                    if (cell == nullptr || cell->i < 0) {
                        fail("bad-value", at + ": " + text.substr(0, dot) + " has no " + field + " set", "give that row a switch first (\"new\")",
                             {}, at);
                    }
                    int otherZone = 0, otherTable = 0;
                    model.locate(other, &otherZone, &otherTable);
                    if (otherZone != zone && cell->i < 1000) {
                        fail("bad-value", at + ": zone switches (0-127) only connect rows of one zone",
                             "use a shared switch (1000-1127) across zones", {}, at);
                    }
                    v = cell->i;
                } else if (!value.isNull()) {
                    v = wholeNumber(value, at);
                }
                std::string column = key;
                if (lower(column).rfind("sw_", 0) != 0)
                    column = "SW_" + column;
                for (char& ch : column)
                    ch = static_cast< char >(std::toupper(static_cast< unsigned char >(ch)));
                if (!hasColumn(model, zone, table, columnHash(column.c_str()))) {
                    std::vector< std::string > have;
                    for (const Column& c : model.columns(zone, table)) {
                        const std::string n = columnLabel(c.hash);
                        if (n.rfind("SW_", 0) == 0)
                            have.push_back(n);
                    }
                    fail("bad-args", at + ": the table has no " + column, "", have, at);
                }
                put(column.c_str(), intCell(v), at);
                if (value.type() == Json::Value::Type::String && lower(value.asString()) == "new") {
                    Json::Value allocation = Json::Value::object();
                    allocation.set("ref", rowRef(model, row));
                    allocation.set("zone", model.zoneName(zone));
                    allocation.set("field", column);
                    allocation.set("id", v);
                    allocatedSwitches.push(std::move(allocation));
                }
            }
            for (const auto& [key, value] : op["links"].members()) {
                const std::string at = where + ".links." + key;
                put(key.c_str(), intCell(value.isNull() ? -1 : wholeNumber(value, at)), at);
            }
            for (const auto& [key, value] : op["fields"].members()) {
                const std::string at = where + ".fields." + key;
                Cell cell;
                std::string error;
                if (!parseCell(value, key, &model.columns(zone, table), &cell, &error))
                    fail("bad-value", at + ": " + error, "", {}, at);
                set.sets.push_back({row, columnHashOfName(key), std::move(cell)});
            }
            if (!set.sets.empty())
                apply(set, where);
        }

        // The lowest zone switch (0-127) no row of the zone uses.
        std::int32_t freeSwitch(int zone) {
            std::set< std::int32_t > used = reservedSwitches[zone];
            for (int t = 0; t < model.tableCount(zone); t++) {
                std::vector< std::uint32_t > columns;
                for (const Column& c : model.columns(zone, t)) {
                    if (columnLabel(c.hash).rfind("SW_", 0) == 0)
                        columns.push_back(c.hash);
                }
                model.forEachRow(zone, t, [&](RowId row) {
                    if (model.state(row) == RowState::Deleted)
                        return;
                    for (std::uint32_t h : columns) {
                        if (const Cell* c = model.cell(row, h))
                            used.insert(c->i);
                    }
                });
            }
            for (std::int32_t id = 0; id < 128; id++) {
                if (!used.count(id)) {
                    reservedSwitches[zone].insert(id);
                    return id;
                }
            }
            fail("capacity", "every zone switch (0-127) of " + std::string(model.zoneName(zone)) + " is used", "use a shared switch (1000-1127)");
        }

        Paths::Rail railOf(const Json::Value& ref, const std::string& where) {
            const RowId row = impl.resolveRef(ref, where);
            for (const auto& rail : Paths::collect(model))
                if (rail.row == row)
                    return rail;
            fail("bad-ref", where + ": the ref is not a path", "use a path ref from get/list", {}, where);
        }

        std::pair< Paths::Rail, Paths::Point > pointOf(RowId row, const std::string& where) {
            for (const auto& rail : Paths::collect(model)) {
                for (const auto& point : rail.points)
                    if (point.row == row)
                        return {rail, point};
            }
            fail("bad-ref", where + ": the ref is not a path point", "get the path to see its point refs", {}, where);
        }

        Vec3 pointPosition(const Json::Value& op, const Paths::Rail& rail, Vec3 current, const std::string& where) {
            const int modes = op.has("pos") + op.has("zone_pos") + op.has("by");
            if (modes != 1)
                fail("bad-args", where + ": give exactly one of pos, zone_pos or by", "pos/by are world space; zone_pos is zone-local",
                     {"pos", "zone_pos", "by"}, where);
            const Frame frame = zoneFrame(model, rail.zone);
            const G::V3 local = op.has("zone_pos") ?
                                    parseVec(op["zone_pos"], where + ".zone_pos") :
                                    toLocal(frame, op.has("pos") ? parseVec(op["pos"], where + ".pos") :
                                                                   toWorld(frame, v3(current)) + parseVec(op["by"], where + ".by"));
            return {local.x, local.y, local.z};
        }

        // The points of an array op (world space): line, arc, ring, grid, polyline.
        std::vector< G::V3 > arrayPoints(const Json::Value& op, const std::string& where) {
            const std::string shape = op.has("along") && !op.has("shape") ? "path" : lower(op["shape"].asString());
            if (op.has("along") && shape != "path" && shape != "polyline")
                fail("bad-args", where + ": along requires shape path", "omit shape or use path", {"path"}, where + ".shape");
            if (op.has("along") && op.has("count") && op.has("spacing"))
                fail("bad-args", where + ": give count or spacing, not both", "", {"count", "spacing"}, where);
            if (op.has("along") && op.has("count")) {
                const int n = wholeNumber(op["count"], where + ".count");
                if (n < 1 || n > 500)
                    fail("bad-value", where + ".count must be in 1..500", "", {}, where + ".count");
            }
            std::vector< G::V3 > out;
            auto count = [&](float length) {
                if (op.has("count"))
                    return std::max(1, op["count"].asInt());
                if (op.has("spacing"))
                    return std::max(2, static_cast< int >(std::floor(length / std::max(1.0, op["spacing"].asNumber()))) + 1);
                fail("bad-args", where + ": give \"count\" or \"spacing\"", "", {}, where);
            };
            // The up of the array's plane: given, else against gravity at `at`.
            auto upAt = [&](G::V3 at) {
                if (op.has("up"))
                    return G::normalized(parseVec(op["up"], where + ".up"));
                if (op.has("down"))
                    return G::normalized(parseVec(op["down"], where + ".down")) * -1.0f;
                return impl.world().gravityAt(model, at).down * -1.0f;
            };
            if (shape == "line" || shape == "arc") {
                // "start"/"end" (Python-friendly); "from"/"to" are accepted too.
                const char* startKey = op.has("start") ? "start" : "from";
                const char* endKey = op.has("end") ? "end" : "to";
                if (!op.has(startKey) || !op.has(endKey))
                    fail("bad-args", where + ": a " + shape + " needs \"start\" and \"end\"", "", {"start", "end"}, where);
                const G::V3 a = parseVec(op[startKey], where + "." + startKey), b = parseVec(op[endKey], where + "." + endKey);
                const int n = count(G::length(b - a));
                const float peak = shape == "arc" ? static_cast< float >(op.has("peak") ? op["peak"].asNumber() : 500.0) : 0.0f;
                const G::V3 up = shape == "arc" ? upAt((a + b) * 0.5f) : G::V3{0, 1, 0};
                for (int i = 0; i < n; i++) {
                    const float t = n == 1 ? 0.5f : static_cast< float >(i) / static_cast< float >(n - 1);
                    out.push_back(a + (b - a) * t + up * (4.0f * peak * t * (1.0f - t)));
                }
            } else if (shape == "ring" || shape == "circle") {
                const G::V3 c = parseVec(op["center"], where + ".center");
                const float r = static_cast< float >(op["radius"].asNumber(500));
                const int n = count(2.0f * G::kPi * r);
                const G::Basis frame = G::gravityFrame(upAt(c), op.has("front") ? parseVec(op["front"], where + ".front") : G::V3{0, 0, 1});
                const float start = static_cast< float >(op["start_angle"].asNumber(0)) * G::kDeg;
                for (int i = 0; i < n; i++) {
                    const float a = start + 2.0f * G::kPi * static_cast< float >(i) / static_cast< float >(n);
                    out.push_back(c + frame.x * (std::cos(a) * r) + frame.z * (std::sin(a) * r));
                }
            } else if (shape == "grid") {
                const G::V3 o = parseVec(op["origin"], where + ".origin");
                const Json::Value& size = op["size"];
                const int nx = size.isArray() && size.items().size() == 2 ? size.items()[0].asInt() : 3;
                const int nz = size.isArray() && size.items().size() == 2 ? size.items()[1].asInt() : 3;
                float sx = 200, sz = 200;
                if (op["spacing"].isArray() && op["spacing"].items().size() == 2) {
                    sx = static_cast< float >(op["spacing"].items()[0].asNumber());
                    sz = static_cast< float >(op["spacing"].items()[1].asNumber());
                } else if (op.has("spacing")) {
                    sx = sz = static_cast< float >(op["spacing"].asNumber());
                }
                const G::Basis frame = G::gravityFrame(upAt(o), op.has("front") ? parseVec(op["front"], where + ".front") : G::V3{0, 0, 1});
                for (int i = 0; i < nx; i++) {
                    for (int k = 0; k < nz; k++) {
                        // Centred on the origin.
                        out.push_back(o + frame.x * ((static_cast< float >(i) - (nx - 1) * 0.5f) * sx) +
                                      frame.z * ((static_cast< float >(k) - (nz - 1) * 0.5f) * sz));
                    }
                }
            } else if (shape == "path" || shape == "polyline") {
                std::vector< G::V3 > pts;
                bool closed = op["closed"].asBool();
                if (op.has("along")) {
                    if (op.has("points") || op.has("closed"))
                        fail("bad-args", where + ": along takes the existing rail's points and closed state", "omit points and closed", {},
                             where);
                    const Paths::Rail rail = railOf(op["along"], where + ".along");
                    if (rail.points.size() < 2)
                        fail("bad-ref", where + ": the rail needs at least two points", "", {}, where + ".along");
                    closed = rail.closed;
                    const Frame frame = zoneFrame(model, rail.zone);
                    pts.push_back(toWorld(frame, v3(rail.points.front().position)));
                    const std::size_t segments = rail.points.size() - (closed ? 0 : 1);
                    for (std::size_t k = 0; k < segments; ++k) {
                        for (int sample = 1; sample <= 256; ++sample) {
                            pts.push_back(
                                toWorld(frame, v3(Paths::evaluate(rail.points[k], rail.points[(k + 1) % rail.points.size()], sample / 256.0f))));
                        }
                    }
                } else {
                int i = 0;
                    for (const Json::Value& p : op["points"].items())
                        pts.push_back(parseVec(p, where + ".points[" + std::to_string(i++) + "]"));
                    if (closed && pts.size() > 2)
                        pts.push_back(pts.front());
                }
                if (pts.size() < 2)
                    fail("bad-args", where + ".points needs at least 2 points", "", {}, where + ".points");
                float length = 0;
                for (std::size_t k = 1; k < pts.size(); k++)
                    length += G::length(pts[k] - pts[k - 1]);
                const bool spaced = op.has("spacing") && !op.has("count");
                const double spacing = op["spacing"].asNumber();
                if (spaced && (op["spacing"].type() != Json::Value::Type::Number || !std::isfinite(spacing) || spacing <= 0))
                    fail("bad-value", where + ".spacing must be a positive finite number", "", {}, where + ".spacing");
                const double amount = spaced ? (closed ? std::ceil(length / spacing) : std::floor(length / spacing) + 1) : count(length);
                if (amount > 500)
                    fail("bad-value", where + ": at most 500 array objects are allowed", "increase spacing or reduce count", {}, where);
                const int n = std::max(1, static_cast< int >(amount));
                for (int j = 0; j < n; j++) {
                    // Spacing stays K units, without stretching it to meet the final endpoint.
                    float d = spaced ? static_cast< float >(j * spacing) :
                                       (n == 1 ? 0 : length * static_cast< float >(j) / static_cast< float >(closed ? n : n - 1));
                    std::size_t k = 1;
                    while (k < pts.size() - 1 && d > G::length(pts[k] - pts[k - 1])) {
                        d -= G::length(pts[k] - pts[k - 1]);
                        k++;
                    }
                    const float seg = std::max(G::length(pts[k] - pts[k - 1]), 1e-6f);
                    out.push_back(pts[k - 1] + (pts[k] - pts[k - 1]) * std::min(1.0f, d / seg));
                }
            } else {
                fail("bad-value", where + ".shape must be line, arc, ring, grid or path", "", {"line", "arc", "ring", "grid", "path"},
                     where + ".shape");
            }
            return out;
        }

        // CommonPath_ID of `row` to the rail `railRef` (Paths::linkPath: same zone, 2+ points).
        void linkRail(RowId row, const Json::Value& railRef, const std::string& where) {
            const RowId railRow = impl.resolveRef(railRef, where);
            for (const Paths::Rail& rail : Paths::collect(model)) {
                if (rail.row != railRow)
                    continue;
                std::string error;
                const Op op = Paths::linkPath(model, row, rail, &error);
                if (op.sets.empty()) {
                    fail("bad-ref", where + ": " + error, "a rail links objects of its own zone: make the path with the object's \"zone\"", {},
                         where);
                }
                apply(op, where);
                return;
            }
            fail("bad-ref", where + ": " + rowRef(model, railRow) + " is not a path (a jmp/path/commonpathinfo row)", "make one with the path op",
                 {}, where);
        }

        // A Launch or Sling Star flies its rail from where it is placed but draws facing its own rotation: face it
        // along the rail's first segment, as the core check's fix does (launch-star-facing: pitch and yaw, no roll).
        void facePath(RowId row, const std::string& where) {
            const char* pName = model.objectName(row);
            const std::string name = pName != nullptr ? pName : "";
            if (name.rfind("SuperSpinDriver", 0) != 0 && name != "SpinDriver")
                return;
            const Cell* rail = model.cell(row, columnHash("CommonPath_ID"));
            int zone = 0, table = 0;
            model.locate(row, &zone, &table);
            for (const Paths::Rail& r : Paths::collect(model)) {
                if (rail == nullptr || r.zone != zone || r.id != rail->i || r.points.size() < 2)
                    continue;
                const Vec3 a = r.points[0].position, b = r.points[1].position;
                G::V3 t{b.x - a.x, b.y - a.y, b.z - a.z};
                if (G::length(t) < 0.001f)
                    return;
                t = G::normalized(t);
                const float pitch = -std::asin(std::clamp(t.y, -1.0f, 1.0f)) / G::kDeg;
                const float yaw = std::atan2(t.x, t.z) / G::kDeg;
                Op face;
                face.label = "Face along the path";
                face.sets.push_back({row, columnHash("dir_x"), floatCell(pitch)});
                face.sets.push_back({row, columnHash("dir_y"), floatCell(yaw)});
                face.sets.push_back({row, columnHash("dir_z"), floatCell(0.0f)});
                apply(face, where);
                return;
            }
        }

        void label(RowId row, const Json::Value& op, const std::string& where) {
            if (!op.has("as"))
                return;
            std::string name = op["as"].asString();
            if (!name.empty() && name[0] == '@')
                name = name.substr(1);
            if (name.empty())
                fail("bad-value", where + ".as must be a name", "", {}, where + ".as");
            impl.labels[name] = row;
        }

        RowId addRow(int zone, const std::string& table, RowId copyOf, const std::string& className, const std::string& where) {
            AddRow add;
            add.id = newRowIdFor(impl.nextNew++);
            add.zone = zone;
            add.table = table;
            add.copyOf = copyOf;
            if (!className.empty()) {
                Cell name;
                name.type = CellType::StringOffset;
                name.s = className;
                add.cells.push_back({columnHash("name"), name});
            }
            Op op;
            op.label = "add";
            op.adds.push_back(add);
            apply(op, where);
            created.push_back(add.id);
            return add.id;
        }

        void run(const Json::Value& op, const std::string& where) {
            const std::string kind = lower(op["op"].asString());
            if (kind == "add") {
                checkKeys(op, with({"op", "class", "zone", "layer", "missions", "like", "preset", "as", "force", "path", "face_path"}, kEditKeys), where);
                if (!op.has("class") && !op.has("like"))
                    fail("bad-args", where + ": add needs \"class\" (or \"like\": a row to copy)", "", {}, where);
                const Catalog::ClassInfo* info =
                    op.has("class") ? impl.resolveClass(op["class"].asString(), !op["force"].asBool(), where + ".class") : nullptr;
                RowId like;
                int likeZone = 0, likeTable = 0;
                if (op.has("like")) {
                    like = impl.resolveRef(op["like"], where + ".like");
                    model.locate(like, &likeZone, &likeTable);
                }
                int zone = op.has("zone") ? zoneOf(op["zone"], where + ".zone") : (like.valid() ? likeZone : 0);
                if (!op.has("zone") && op["on_ground"].asBool() && op.has("pos")) {
                    // As the Browser places: in the zone that owns the ground below (if it is placed).
                    const G::V3 p = parseVec(op["pos"], where + ".pos");
                    const G::V3 down =
                        op.has("down") ? G::normalized(parseVec(op["down"], where + ".down"), {0, -1, 0}) : impl.world().gravityAt(model, p).down;
                    const Hit hit = groundBelow(impl.world(), model, p, down, "the gravity there", where + ".on_ground");
                    int hitZone = 0, hitTable = 0;
                    if (model.locate(hit.row, &hitZone, &hitTable))
                        zone = hitZone;
                }
                std::string table;
                if (like.valid() && !op.has("layer") && !op.has("missions") && !op.has("class")) {
                    table = model.tablePath(likeZone, likeTable);
                } else {
                    const std::string layer = layerTableName(layerFor(op, zone, where));
                    table = info != nullptr ? Catalog::tablePath(*info, layer) : "jmp/placement/" + layer + "/objinfo";
                }
                if (!op.has("pos") && !op.has("zone_pos") && !like.valid()) {
                    fail("bad-args", where + ": add needs \"pos\" (world) or \"zone_pos\"",
                         "placement helpers (\"on_ground\") come with the place op", {}, where);
                }
                const std::string className = info != nullptr ? info->name : (op.has("class") ? op["class"].asString() : "");
                const RowId row = addRow(zone, table, like, className, where);
                Json::Value edits = op;
                if (info != nullptr && op.has("preset")) {
                    const Catalog::Preset* preset = nullptr;
                    std::vector< std::string > names;
                    for (const Catalog::Preset& p : info->presets) {
                        names.push_back(p.label);
                        if (lower(p.label) == lower(op["preset"].asString()))
                            preset = &p;
                    }
                    if (preset == nullptr)
                        fail("bad-value", where + ".preset: no preset '" + op["preset"].asString() + "'", "", names, where + ".preset");
                    Json::Value args = Json::Value::object();
                    for (const auto& [index, value] : preset->args)
                        args.set("Obj_arg" + std::to_string(index), value);
                    for (const auto& [k, v] : op["args"].members())
                        args.set(k, v);
                    edits.set("args", std::move(args));
                }
                edit(row, edits, where);
                if (op.has("path")) {
                    linkRail(row, op["path"], where + ".path");
                    // Facing the path is the default unless the op orients the star itself.
                    const bool oriented = op.has("rot") || op.has("zone_rot") || op.has("up") || op.has("front") || op.has("turn");
                    if (op.has("face_path") ? op["face_path"].asBool() : !oriented)
                        facePath(row, where + ".path");
                }
                label(row, op, where);
            } else if (kind == "gravity-shape" || kind == "gravity-values") {
                checkKeys(op, kind == "gravity-shape" ? std::vector<std::string>{"op", "ref", "refs", "select", "shape", "bound"} :
                                                        std::vector<std::string>{"op", "ref", "refs", "select", "values"}, where);
                const auto rows = impl.resolveTargets(op, where);
                for (RowId row : rows) {
                    if (EditorGravity::shapeForPlacement(model.objectName(row)) == EditorGravity::Shape::Unknown)
                        fail("bad-ref", where + ": target is not a gravity field", "select category Gravity", {}, where);
                }
                if (kind == "gravity-shape") {
                    EditorGravity::Shape shape = EditorGravity::Shape::Unknown;
                    std::vector<std::string> names;
                    for (int i = 0; i < 8; ++i) {
                        const auto candidate = static_cast<EditorGravity::Shape>(i);
                        names.push_back(EditorGravity::shapeName(candidate));
                        if (lower(names.back()) == lower(op["shape"].asString())) shape = candidate;
                    }
                    if (shape == EditorGravity::Shape::Unknown)
                        fail("bad-value", where + ".shape is not a gravity shape", "", names, where + ".shape");
                    const std::string boundName = op.has("bound") ? lower(op["bound"].asString()) : "sphere";
                    if (boundName != "sphere" && boundName != "box" && boundName != "cylinder")
                        fail("bad-value", where + ".bound must be Sphere, Box or Cylinder", "", {"Sphere", "Box", "Cylinder"}, where + ".bound");
                    if (op.has("bound") && shape != EditorGravity::Shape::Parallel)
                        fail("bad-args", where + ": bound applies to Parallel fields", "omit bound", {}, where + ".bound");
                    const auto bound = boundName == "box" ? EditorGravity::Bound::Box : boundName == "cylinder" ? EditorGravity::Bound::Cylinder : EditorGravity::Bound::Sphere;
                    for (RowId row : rows) {
                        const Op change = EditorGravity::shapeOp(model, row, shape, bound);
                        if (change.sets.empty())
                            fail("op-failed", where + ": gravity shape could not be changed", "Wire requires a linked rail with at least two points", {}, where);
                        apply(change, where);
                    }
                } else {
                    if (!op["values"].isObject() || op["values"].members().empty())
                        fail("bad-args", where + ".values must be a nonempty object", "use Range, Distant, Priority, Inverse or Obj_arg0..7", {}, where + ".values");
                    for (const auto& [column, value] : op["values"].members()) {
                        const bool arg = column.size() == 8 && column.rfind("Obj_arg", 0) == 0 && column.back() >= '0' && column.back() <= '7';
                        if (column != "Range" && column != "Distant" && column != "Priority" && column != "Inverse" && !arg)
                            fail("bad-arg", where + ": unknown gravity value " + column, "named catalog args can also be changed with set.args", {"Range", "Distant", "Priority", "Inverse", "Obj_arg0"}, where + ".values." + column);
                        const bool integer = column == "Priority" || column == "Inverse" || arg;
                        const double number = value.type() == Json::Value::Type::Bool && column == "Inverse" ? (value.asBool() ? 1 : 0) : value.asNumber();
                        if ((value.type() != Json::Value::Type::Number && !(column == "Inverse" && value.type() == Json::Value::Type::Bool)) ||
                            !std::isfinite(number) || std::fabs(number) > 10000000 || (integer && number != std::floor(number)))
                            fail("bad-value", where + ".values." + column + " must be a finite " + (integer ? "integer" : "number") + " within +/-10000000", "", {}, where + ".values." + column);
                        if (column == "Inverse" && number != 0 && number != 1)
                            fail("bad-value", where + ".values.Inverse must be 0, 1 or boolean", "", {}, where + ".values.Inverse");
                        apply(EditorGravity::numericOp(model, rows, column.c_str(), static_cast<float>(number), integer), where);
                    }
                }
            } else if (kind == "path") {
                checkKeys(op, {"op", "zone", "points", "closed", "smooth", "on_ground", "height", "down", "as"}, where);
                const int zone = op.has("zone") ? zoneOf(op["zone"], where + ".zone") : 0;
                const Frame f = zoneFrame(model, zone);
                struct P {
                    G::V3 pos, in, out;
                    bool handles = false;
                };
                std::vector< P > pts;
                int i = 0;
                for (const Json::Value& v : op["points"].items()) {
                    const std::string at = where + ".points[" + std::to_string(i++) + "]";
                    P p;
                    if (v.isObject()) {
                        p.pos = parseVec(v["pos"], at + ".pos");
                        if (v.has("in") || v.has("out")) {
                            p.in = v.has("in") ? parseVec(v["in"], at + ".in") : p.pos;
                            p.out = v.has("out") ? parseVec(v["out"], at + ".out") : p.pos;
                            p.handles = true;
                        }
                    } else {
                        p.pos = parseVec(v, at);
                    }
                    if (op["on_ground"].asBool()) {
                        const G::V3 down = op.has("down") ? G::normalized(parseVec(op["down"], where + ".down"), {0, -1, 0}) :
                                                            impl.world().gravityAt(model, p.pos).down;
                        const Hit hit = groundBelow(impl.world(), model, p.pos, down, "the gravity there", at);
                        const G::V3 moved = hit.point - down * static_cast< float >(op["height"].asNumber(0));
                        p.in = p.in + (moved - p.pos);
                        p.out = p.out + (moved - p.pos);
                        p.pos = moved;
                    }
                    pts.push_back(p);
                }
                if (pts.size() < 2)
                    fail("bad-args", where + ".points needs at least 2 points", "", {}, where + ".points");
                const bool closed = op["closed"].asBool();
                const bool smooth = op.has("smooth") ? op["smooth"].asBool() : true;
                std::vector< Paths::Point > points;
                for (std::size_t k = 0; k < pts.size(); k++) {
                    P p = pts[k];
                    if (!p.handles) {
                        p.in = p.out = p.pos;
                        if (smooth) {
                            // Catmull-Rom handles: a sixth of the neighbours' span either side.
                            const std::size_t n = pts.size();
                            const G::V3 prev = k > 0 ? pts[k - 1].pos : (closed ? pts[n - 1].pos : p.pos);
                            const G::V3 next = k + 1 < n ? pts[k + 1].pos : (closed ? pts[0].pos : p.pos);
                            const G::V3 t = (next - prev) * (1.0f / 6.0f);
                            p.in = p.pos - t;
                            p.out = p.pos + t;
                        }
                    }
                    auto local = [&](G::V3 w) {
                        const G::V3 l = toLocal(f, w);
                        return Vec3{l.x, l.y, l.z};
                    };
                    Paths::Point point;
                    point.row = newRowIdFor(impl.nextNew++);
                    point.position = local(p.pos);
                    point.incoming = local(p.in);
                    point.outgoing = local(p.out);
                    points.push_back(point);
                }
                const RowId railRow = newRowIdFor(impl.nextNew++);
                std::string error;
                const Op railOp = Paths::addRail(model, zone, points, closed, railRow, &error);
                if (railOp.adds.empty())
                    fail("op-failed", where + ": " + error, "", {}, where);
                apply(railOp, where);
                created.push_back(railRow);
                label(railRow, op, where);
            } else if (kind == "move-point" || kind == "move-handle" || kind == "delete-point") {
                checkKeys(op, {"op", "ref", "refs", "select", "pos", "zone_pos", "by", "handle"}, where);
                if (kind == "delete-point" && (op.has("pos") || op.has("zone_pos") || op.has("by") || op.has("handle")))
                    fail("bad-args", where + ": delete-point takes only point refs", "", {}, where);
                if (kind != "move-handle" && op.has("handle"))
                    fail("bad-args", where + ": handle belongs to move-handle", "", {}, where);
                const auto rows = impl.resolveTargets(op, where);
                if (rows.size() > 1 && (op.has("pos") || op.has("zone_pos")))
                    fail("bad-args", where + ": an absolute position would stack the points", "use by for multiple points", {}, where);
                for (RowId row : rows) {
                    const auto [rail, point] = pointOf(row, where);
                    if (kind == "move-point") {
                        apply(Paths::movePoint(point, pointPosition(op, rail, point.position, where)), where);
                    } else if (kind == "move-handle") {
                        const std::string handle = lower(op["handle"].asString());
                        if (handle != "in" && handle != "out")
                            fail("bad-value", where + ".handle must be in or out", "", {"in", "out"}, where + ".handle");
                        apply(Paths::moveHandle(point, handle == "out",
                                                pointPosition(op, rail, handle == "out" ? point.outgoing : point.incoming, where)),
                              where);
                    } else {
                        std::string error;
                        const Op deletion = Paths::deletePoint(rail, row, &error);
                        if (deletion.deletes.empty())
                            fail("op-failed", where + ": " + error, "a path must keep at least two points", {}, where);
                        apply(deletion, where);
                    }
                }
            } else if (kind == "insert-point") {
                checkKeys(op, {"op", "path", "segment", "t", "as"}, where);
                const Paths::Rail rail = railOf(op["path"], where + ".path");
                const int segment = wholeNumber(op["segment"], where + ".segment");
                const double t = op["t"].asNumber(0.5);
                if ((op.has("t") && op["t"].type() != Json::Value::Type::Number) || !std::isfinite(t) || t < 0.01 || t > 0.99)
                    fail("bad-value", where + ".t must be in 0.01..0.99", "the canonical path helper uses this interval", {}, where + ".t");
                const RowId fresh = newRowIdFor(impl.nextNew++);
                std::string error;
                const Op insertion = Paths::insertPoint(rail, segment, t, fresh, &error);
                if (insertion.adds.empty())
                    fail("op-failed", where + ": " + error, "segment is the zero-based start point of a segment", {}, where + ".segment");
                apply(insertion, where);
                created.push_back(fresh);
                label(fresh, op, where);
            } else if (kind == "link") {
                checkKeys(op, {"op", "ref", "refs", "select", "path", "face_path"}, where);
                if (!op.has("path"))
                    fail("bad-args", where + ": link needs \"path\": a path ref", "", {}, where);
                for (RowId row : impl.resolveTargets(op, where)) {
                    linkRail(row, op["path"], where + ".path");
                    if (op["face_path"].asBool())
                        facePath(row, where + ".path");
                }
            } else if (kind == "array" || kind == "ground_array") {
                checkKeys(op,
                          with({"op",          "class",  "zone",  "layer",  "missions", "like",  "preset",  "as",     "force",
                                "shape",       "start",  "end",   "from",   "to",       "count", "spacing", "center", "radius",
                                "start_angle", "points", "along", "origin", "size",     "peak",  "closed",  "rot",    "face_along"},
                                   {"args", "switches", "links", "fields", "on_ground", "height", "down", "align", "up", "front", "turn", "scale"}),
                          where);
                const std::vector< G::V3 > points = arrayPoints(op, where);
                if (points.size() > 500)
                    fail("bad-value", where + ": " + std::to_string(points.size()) + " objects is too many for one array (500)", "", {}, where);
                if (kind == "ground_array" && op.has("on_ground") && !op["on_ground"].asBool())
                    fail("bad-args", where + ": ground_array always snaps to ground", "use array without on_ground", {}, where + ".on_ground");
                const bool snap = kind == "ground_array" || op["on_ground"].asBool();
                const double height = op["height"].asNumber(0);
                if (snap && ((op.has("height") && op["height"].type() != Json::Value::Type::Number) || !std::isfinite(height)))
                    fail("bad-value", where + ".height must be finite", "", {}, where + ".height");
                // Resolve every ground hit before creating any row. Earlier copies cannot
                // become the ground for later copies, including overlapping brush arrays.
                std::vector<G::V3> placed = points, downs;
                std::vector<Hit> hits;
                std::vector<Json::Value> probeInfo;
                for (std::size_t i = 0; snap && i < points.size(); ++i) {
                    const std::string at = where + "[" + std::to_string(i) + "]";
                    const auto gravity = impl.world().gravityAt(model, points[i]);
                    const G::V3 requestedDown = op.has("down") ? parseVec(op["down"], at + ".down") : gravity.down;
                    if (G::length(requestedDown) < 1e-6f)
                        fail("bad-value", at + ".down must be nonzero", "", {}, at + ".down");
                    const G::V3 down = G::normalized(requestedDown);
                    float lifted = 0;
                    const Hit hit = groundBelow(impl.world(), model, points[i], down, gravity.source, at + ".on_ground", &lifted);
                    placed[i] = hit.point - down * static_cast<float>(height);
                    downs.push_back(down);
                    hits.push_back(hit);
                    Json::Value detail = Json::Value::object();
                    detail.set("ground", vecJson(hit.point, 1));
                    detail.set("on", rowRef(model, hit.row));
                    detail.set("gravity", op.has("down") ? "given" : gravity.source);
                    if (lifted > 0) detail.set("lifted", std::round(lifted * 10) / 10.0);
                    probeInfo.push_back(std::move(detail));
                }
                const std::string as = op["as"].asString();
                for (std::size_t i = 0; i < points.size(); i++) {
                    Json::Value add = Json::Value::object();
                    for (const auto& [k, v] : op.members()) {
                        static const std::set< std::string > kShape = {"shape",   "start",  "end",    "from",        "to",     "count",
                                                                       "spacing", "center", "radius", "start_angle", "points", "along",
                                                                       "origin",  "size",   "peak",   "closed",      "as",     "face_along"};
                        if (!kShape.count(k))
                            add.set(k, v);
                    }
                    add.set("op", "add");
                    add.set("pos", vecJson(placed[i], 3));
                    if (snap) {
                        add.set("on_ground", false);
                        add.set("align", false);
                        if ((!op.has("align") || op["align"].asBool()) && !op.has("up"))
                            add.set("up", vecJson(downs[i] * -1.0f, 6));
                        if (!op.has("zone")) {
                            int zone = 0, table = 0;
                            model.locate(hits[i].row, &zone, &table);
                            add.set("zone", zone);
                        }
                    }
                    if (op["face_along"].asBool() && points.size() > 1) {
                        const G::V3 next = i + 1 < placed.size() ? placed[i + 1] - placed[i] : placed[i] - placed[i - 1];
                        add.set("front", vecJson(next, 4));
                    }
                    if (!as.empty())
                        add.set("as", as + "." + std::to_string(i));
                    run(add, where + "[" + std::to_string(i) + "]");
                    if (snap) {
                        probeInfo[i].set("ref", rowRef(model, created.back()));
                        placements.push(std::move(probeInfo[i]));
                    }
                }
            } else if (kind == "set" || kind == "move" || kind == "rotate" || kind == "scale" || kind == "transform") {
                checkKeys(op, with({"op", "ref", "refs", "select"}, kEditKeys), where);
                const std::vector< RowId > rows = impl.resolveTargets(op, where);
                if (rows.size() > 1 && (op.has("pos") || op.has("zone_pos"))) {
                    fail("bad-args", where + ": \"pos\" on " + std::to_string(rows.size()) + " rows would stack them",
                         "use \"by\" to move several rows", {}, where);
                }
                for (RowId row : rows)
                    edit(row, op, where);
            } else if (kind == "delete") {
                checkKeys(op, {"op", "ref", "refs", "select"}, where);
                Op del;
                del.label = "delete";
                del.deletes = impl.resolveTargets(op, where);
                apply(del, where);
            } else if (kind == "duplicate") {
                checkKeys(op, with({"op", "ref", "as", "count"}, kEditKeys), where);
                const RowId source = impl.resolveRef(op["ref"], where + ".ref");
                int zone = 0, table = 0;
                model.locate(source, &zone, &table);
                const RowId row = addRow(zone, model.tablePath(zone, table), source, "", where);
                edit(row, op, where);
                label(row, op, where);
            } else if (kind == "revert") {
                checkKeys(op, {"op", "ref", "refs", "select"}, where);
                Op revert;
                revert.label = "revert";
                revert.revertRows = impl.resolveTargets(op, where);
                apply(revert, where);
            } else if (kind == "fix") {
                checkKeys(op, {"op", "check"}, where);
                const std::string key = op["check"].asString();
                std::vector< std::string > keys;
                for (const Check& c : computeChecks(model)) {
                    keys.push_back(c.key);
                    if (c.key != key)
                        continue;
                    if (c.fix.adds.empty() && c.fix.sets.empty() && c.fix.deletes.empty() && c.fix.revertRows.empty()) {
                        fail("no-fix", where + ": check " + key + " has no automatic fix",
                             c.fixKind.empty() ? "edit the rows it names" : "it needs a choice (" + c.fixKind + "): set the field yourself", {},
                             where);
                    }
                    apply(c.fix, where);
                    return;
                }
                fail("bad-ref", where + ": no current check '" + key + "'", "run `checks`", keys, where + ".check");
            } else if (kind == "label") {
                checkKeys(op, {"op", "ref", "as"}, where);
                label(impl.resolveRef(op["ref"], where + ".ref"), op, where);
            } else if (kind == "raw") {
                checkKeys(op, {"op", "edit"}, where);
                Op raw;
                std::string error;
                if (!opFromJson(model, op["edit"], &raw, &error))
                    fail("bad-args", where + ".edit: " + error, "", {}, where + ".edit");
                for (const AddRow& add : raw.adds) {
                    impl.nextNew = std::max(impl.nextNew, (add.id.v & ~(3ull << 62)) + 1);
                    created.push_back(add.id);
                }
                apply(raw, where);
            } else {
                std::vector< std::string > kinds;
                for (const CommandSpec& s : opKinds())
                    kinds.push_back(s.name);
                fail("bad-op", where + ": unknown op '" + op["op"].asString() + "'", "`help --ops` lists them", kinds, where + ".op");
            }
        }
    };
}  // namespace

Json::Value Level::Impl::applyBatch(const Json::Value& args, bool dryRun) {
    const Json::Value& ops = args.has("ops") ? args["ops"] : args;
    if (!ops.isArray())
        fail("bad-args", "apply needs \"ops\": [op, ...]", "e.g. {\"ops\": [{\"op\": \"add\", \"class\": \"Coin\", \"pos\": [0, 500, 0]}]}");
    const std::string key = args["key"].asString();
    Json::Value out = Json::Value::object();
    if (!key.empty()) {
        for (int i = 0; i < model->historyCursor(); i++) {
            if (entries[i].key == key) {
                out.set("skipped", true);
                out.set("reason", "a batch with key '" + key + "' is already applied (journal " + std::to_string(i) + ")");
                out.set("journal", i);
                return out;
            }
        }
    }
    const Snapshot before = snapshot(*model);
    const std::vector< Check > checksBefore = newChecks();
    const auto labelsBefore = labels;
    const std::uint64_t nextBefore = nextNew;
    Batch batch{*this, *model};
    auto rollback = [&]() {
        for (int i = 0; i < batch.steps; i++)
            model->undo();
        labels = labelsBefore;
        nextNew = nextBefore;
    };
    try {
        int i = 0;
        for (const Json::Value& op : ops.items()) {
            const std::string where = "ops[" + std::to_string(i++) + "]";
            if (!op.isObject() || !op.has("op"))
                fail("bad-args", where + " must be an object with \"op\"", "", {}, where);
            batch.run(op, where);
        }
    } catch (const Failure&) {
        rollback();
        throw;
    }
    const Snapshot after = snapshot(*model);
    std::string label = args["label"].asString();
    if (label.empty()) {
        label = ops.items().size() == 1 ? ops.items()[0]["op"].asString() : std::to_string(ops.items().size()) + " ops";
        if (ops.items().size() == 1 && ops.items()[0].has("class"))
            label += " " + ops.items()[0]["class"].asString();
    }
    const Op merged = mergedOp(*model, before, after, label);
    const Json::Value opJson = opToJson(*model, merged);

    Json::Value created = Json::Value::array();
    for (RowId row : batch.created) {
        if (after.count(row))
            created.push(objectJson(row, false));
    }
    Json::Value changed = Json::Value::array();
    std::set< RowId > seen;
    for (const SetCell& s : merged.sets) {
        if (seen.insert(s.row).second)
            changed.push(rowRef(*model, s.row));
    }
    Json::Value deleted = Json::Value::array();
    for (RowId row : merged.deletes)
        deleted.push(rowRef(*model, row));
    // Checks this batch introduced and resolved.
    const std::vector< Check > checksAfter = newChecks();
    Json::Value introduced = Json::Value::array();
    for (const Check& c : checksAfter) {
        bool had = false;
        for (const Check& b : checksBefore)
            had = had || b.key == c.key;
        if (had)
            continue;
        Json::Value j = Json::Value::object();
        j.set("key", c.key);
        j.set("severity", c.severity == Severity::Error ? "error" : "warning");
        j.set("message", c.message);
        if (!c.fixLabel.empty())
            j.set("fix", c.fixLabel);
        introduced.push(std::move(j));
    }
    Json::Value resolved = Json::Value::array();
    for (const Check& b : checksBefore) {
        bool still = false;
        for (const Check& c : checksAfter)
            still = still || b.key == c.key;
        if (!still)
            resolved.push(b.key);
    }

    // One undo step: back out the steps, then apply the merged op.
    for (int i = 0; i < batch.steps; i++)
        model->undo();
    const bool empty = merged.adds.empty() && merged.sets.empty() && merged.deletes.empty();
    if (dryRun || empty) {
        if (dryRun) {
            labels = labelsBefore;
            nextNew = nextBefore;
        }
        out.set("dry_run", dryRun);
        if (empty)
            out.set("no_change", true);
    } else {
        std::string error;
        if (!model->apply(merged, &error)) {
            labels = labelsBefore;
            nextNew = nextBefore;
            fail("internal", "the merged op failed: " + error, "please report this with the batch");
        }
        entries.resize(model->historyCursor() - 1);
        entries.push_back(JournalEntry{key, label, opJson});
        out.set("journal", model->historyCursor() - 1);
    }
    out.set("label", label);
    out.set("created", std::move(created));
    out.set("changed", std::move(changed));
    out.set("deleted", std::move(deleted));
    if (!batch.placements.items().empty())
        out.set("placements", batch.placements);
    if (!batch.allocatedSwitches.items().empty()) out.set("allocated_switches", batch.allocatedSwitches);
    out.set("checks_introduced", std::move(introduced));
    out.set("checks_resolved", std::move(resolved));
    if (args["show_op"].asBool())
        out.set("op", opJson);
    return out;
}

}  // namespace PetariEditor::LevelApi
