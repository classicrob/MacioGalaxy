// Batches (level_api.hpp): high-level ops applied one by one to the EditModel, then merged into ONE op
// (one undo step, one journal entry), all or nothing.
#include "level_internal.hpp"

#include "checks.hpp"
#include "world.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace PetariEditor::LevelApi {

namespace {
    // ---- snapshots and the merged op ----------------------------------------------------------------

    struct RowSnap {
        int zone = 0, table = 0, index = 0;
        std::vector< std::pair< std::uint32_t, Cell > > cells;
    };
    using Snapshot = std::map< RowId, RowSnap >;

    bool sameCell(const Cell& a, const Cell& b) {
        if (a.type != b.type) return false;
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
                    if (model.state(row) == RowState::Deleted) return;
                    RowSnap s;
                    s.zone = z;
                    s.table = t;
                    s.index = index++;
                    for (const Column& c : model.columns(z, t)) {
                        if (const Cell* cell = model.cell(row, c.hash)) s.cells.push_back({c.hash, *cell});
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
                    if (h == hash) old = &c;
                }
                if (old == nullptr || !sameCell(*old, cell)) op.sets.push_back({row, hash, cell});
            }
        }
        std::sort(added.begin(), added.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
        for (const auto& [where, row] : added) {
            const RowSnap& a = after.at(row);
            AddRow add;
            add.id = row;
            add.zone = a.zone;
            add.table = model.tablePath(a.zone, a.table);
            for (const auto& [hash, cell] : a.cells) add.cells.push_back({hash, cell});
            op.adds.push_back(std::move(add));
        }
        for (const auto& [row, b] : before) {
            if (!after.count(row)) op.deletes.push_back(row);
        }
        return op;
    }

    // ---- helpers -------------------------------------------------------------------------------

    void checkKeys(const Json::Value& op, const std::vector< std::string >& allowed, const std::string& where) {
        for (const auto& [key, value] : op.members()) {
            if (std::find(allowed.begin(), allowed.end(), key) != allowed.end()) continue;
            std::vector< std::string > close;
            for (const std::string& a : allowed) {
                if (lower(a).find(lower(key).substr(0, 3)) != std::string::npos || lower(key).find(lower(a)) != std::string::npos) close.push_back(a);
            }
            fail("bad-args", where + ": unknown key '" + key + "'", "allowed: see `help --op " + op["op"].asString() + "`", close.empty() ? allowed : close,
                 where + "." + key);
        }
    }

    const std::vector< std::string > kEditKeys = {"pos", "zone_pos", "by", "rot", "zone_rot", "up", "front", "turn", "scale",
                                                  "args", "switches", "links", "fields", "on_ground", "height", "down", "align"};

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
            if (c.hash == hash) return true;
        }
        return false;
    }

    std::int32_t wholeNumber(const Json::Value& v, const std::string& where) {
        const double n = v.asNumber();
        if (v.type() != Json::Value::Type::Number || n != std::floor(n)) fail("bad-value", where + " must be a whole number", "", {}, where);
        return static_cast< std::int32_t >(n);
    }

    std::string layerTableName(const std::string& letter) {
        return letter == "common" ? "common" : "layer" + letter;
    }
}  // namespace

std::vector< Check > Level::Impl::newChecks() const {
    std::vector< Check > out;
    for (Check& c : computeChecks(*model)) {
        if (!baseline.count(c.key)) out.push_back(std::move(c));
    }
    return out;
}

Json::Value Level::Impl::checksJson(bool all) const {
    Json::Value list = Json::Value::array();
    int errors = 0, warnings = 0;
    for (const Check& c : computeChecks(*model)) {
        const bool known = baseline.count(c.key) != 0;
        if (known && !all) continue;
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
        if (!c.fixLabel.empty()) j.set("fix", c.fixLabel);
        if (!c.fixKind.empty()) j.set("fix_kind", c.fixKind);
        if (all) j.set("shipped", known);
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

        void apply(const Op& op, const std::string& where) {
            std::string error;
            if (!model.apply(op, &error)) fail("op-failed", where + ": " + error, "", {}, where);
            steps++;
        }

        int zoneOf(const Json::Value& v, const std::string& where) {
            const std::string want = v.type() == Json::Value::Type::Number ? std::to_string(v.asInt()) : v.asString();
            std::vector< std::string > zones;
            for (int z = 0; z < model.zoneCount(); z++) {
                if (lower(model.zoneName(z)) == lower(want) || std::to_string(z) == want) return z;
                zones.push_back(model.zoneName(z));
            }
            fail("bad-value", where + ": no zone '" + want + "'", "", zones, where);
        }

        // "common", "a".."p", "layera"; or the missions a layer must be active in (exactly).
        std::string layerFor(const Json::Value& op, int zone, const std::string& where) {
            if (op.has("layer")) {
                std::string l = lower(op["layer"].asString());
                if (l.rfind("layer", 0) == 0) l = l.substr(5);
                if (l == "common" || (l.size() == 1 && l[0] >= 'a' && l[0] <= 'p')) return l;
                fail("bad-value", where + ".layer must be \"common\" or a letter a-p", "", {"common", "a", "b", "c"}, where + ".layer");
            }
            if (op.has("missions")) {
                std::uint32_t want = 0;
                for (const Json::Value& m : op["missions"].items()) {
                    const int n = m.asInt();
                    if (n < 1 || n > model.missionCount()) fail("bad-value", where + ".missions: no mission " + std::to_string(n), "", {}, where + ".missions");
                    want |= 1u << (n - 1);
                }
                const std::uint32_t everyMission = model.missionCount() >= 32 ? ~0u : (1u << model.missionCount()) - 1;
                if (want == everyMission) return "common";
                std::vector< std::string > candidates;
                for (int layer = 1; layer <= 16; layer++) {
                    std::uint32_t in = 0;
                    for (int m = 1; m <= model.missionCount(); m++) {
                        if (model.activeLayers(zone, m) & (1u << layer)) in |= 1u << (m - 1);
                    }
                    const std::string letter(1, static_cast< char >('a' + layer - 1));
                    if (in == want) return letter;
                    if (in != 0) {
                        std::string text = letter + ": missions";
                        for (int m = 1; m <= model.missionCount(); m++) {
                            if (in & (1u << (m - 1))) text += " " + std::to_string(m);
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
                if (!model.hasTransform(row)) fail("bad-args", where + ": " + rowRef(model, row) + " has no transform", "", {}, where);
                const RowTransform t = model.transform(row);
                const Frame f = zoneFrame(model, zone);
                G::V3 world = toWorld(f, v3(t.pos));
                if (op.has("pos")) world = parseVec(op["pos"], where + ".pos");
                if (op.has("zone_pos")) world = toWorld(f, parseVec(op["zone_pos"], where + ".zone_pos"));
                if (op.has("by")) world = world + parseVec(op["by"], where + ".by");
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
                    World& w = impl.world();
                    std::optional< Hit > hit = w.ray(world, down, 50000.0f);
                    if (!hit) hit = w.ray(world - down * 1500.0f, down, 51500.0f);
                    if (!hit) {
                        fail("no-ground", where + ": no ground below " + Json::write(vecJson(world, 0)) + " along gravity " + Json::write(vecJson(down, 3)) +
                                              " (" + gravitySource + ")",
                             "move the point over a planet, give \"down\": [x,y,z], or check `colliders`", {}, where + ".on_ground");
                    }
                    const float height = op.has("height") ? static_cast< float >(op["height"].asNumber()) : 0.0f;
                    world = hit->point - down * height;
                    groundInfo = Json::Value::object();
                    groundInfo.set("ground", vecJson(hit->point, 1));
                    groundInfo.set("on", rowRef(model, hit->row));
                    groundInfo.set("gravity", gravitySource);
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
                    if (op.has("rot")) basis = G::fromEulerDegrees(parseVec(op["rot"], where + ".rot"));
                    if (op.has("zone_rot")) basis = basisToWorld(f, G::fromEulerDegrees(parseVec(op["zone_rot"], where + ".zone_rot")));
                    if (align && !op.has("up")) basis = G::alignUp(basis, down * -1.0f);
                    if (op.has("up") || op.has("front")) {
                        const G::V3 up = op.has("up") ? G::normalized(parseVec(op["up"], where + ".up")) : basis.y;
                        const G::V3 front = op.has("front") ? parseVec(op["front"], where + ".front") : basis.z;
                        basis = G::gravityFrame(up, front);
                    }
                    if (op.has("turn")) basis = G::rotate(basis, basis.y, static_cast< float >(op["turn"].asNumber()) * G::kDeg);
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
                        if (!a.name.empty() && lower(a.name) == lower(key)) arg = &a;
                    }
                }
                if (arg == nullptr) {
                    std::string k = lower(key);
                    if (k.rfind("obj_arg", 0) == 0) k = k.substr(7);
                    if (!k.empty() && k.find_first_not_of("0123456789") == std::string::npos) index = std::atoi(k.c_str());
                    if (info != nullptr) {
                        for (const Catalog::Arg& a : info->args) {
                            if (a.index == index) arg = &a;
                        }
                    }
                } else {
                    index = arg->index;
                }
                if (index < 0 || index > 7) {
                    std::vector< std::string > names;
                    if (info != nullptr) {
                        for (const Catalog::Arg& a : info->args) names.push_back(a.name.empty() ? "Obj_arg" + std::to_string(a.index) : a.name);
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
                    if (!found) fail("bad-value", at + ": '" + value.asString() + "' is not a choice", "use one of the labels, or a number", labels, at);
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
                        fail("bad-value", at + ": '" + text + "' is not a switch", "a number, null, \"new\" or \"<ref>.SW_FIELD\" (e.g. \"@goomba.SW_DEAD\")", {}, at);
                    }
                    const RowId other = impl.resolveRef(Json::Value(text.substr(0, dot)), at);
                    std::string field = text.substr(dot + 1);
                    for (char& ch : field) ch = static_cast< char >(std::toupper(static_cast< unsigned char >(ch)));
                    const Cell* cell = model.cell(other, columnHash(field.c_str()));
                    if (cell == nullptr || cell->i < 0) {
                        fail("bad-value", at + ": " + text.substr(0, dot) + " has no " + field + " set", "give that row a switch first (\"new\")", {}, at);
                    }
                    int otherZone = 0, otherTable = 0;
                    model.locate(other, &otherZone, &otherTable);
                    if (otherZone != zone && cell->i < 1000) {
                        fail("bad-value", at + ": zone switches (0-127) only connect rows of one zone", "use a shared switch (1000-1127) across zones", {}, at);
                    }
                    v = cell->i;
                } else if (!value.isNull()) {
                    v = wholeNumber(value, at);
                }
                std::string column = key;
                if (lower(column).rfind("sw_", 0) != 0) column = "SW_" + column;
                for (char& ch : column) ch = static_cast< char >(std::toupper(static_cast< unsigned char >(ch)));
                if (!hasColumn(model, zone, table, columnHash(column.c_str()))) {
                    std::vector< std::string > have;
                    for (const Column& c : model.columns(zone, table)) {
                        const std::string n = columnLabel(c.hash);
                        if (n.rfind("SW_", 0) == 0) have.push_back(n);
                    }
                    fail("bad-args", at + ": the table has no " + column, "", have, at);
                }
                put(column.c_str(), intCell(v), at);
            }
            for (const auto& [key, value] : op["links"].members()) {
                const std::string at = where + ".links." + key;
                put(key.c_str(), intCell(value.isNull() ? -1 : wholeNumber(value, at)), at);
            }
            for (const auto& [key, value] : op["fields"].members()) {
                const std::string at = where + ".fields." + key;
                Cell cell;
                std::string error;
                if (!parseCell(value, key, &model.columns(zone, table), &cell, &error)) fail("bad-value", at + ": " + error, "", {}, at);
                set.sets.push_back({row, columnHashOfName(key), std::move(cell)});
            }
            if (!set.sets.empty()) apply(set, where);
        }

        // The lowest zone switch (0-127) no row of the zone uses.
        std::int32_t freeSwitch(int zone) {
            std::set< std::int32_t > used;
            for (int t = 0; t < model.tableCount(zone); t++) {
                std::vector< std::uint32_t > columns;
                for (const Column& c : model.columns(zone, t)) {
                    if (columnLabel(c.hash).rfind("SW_", 0) == 0) columns.push_back(c.hash);
                }
                model.forEachRow(zone, t, [&](RowId row) {
                    if (model.state(row) == RowState::Deleted) return;
                    for (std::uint32_t h : columns) {
                        if (const Cell* c = model.cell(row, h)) used.insert(c->i);
                    }
                });
            }
            for (std::int32_t id = 0; id < 128; id++) {
                if (!used.count(id)) return id;
            }
            fail("capacity", "every zone switch (0-127) of " + std::string(model.zoneName(zone)) + " is used", "use a shared switch (1000-1127)");
        }

        // The points of an array op (world space): line, arc, ring, grid, polyline.
        std::vector< G::V3 > arrayPoints(const Json::Value& op, const std::string& where) {
            const std::string shape = lower(op["shape"].asString());
            std::vector< G::V3 > out;
            auto count = [&](float length) {
                if (op.has("count")) return std::max(1, op["count"].asInt());
                if (op.has("spacing")) return std::max(2, static_cast< int >(std::floor(length / std::max(1.0, op["spacing"].asNumber()))) + 1);
                fail("bad-args", where + ": give \"count\" or \"spacing\"", "", {}, where);
            };
            // The up of the array's plane: given, else against gravity at `at`.
            auto upAt = [&](G::V3 at) {
                if (op.has("up")) return G::normalized(parseVec(op["up"], where + ".up"));
                if (op.has("down")) return G::normalized(parseVec(op["down"], where + ".down")) * -1.0f;
                return impl.world().gravityAt(model, at).down * -1.0f;
            };
            if (shape == "line" || shape == "arc") {
                const G::V3 a = parseVec(op["from"], where + ".from"), b = parseVec(op["to"], where + ".to");
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
                        out.push_back(o + frame.x * ((static_cast< float >(i) - (nx - 1) * 0.5f) * sx) + frame.z * ((static_cast< float >(k) - (nz - 1) * 0.5f) * sz));
                    }
                }
            } else if (shape == "path" || shape == "polyline") {
                std::vector< G::V3 > pts;
                int i = 0;
                for (const Json::Value& p : op["points"].items()) pts.push_back(parseVec(p, where + ".points[" + std::to_string(i++) + "]"));
                if (op["closed"].asBool() && pts.size() > 2) pts.push_back(pts.front());
                if (pts.size() < 2) fail("bad-args", where + ".points needs at least 2 points", "", {}, where + ".points");
                float length = 0;
                for (std::size_t k = 1; k < pts.size(); k++) length += G::length(pts[k] - pts[k - 1]);
                const int n = count(length);
                for (int j = 0; j < n; j++) {
                    // Evenly by distance along the polyline.
                    float d = n == 1 ? 0 : length * static_cast< float >(j) / static_cast< float >(op["closed"].asBool() ? n : n - 1);
                    std::size_t k = 1;
                    while (k < pts.size() - 1 && d > G::length(pts[k] - pts[k - 1])) {
                        d -= G::length(pts[k] - pts[k - 1]);
                        k++;
                    }
                    const float seg = std::max(G::length(pts[k] - pts[k - 1]), 1e-6f);
                    out.push_back(pts[k - 1] + (pts[k] - pts[k - 1]) * std::min(1.0f, d / seg));
                }
            } else {
                fail("bad-value", where + ".shape must be line, arc, ring, grid or path", "", {"line", "arc", "ring", "grid", "path"}, where + ".shape");
            }
            return out;
        }

        void label(RowId row, const Json::Value& op, const std::string& where) {
            if (!op.has("as")) return;
            std::string name = op["as"].asString();
            if (!name.empty() && name[0] == '@') name = name.substr(1);
            if (name.empty()) fail("bad-value", where + ".as must be a name", "", {}, where + ".as");
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
                checkKeys(op, with({"op", "class", "zone", "layer", "missions", "like", "preset", "as", "force"}, kEditKeys), where);
                if (!op.has("class") && !op.has("like")) fail("bad-args", where + ": add needs \"class\" (or \"like\": a row to copy)", "", {}, where);
                const Catalog::ClassInfo* info = op.has("class") ? impl.resolveClass(op["class"].asString(), !op["force"].asBool(), where + ".class") : nullptr;
                RowId like;
                int likeZone = 0, likeTable = 0;
                if (op.has("like")) {
                    like = impl.resolveRef(op["like"], where + ".like");
                    model.locate(like, &likeZone, &likeTable);
                }
                const int zone = op.has("zone") ? zoneOf(op["zone"], where + ".zone") : (like.valid() ? likeZone : 0);
                std::string table;
                if (like.valid() && !op.has("layer") && !op.has("missions") && !op.has("class")) {
                    table = model.tablePath(likeZone, likeTable);
                } else {
                    const std::string layer = layerTableName(layerFor(op, zone, where));
                    table = info != nullptr ? Catalog::tablePath(*info, layer) : "jmp/placement/" + layer + "/objinfo";
                }
                if (!op.has("pos") && !op.has("zone_pos") && !like.valid()) {
                    fail("bad-args", where + ": add needs \"pos\" (world) or \"zone_pos\"", "placement helpers (\"on_ground\") come with the place op", {}, where);
                }
                const std::string className = info != nullptr ? info->name : (op.has("class") ? op["class"].asString() : "");
                const RowId row = addRow(zone, table, like, className, where);
                Json::Value edits = op;
                if (info != nullptr && op.has("preset")) {
                    const Catalog::Preset* preset = nullptr;
                    std::vector< std::string > names;
                    for (const Catalog::Preset& p : info->presets) {
                        names.push_back(p.label);
                        if (lower(p.label) == lower(op["preset"].asString())) preset = &p;
                    }
                    if (preset == nullptr) fail("bad-value", where + ".preset: no preset '" + op["preset"].asString() + "'", "", names, where + ".preset");
                    Json::Value args = Json::Value::object();
                    for (const auto& [index, value] : preset->args) args.set("Obj_arg" + std::to_string(index), value);
                    for (const auto& [k, v] : op["args"].members()) args.set(k, v);
                    edits.set("args", std::move(args));
                }
                edit(row, edits, where);
                label(row, op, where);
            } else if (kind == "array") {
                checkKeys(op, with({"op", "class", "zone", "layer", "missions", "like", "preset", "as", "force", "shape", "from", "to", "count", "spacing",
                                    "center", "radius", "start_angle", "points", "origin", "size", "peak", "closed", "rot", "face_along"},
                                   {"args", "switches", "links", "fields", "on_ground", "height", "down", "align", "up", "front", "turn", "scale"}),
                          where);
                const std::vector< G::V3 > points = arrayPoints(op, where);
                if (points.size() > 500) fail("bad-value", where + ": " + std::to_string(points.size()) + " objects is too many for one array (500)", "", {}, where);
                const std::string as = op["as"].asString();
                for (std::size_t i = 0; i < points.size(); i++) {
                    Json::Value add = Json::Value::object();
                    for (const auto& [k, v] : op.members()) {
                        static const std::set< std::string > kShape = {"shape", "from", "to", "count", "spacing", "center", "radius", "start_angle",
                                                                      "points", "origin", "size", "peak", "closed", "as", "face_along"};
                        if (!kShape.count(k)) add.set(k, v);
                    }
                    add.set("op", "add");
                    add.set("pos", vecJson(points[i], 3));
                    if (op["face_along"].asBool() && points.size() > 1) {
                        const G::V3 next = i + 1 < points.size() ? points[i + 1] - points[i] : points[i] - points[i - 1];
                        add.set("front", vecJson(next, 4));
                    }
                    if (!as.empty()) add.set("as", as + "." + std::to_string(i));
                    run(add, where + "[" + std::to_string(i) + "]");
                }
            } else if (kind == "set" || kind == "move" || kind == "rotate" || kind == "scale" || kind == "transform") {
                checkKeys(op, with({"op", "ref", "refs", "select"}, kEditKeys), where);
                const std::vector< RowId > rows = impl.resolveTargets(op, where);
                if (rows.size() > 1 && (op.has("pos") || op.has("zone_pos"))) {
                    fail("bad-args", where + ": \"pos\" on " + std::to_string(rows.size()) + " rows would stack them", "use \"by\" to move several rows", {}, where);
                }
                for (RowId row : rows) edit(row, op, where);
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
                    if (c.key != key) continue;
                    if (c.fix.adds.empty() && c.fix.sets.empty() && c.fix.deletes.empty() && c.fix.revertRows.empty()) {
                        fail("no-fix", where + ": check " + key + " has no automatic fix",
                             c.fixKind.empty() ? "edit the rows it names" : "it needs a choice (" + c.fixKind + "): set the field yourself", {}, where);
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
                if (!opFromJson(model, op["edit"], &raw, &error)) fail("bad-args", where + ".edit: " + error, "", {}, where + ".edit");
                for (const AddRow& add : raw.adds) {
                    impl.nextNew = std::max(impl.nextNew, (add.id.v & ~(3ull << 62)) + 1);
                    created.push_back(add.id);
                }
                apply(raw, where);
            } else {
                std::vector< std::string > kinds;
                for (const CommandSpec& s : opKinds()) kinds.push_back(s.name);
                fail("bad-op", where + ": unknown op '" + op["op"].asString() + "'", "`help --ops` lists them", kinds, where + ".op");
            }
        }
    };
}  // namespace

Json::Value Level::Impl::applyBatch(const Json::Value& args, bool dryRun) {
    const Json::Value& ops = args.has("ops") ? args["ops"] : args;
    if (!ops.isArray()) fail("bad-args", "apply needs \"ops\": [op, ...]", "e.g. {\"ops\": [{\"op\": \"add\", \"class\": \"Coin\", \"pos\": [0, 500, 0]}]}");
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
        for (int i = 0; i < batch.steps; i++) model->undo();
        labels = labelsBefore;
        nextNew = nextBefore;
    };
    try {
        int i = 0;
        for (const Json::Value& op : ops.items()) {
            const std::string where = "ops[" + std::to_string(i++) + "]";
            if (!op.isObject() || !op.has("op")) fail("bad-args", where + " must be an object with \"op\"", "", {}, where);
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
        if (ops.items().size() == 1 && ops.items()[0].has("class")) label += " " + ops.items()[0]["class"].asString();
    }
    const Op merged = mergedOp(*model, before, after, label);
    const Json::Value opJson = opToJson(*model, merged);

    Json::Value created = Json::Value::array();
    for (RowId row : batch.created) {
        if (after.count(row)) created.push(objectJson(row, false));
    }
    Json::Value changed = Json::Value::array();
    std::set< RowId > seen;
    for (const SetCell& s : merged.sets) {
        if (seen.insert(s.row).second) changed.push(rowRef(*model, s.row));
    }
    Json::Value deleted = Json::Value::array();
    for (RowId row : merged.deletes) deleted.push(rowRef(*model, row));
    // Checks this batch introduced and resolved.
    const std::vector< Check > checksAfter = newChecks();
    Json::Value introduced = Json::Value::array();
    for (const Check& c : checksAfter) {
        bool had = false;
        for (const Check& b : checksBefore) had = had || b.key == c.key;
        if (had) continue;
        Json::Value j = Json::Value::object();
        j.set("key", c.key);
        j.set("severity", c.severity == Severity::Error ? "error" : "warning");
        j.set("message", c.message);
        if (!c.fixLabel.empty()) j.set("fix", c.fixLabel);
        introduced.push(std::move(j));
    }
    Json::Value resolved = Json::Value::array();
    for (const Check& b : checksBefore) {
        bool still = false;
        for (const Check& c : checksAfter) still = still || b.key == c.key;
        if (!still) resolved.push(b.key);
    }

    // One undo step: back out the steps, then apply the merged op.
    for (int i = 0; i < batch.steps; i++) model->undo();
    const bool empty = merged.adds.empty() && merged.sets.empty() && merged.deletes.empty();
    if (dryRun || empty) {
        if (dryRun) {
            labels = labelsBefore;
            nextNew = nextBefore;
        }
        out.set("dry_run", dryRun);
        if (empty) out.set("no_change", true);
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
    if (!batch.placements.items().empty()) out.set("placements", batch.placements);
    out.set("checks_introduced", std::move(introduced));
    out.set("checks_resolved", std::move(resolved));
    if (args["show_op"].asBool()) out.set("op", opJson);
    return out;
}

}  // namespace PetariEditor::LevelApi
