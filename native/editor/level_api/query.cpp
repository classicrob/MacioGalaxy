// Queries: refs and selectors, classes by any name, objects as JSON (level_api.hpp).
#include "level_internal.hpp"

#include <petari/editor/paths.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace PetariEditor::LevelApi {

void fail(const std::string& code, const std::string& message, const std::string& hint, std::vector< std::string > candidates,
          const std::string& where) {
    throw Failure{Error{code, message, hint, std::move(candidates), where}};
}

Json::Value Error::toJson() const {
    Json::Value e = Json::Value::object();
    e.set("code", code);
    e.set("message", message);
    if (!hint.empty())
        e.set("hint", hint);
    if (!where.empty())
        e.set("where", where);
    if (!candidates.empty()) {
        Json::Value list = Json::Value::array();
        for (const std::string& c : candidates)
            list.push(c);
        e.set("candidates", std::move(list));
    }
    return e;
}

// ---- geometry ------------------------------------------------------------------------------------

G::V3 v3(const Vec3& v) {
    return {v.x, v.y, v.z};
}

Frame zoneFrame(const ModelView& model, int zone) {
    const Mtx34 m = model.zoneWorldMatrix(zone);
    Frame f;
    f.origin = {m.m[0][3], m.m[1][3], m.m[2][3]};
    f.basis.x = {m.m[0][0], m.m[1][0], m.m[2][0]};
    f.basis.y = {m.m[0][1], m.m[1][1], m.m[2][1]};
    f.basis.z = {m.m[0][2], m.m[1][2], m.m[2][2]};
    return f;
}

G::V3 toWorld(const Frame& f, G::V3 p) {
    return f.origin + f.basis.x * p.x + f.basis.y * p.y + f.basis.z * p.z;
}

G::V3 toLocal(const Frame& f, G::V3 p) {
    const G::V3 d = p - f.origin;
    return {G::dot(d, f.basis.x), G::dot(d, f.basis.y), G::dot(d, f.basis.z)};
}

G::Basis basisToWorld(const Frame& f, const G::Basis& b) {
    auto dir = [&](G::V3 v) { return f.basis.x * v.x + f.basis.y * v.y + f.basis.z * v.z; };
    return {dir(b.x), dir(b.y), dir(b.z)};
}

G::Basis basisToLocal(const Frame& f, const G::Basis& b) {
    auto dir = [&](G::V3 v) { return G::V3{G::dot(v, f.basis.x), G::dot(v, f.basis.y), G::dot(v, f.basis.z)}; };
    return {dir(b.x), dir(b.y), dir(b.z)};
}

Json::Value vecJson(G::V3 v, int decimals) {
    const double scale = std::pow(10.0, decimals);
    auto r = [&](float x) {
        double d = std::round(static_cast< double >(x) * scale) / scale;
        return Json::Value(d == 0.0 ? 0.0 : d);  // no "-0"
    };
    Json::Value a = Json::Value::array();
    a.push(r(v.x));
    a.push(r(v.y));
    a.push(r(v.z));
    return a;
}

G::V3 parseVec(const Json::Value& value, const std::string& where) {
    if (value.isArray() && value.items().size() == 3) {
        const auto& i = value.items();
        if (i[0].type() == Json::Value::Type::Number && i[1].type() == Json::Value::Type::Number && i[2].type() == Json::Value::Type::Number) {
            return {static_cast< float >(i[0].asNumber()), static_cast< float >(i[1].asNumber()), static_cast< float >(i[2].asNumber())};
        }
    }
    if (value.isObject() && value.has("x") && value.has("y") && value.has("z")) {
        return {static_cast< float >(value["x"].asNumber()), static_cast< float >(value["y"].asNumber()),
                static_cast< float >(value["z"].asNumber())};
    }
    if (value.type() == Json::Value::Type::String) {
        // "x,y,z" (the CLI's flag form)
        float x, y, z;
        if (std::sscanf(value.asString().c_str(), "%f,%f,%f", &x, &y, &z) == 3) {
            return {x, y, z};
        }
    }
    fail("bad-value", where + " must be [x, y, z]", "e.g. [0, 1200, -350]", {}, where);
}

// ---- rows ----------------------------------------------------------------------------------------

std::vector< RowId > Level::Impl::allRows() const {
    std::vector< RowId > rows;
    for (int z = 0; z < model->zoneCount(); z++) {
        for (int t = 0; t < model->tableCount(z); t++) {
            model->forEachRow(z, t, [&](RowId row) {
                if (model->state(row) != RowState::Deleted)
                    rows.push_back(row);
            });
        }
    }
    return rows;
}

std::string Level::Impl::labelOf(RowId row) const {
    for (const auto& [name, id] : labels) {
        if (id == row)
            return name;
    }
    return "";
}

namespace {
    bool liveRow(const ModelView& model, RowId row) {
        int z, t;
        return row.valid() && model.locate(row, &z, &t) && model.state(row) != RowState::Deleted;
    }

    std::string describe(const Level::Impl& impl, RowId row) {
        const char* name = impl.model->objectName(row);
        std::string text = rowRef(*impl.model, row) + " (" + (name != nullptr ? name : "?");
        if (impl.model->hasTransform(row)) {
            const RowTransform t = impl.model->transform(row);
            int zone = 0, table = 0;
            impl.model->locate(row, &zone, &table);
            const G::V3 p = toWorld(zoneFrame(*impl.model, zone), v3(t.pos));
            char buf[64];
            std::snprintf(buf, sizeof(buf), " at %.0f,%.0f,%.0f", p.x, p.y, p.z);
            text += buf;
        }
        return text + ")";
    }
}  // namespace

RowId Level::Impl::resolveRef(const Json::Value& ref, const std::string& where) const {
    if (ref.isObject()) {
        Json::Value filter = ref;
        const std::vector< RowId > rows = select(filter, where);
        const int nth = ref.has("nth") ? ref["nth"].asInt() : -1;
        if (nth >= 0) {
            if (nth >= static_cast< int >(rows.size())) {
                fail("bad-ref",
                     where + ": the selector matches " + std::to_string(rows.size()) + " rows, nth " + std::to_string(nth) + " is past the end",
                     "", {}, where);
            }
            return rows[nth];
        }
        if (rows.size() != 1) {
            std::vector< std::string > candidates;
            for (std::size_t i = 0; i < rows.size() && i < 8; i++)
                candidates.push_back(describe(*this, rows[i]));
            fail(rows.empty() ? "bad-ref" : "ambiguous-ref",
                 where + ": the selector matches " + std::to_string(rows.size()) + " rows (want exactly 1)",
                 rows.empty() ? "loosen the selector, or list objects with `list`" : "add \"nth\": i, \"near\": [x,y,z] or use a ref", candidates,
                 where);
        }
        return rows[0];
    }
    if (ref.type() != Json::Value::Type::String || ref.asString().empty()) {
        fail("bad-ref", where + " must be a ref string or a selector object",
             "refs look like \"EggStarGalaxy:common/objinfo:12\", \"new:3\" or \"@label\"", {}, where);
    }
    const std::string text = ref.asString();
    if (text[0] == '@') {
        auto it = labels.find(text.substr(1));
        if (it == labels.end()) {
            std::vector< std::string > candidates;
            for (const auto& [name, id] : labels)
                candidates.push_back("@" + name);
            fail("bad-ref", where + ": no label " + text, "labels are set with \"as\" on add/duplicate or the label op", candidates, where);
        }
        if (!liveRow(*model, it->second)) {
            fail("bad-ref", where + ": " + text + " (" + rowRef(*model, it->second) + ") was deleted or undone", "", {}, where);
        }
        return it->second;
    }
    // "Class#n": the n-th row of a class, galaxy order.
    const std::size_t hash = text.find('#');
    if (text.find(':') == std::string::npos && hash != std::string::npos) {
        Json::Value filter = Json::Value::object();
        filter.set("class", text.substr(0, hash));
        filter.set("nth", std::atoi(text.c_str() + hash + 1));
        return resolveRef(filter, where);
    }
    const RowId row = parseRowRef(*model, text);
    if (!row.valid()) {
        std::vector< std::string > zones;
        for (int z = 0; z < model->zoneCount(); z++)
            zones.push_back(model->zoneName(z));
        fail("bad-ref", where + ": '" + text + "' is not a ref",
             "refs: \"<Zone>:<table>:<index>\", \"new:<n>\", \"@label\" or \"<Class>#<n>\"; zones are listed", zones, where);
    }
    if (!liveRow(*model, row)) {
        fail("bad-ref", where + ": " + text + (model->state(row) == RowState::Deleted ? " was deleted" : " names no row"),
             "list objects with `list`", {}, where);
    }
    return row;
}

std::vector< RowId > Level::Impl::resolveTargets(const Json::Value& op, const std::string& where) const {
    std::vector< RowId > rows;
    if (op.has("ref"))
        rows.push_back(resolveRef(op["ref"], where + ".ref"));
    if (op.has("refs")) {
        int i = 0;
        for (const Json::Value& r : op["refs"].items())
            rows.push_back(resolveRef(r, where + ".refs[" + std::to_string(i++) + "]"));
    }
    if (op.has("select")) {
        std::vector< RowId > picked = select(op["select"], where + ".select");
        if (op["select"].has("expect") && static_cast< int >(picked.size()) != op["select"]["expect"].asInt()) {
            fail("bad-ref",
                 where + ".select matches " + std::to_string(picked.size()) + " rows, expected " + std::to_string(op["select"]["expect"].asInt()),
                 "", {}, where + ".select");
        }
        rows.insert(rows.end(), picked.begin(), picked.end());
    }
    if (rows.empty()) {
        fail("bad-args", where + " needs \"ref\", \"refs\" or \"select\"", "", {}, where);
    }
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    return rows;
}

const Catalog::ClassInfo* Level::Impl::resolveClass(const std::string& name, bool required, const std::string& where) const {
    if (const Catalog::ClassInfo* p = Catalog::find(name.c_str()))
        return p;
    const std::string want = lower(name);
    for (const Catalog::ClassInfo& c : Catalog::classes()) {
        if (lower(c.displayName) == want)
            return &c;
    }
    for (const Catalog::ClassInfo& c : Catalog::classes()) {
        for (const std::string& a : c.aliases) {
            if (lower(a) == want)
                return &c;
        }
    }
    if (!required)
        return nullptr;
    std::vector< std::string > candidates;
    for (const Catalog::ClassInfo* c : Catalog::search(name, 6))
        candidates.push_back(c->name + " (" + c->displayName + ")");
    if (candidates.empty()) {
        // Typos: the names (class, display, alias) nearest by edit distance.
        auto distance = [](const std::string& a, const std::string& b) {
            std::vector< int > row(b.size() + 1);
            for (std::size_t j = 0; j <= b.size(); j++)
                row[j] = static_cast< int >(j);
            for (std::size_t i = 1; i <= a.size(); i++) {
                int diagonal = row[0];
                row[0] = static_cast< int >(i);
                for (std::size_t j = 1; j <= b.size(); j++) {
                    const int up = row[j];
                    row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
                    diagonal = up;
                }
            }
            return row[b.size()];
        };
        std::vector< std::pair< int, const Catalog::ClassInfo* > > scored;
        for (const Catalog::ClassInfo& c : Catalog::classes()) {
            if (!c.placeable)
                continue;
            int best = std::min(distance(want, lower(c.name)), distance(want, lower(c.displayName)));
            for (const std::string& a : c.aliases)
                best = std::min(best, distance(want, lower(a)));
            scored.push_back({best, &c});
        }
        std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (std::size_t i = 0; i < scored.size() && i < 5; i++)
            candidates.push_back(scored[i].second->name + " (" + scored[i].second->displayName + ")");
    }
    fail("unknown-class", where + ": no class '" + name + "'", "search the catalog: `catalog --query <words>`", candidates, where);
}

std::vector< RowId > Level::Impl::select(const Json::Value& filter, const std::string& where) const {
    if (!filter.isObject())
        fail("bad-args", where + " must be an object", "e.g. {\"class\": \"Coin\", \"zone\": \"EggStarGalaxy\"}", {}, where);
    std::set< std::string > classes;
    if (filter.has("class")) {
        const std::string want = filter["class"].asString();
        if (const Catalog::ClassInfo* info = resolveClass(want, false, where))
            classes.insert(lower(info->name));
        classes.insert(lower(want));
    }
    const Catalog::Category* pCategory = nullptr;
    Catalog::Category category{};
    if (filter.has("category")) {
        const std::string want = lower(filter["category"].asString());
        bool found = false;
        std::vector< std::string > names;
        for (int c = 0; c <= static_cast< int >(Catalog::Category::Other); c++) {
            const std::string n = lower(Catalog::categoryName(static_cast< Catalog::Category >(c)));
            names.push_back(n);
            if (n == want || n == want + "s" || n.rfind(want, 0) == 0) {
                category = static_cast< Catalog::Category >(c);
                found = true;
                break;
            }
        }
        if (!found)
            fail("bad-value", where + ".category: no category '" + want + "'", "", names, where + ".category");
        pCategory = &category;
    }
    int zone = -1;
    if (filter.has("zone")) {
        const std::string want =
            filter["zone"].type() == Json::Value::Type::Number ? std::to_string(filter["zone"].asInt()) : filter["zone"].asString();
        for (int z = 0; z < model->zoneCount(); z++) {
            if (lower(model->zoneName(z)) == lower(want) || std::to_string(z) == want)
                zone = z;
        }
        if (zone < 0) {
            std::vector< std::string > zones;
            for (int z = 0; z < model->zoneCount(); z++)
                zones.push_back(model->zoneName(z));
            fail("bad-value", where + ".zone: no zone '" + want + "'", "", zones, where + ".zone");
        }
    }
    const int mission = filter.has("mission") ? filter["mission"].asInt() : 0;
    const bool hasNear = filter.has("near");
    const G::V3 near = hasNear ? parseVec(filter["near"], where + ".near") : G::V3{};
    const float radius = filter.has("radius") ? static_cast< float >(filter["radius"].asNumber()) : 1e30f;
    const std::string table = filter.has("table") ? lower(filter["table"].asString()) : "";
    const std::string state = filter.has("state") ? lower(filter["state"].asString()) : "";
    const std::string text = filter.has("text") ? lower(filter["text"].asString()) : "";

    std::vector< std::pair< float, RowId > > hits;
    for (RowId row : allRows()) {
        int z = 0, t = 0;
        model->locate(row, &z, &t);
        if (zone >= 0 && z != zone)
            continue;
        const char* pName = model->objectName(row);
        const std::string name = pName != nullptr ? pName : "";
        if (!classes.empty() && !classes.count(lower(name)))
            continue;
        const Catalog::ClassInfo* info = name.empty() ? nullptr : Catalog::find(name.c_str());
        if (pCategory != nullptr && (info == nullptr || info->category != *pCategory))
            continue;
        if (mission > 0 && !(model->rowMissions(row) & (1u << (mission - 1))))
            continue;
        if (!table.empty()) {
            const std::string path = model->tablePath(z, t);
            if (path != table && shortTablePath(path) != table && shortTablePath(path).find(table) == std::string::npos)
                continue;
        }
        if (!state.empty()) {
            static const char* const kStates[] = {"original", "changed", "added", "deleted"};
            if (state != kStates[static_cast< int >(model->state(row))])
                continue;
        }
        if (!text.empty()) {
            const std::string display = info != nullptr ? lower(info->displayName) : "";
            if (lower(name).find(text) == std::string::npos && display.find(text) == std::string::npos)
                continue;
        }
        float d = 0;
        if (hasNear) {
            if (!model->hasTransform(row))
                continue;
            const G::V3 p = toWorld(zoneFrame(*model, z), v3(model->transform(row).pos));
            d = G::length(p - near);
            if (d > radius)
                continue;
        }
        hits.push_back({d, row});
    }
    if (hasNear) {
        std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    }
    std::vector< RowId > rows;
    for (const auto& h : hits)
        rows.push_back(h.second);
    return rows;
}

// ---- objects as JSON -----------------------------------------------------------------------------

namespace {
    const char* layerName(int layer) {
        static const char* const kNames[] = {"common", "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p"};
        return layer >= 0 && layer <= 16 ? kNames[layer] : "";
    }

    Json::Value cellValue(const Cell& c) {
        switch (c.type) {
        case CellType::Float: {
            double d = std::round(static_cast< double >(c.f) * 1000.0) / 1000.0;
            return Json::Value(d == 0.0 ? 0.0 : d);
        }
        case CellType::String:
        case CellType::StringOffset:
            return Json::Value(c.s);
        default:
            return Json::Value(static_cast< double >(c.i));
        }
    }

    bool isSwitchColumn(const std::string& name) {
        return name.rfind("SW_", 0) == 0;
    }

    bool isLinkColumn(const std::string& name) {
        static const char* const kLinks[] = {"CommonPath_ID", "GroupId",   "ClippingGroupId", "ViewGroupId", "DemoGroupId",
                                             "CameraSetId",   "MessageId", "CastId",          "ParentID",    "MapParts_ID",
                                             "Obj_ID",        "FollowId",  "AreaShapeNo"};
        for (const char* l : kLinks) {
            if (name == l)
                return true;
        }
        return false;
    }
}  // namespace

Json::Value argValueJson(const Catalog::Arg& arg, std::int32_t value) {
    if (value == -1)
        return Json::Value();  // the class's default
    switch (arg.type) {
    case Catalog::ArgType::Bool:
        return Json::Value(true);  // the game's bool getter: anything but -1 is on
    case Catalog::ArgType::Enum:
        for (const Catalog::EnumChoice& c : arg.choices) {
            if (c.value == value)
                return Json::Value(c.label);
        }
        return Json::Value(static_cast< double >(value));
    default:
        return Json::Value(static_cast< double >(value));
    }
}

Json::Value Level::Impl::objectJson(RowId row, bool full) const {
    Json::Value o = Json::Value::object();
    int zone = 0, table = 0;
    model->locate(row, &zone, &table);
    o.set("ref", rowRef(*model, row));
    const std::string label = labelOf(row);
    if (!label.empty())
        o.set("label", "@" + label);
    const char* pName = model->objectName(row);
    const std::string name = pName != nullptr ? pName : "";
    o.set("class", name);
    const Catalog::ClassInfo* info = name.empty() ? nullptr : Catalog::find(name.c_str());
    if (info != nullptr && info->displayName != name)
        o.set("name", info->displayName);
    o.set("zone", model->zoneName(zone));
    if (model->hasTransform(row)) {
        const RowTransform t = model->transform(row);
        const Frame f = zoneFrame(*model, zone);
        o.set("pos", vecJson(toWorld(f, v3(t.pos)), 1));
        if (full) {
            const G::Basis world = basisToWorld(f, G::fromEulerDegrees(v3(t.dirDeg)));
            o.set("rot", vecJson(G::toEulerDegrees(world), 2));
            o.set("up", vecJson(world.y, 3));
            o.set("front", vecJson(world.z, 3));
            o.set("scale", vecJson(v3(t.scale), 3));
            if (zone != 0) {
                o.set("zone_pos", vecJson(v3(t.pos), 1));
                o.set("zone_rot", vecJson(v3(t.dirDeg), 2));
            }
        }
    }
    // A path point uses absolute pnt0/1/2 coordinates rather than object transforms.
    if (std::string(model->tablePath(zone, table)).find("jmp/path/commonpathpointinfo.") == 0) {
        for (const auto& rail : Paths::collect(*model)) {
            for (const auto& point : rail.points) {
                if (point.row != row)
                    continue;
                const Frame frame = zoneFrame(*model, zone);
                o.set("kind", "path-point");
                o.set("path", rowRef(*model, rail.row));
                o.set("pos", vecJson(toWorld(frame, v3(point.position)), 3));
                o.set("in", vecJson(toWorld(frame, v3(point.incoming)), 3));
                o.set("out", vecJson(toWorld(frame, v3(point.outgoing)), 3));
                if (full)
                    o.set("zone_pos", vecJson(v3(point.position), 3));
                break;
            }
        }
    }
    // A rail (jmp/path/commonpathinfo): its points in world space (summaries: the count and the first point).
    if (std::string(model->tablePath(zone, table)) == "jmp/path/commonpathinfo") {
        for (const Paths::Rail& rail : Paths::collect(*model)) {
            if (rail.row != row)
                continue;
            const Frame f = zoneFrame(*model, zone);
            if (!rail.points.empty())
                o.set("pos", vecJson(toWorld(f, v3(rail.points.front().position)), 1));
            o.set("rail_id", rail.id);
            if (!full) {
                o.set("points", static_cast< int >(rail.points.size()));
                break;
            }
            o.set("closed", rail.closed);
            Json::Value points = Json::Value::array();
            for (const Paths::Point& p : rail.points) {
                Json::Value j = Json::Value::object();
                j.set("ref", rowRef(*model, p.row));
                j.set("pos", vecJson(toWorld(f, v3(p.position)), 1));
                j.set("in", vecJson(toWorld(f, v3(p.incoming)), 1));
                j.set("out", vecJson(toWorld(f, v3(p.outgoing)), 1));
                points.push(std::move(j));
            }
            o.set("points", std::move(points));
            break;
        }
    }
    Json::Value missions = Json::Value::array();
    const std::uint32_t mask = model->rowMissions(row);
    for (int m = 1; m <= model->missionCount(); m++) {
        if (mask & (1u << (m - 1)))
            missions.push(m);
    }
    o.set("missions", std::move(missions));
    if (!full)
        return o;

    if (info != nullptr)
        o.set("category", Catalog::categoryName(info->category));
    o.set("table", shortTablePath(model->tablePath(zone, table)));
    const int layer = model->tableLayer(zone, table);
    if (layer >= 0)
        o.set("layer", layerName(layer));
    static const char* const kStates[] = {"original", "changed", "added", "deleted"};
    o.set("state", kStates[static_cast< int >(model->state(row))]);

    // Args by name (null = the class's default), then switches and links, then every raw field.
    Json::Value args = Json::Value::object();
    Json::Value switches = Json::Value::object();
    Json::Value links = Json::Value::object();
    Json::Value fields = Json::Value::object();
    for (const Column& c : model->columns(zone, table)) {
        const Cell* cell = model->cell(row, c.hash);
        if (cell == nullptr)
            continue;
        const std::string col = columnLabel(c.hash);
        fields.set(col, cellValue(*cell));
        if (col.rfind("Obj_arg", 0) == 0) {
            const int index = std::atoi(col.c_str() + 7);
            const Catalog::Arg* arg = nullptr;
            if (info != nullptr) {
                for (const Catalog::Arg& a : info->args) {
                    if (a.index == index)
                        arg = &a;
                }
            }
            if (arg != nullptr && !arg->name.empty()) {
                args.set(arg->name, argValueJson(*arg, cell->i));
            } else if (cell->i != -1) {
                args.set(col, Json::Value(static_cast< double >(cell->i)));
            }
        } else if (isSwitchColumn(col)) {
            if (cell->i != -1)
                switches.set(col, Json::Value(static_cast< double >(cell->i)));
        } else if (isLinkColumn(col)) {
            if (cell->i != -1)
                links.set(col, Json::Value(static_cast< double >(cell->i)));
        }
    }
    o.set("args", std::move(args));
    o.set("switches", std::move(switches));
    o.set("links", std::move(links));
    o.set("fields", std::move(fields));
    return o;
}

}  // namespace PetariEditor::LevelApi
