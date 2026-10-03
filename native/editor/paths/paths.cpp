#include <algorithm>
#include <cmath>
#include <map>
#include <petari/editor/debug_draw.hpp>
#include <petari/editor/paths.hpp>
#include <set>

namespace PetariEditor {
    int ModelView::railPointTable(RowId rail) const {
        int zone = -1, table = -1;
        if (!locate(rail, &zone, &table) || state(rail) == RowState::Deleted || std::string(tablePath(zone, table)) != "jmp/path/commonpathinfo")
            return -1;
        int index = 0, found = -1;
        forEachRow(zone, table, [&](RowId row) {
            if (state(row) != RowState::Deleted) {
                if (row == rail)
                    found = index;
                ++index;
            }
        });
        if (found < 0)
            return -1;
        const std::string path = "jmp/path/commonpathpointinfo." + std::to_string(found);
        for (int t = 0; t < tableCount(zone); ++t)
            if (tablePath(zone, t) == path)
                return t;
        return -1;
    }
    std::vector< RowId > ModelView::railPoints(RowId rail) const {
        std::vector< RowId > result;
        int zone = -1;
        const int table = railPointTable(rail);
        if (table >= 0 && locate(rail, &zone, nullptr))
            forEachRow(zone, table, [&](RowId row) {
                if (state(row) != RowState::Deleted)
                    result.push_back(row);
            });
        return result;
    }
}  // namespace PetariEditor
namespace PetariEditor::Paths {
    namespace {
        int integer(const ModelView& m, RowId row, const char* field) {
            const Cell* cell = m.cell(row, columnHash(field));
            return cell ? cell->i : -1;
        }
        Vec3 position(const ModelView& m, RowId row, int n) {
            float v[3]{};
            for (int i = 0; i < 3; ++i) {
                const std::string field = "pnt" + std::to_string(n) + "_" + "xyz"[i];
                const Cell* c = m.cell(row, columnHash(field.c_str()));
                if (c)
                    v[i] = c->type == CellType::Float ? c->f : static_cast< float >(c->i);
            }
            return {v[0], v[1], v[2]};
        }
        void setPosition(Op& op, RowId row, int n, Vec3 p) {
            const float v[] = {p.x, p.y, p.z};
            for (int i = 0; i < 3; ++i) {
                const std::string field = "pnt" + std::to_string(n) + "_" + "xyz"[i];
                op.sets.push_back({row, columnHash(field.c_str()), Cell{CellType::Float, 0, v[i], {}}});
            }
        }
        Vec3 lerp(Vec3 a, Vec3 b, float t) {
            return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
        }
        bool live(const ModelView& m, RowId r) {
            return m.state(r) != RowState::Deleted;
        }
    }  // namespace
    std::vector< Rail > collect(const ModelView& m) {
        std::vector< Rail > rails;
        for (int z = 0; z < m.zoneCount(); ++z) {
            int info = -1;
            std::map< std::string, int > tables;
            for (int t = 0; t < m.tableCount(z); ++t) {
                const std::string path = m.tablePath(z, t);
                if (path == "jmp/path/commonpathinfo")
                    info = t;
                tables[path] = t;
            }
            if (info < 0)
                continue;
            int index = 0;
            m.forEachRow(z, info, [&](RowId row) {
                if (!live(m, row))
                    return;
                Rail rail;
                rail.row = row;
                rail.zone = z;
                rail.id = integer(m, row, "l_id");
                rail.table = "jmp/path/commonpathpointinfo." + std::to_string(index++);
                const Cell* closed = m.cell(row, columnHash("closed"));
                rail.closed = closed && closed->s == "CLOSE";
                const int pointTable = m.railPointTable(row);
                if (pointTable >= 0)
                    rail.table = m.tablePath(z, pointTable);
                for (RowId p : m.railPoints(row))
                    rail.points.push_back({p, position(m, p, 0), position(m, p, 1), position(m, p, 2)});
                rails.push_back(std::move(rail));
            });
        }
        return rails;
    }
    Op addRail(const ModelView& m, int zone, const std::vector< Point >& points, bool closed, RowId freshRail, std::string* error) {
        auto reject = [&](const char* message) {
            if (error)
                *error = message;
            return Op{};
        };
        if (zone < 0 || zone >= m.zoneCount() || points.size() < 2)
            return reject("Choose a zone and at least two path points.");
        std::set< RowId > identities;
        identities.insert(freshRail);
        if (!freshRail.valid() || m.locate(freshRail, nullptr, nullptr))
            return reject("The rail needs a fresh row ID.");
        for (const auto& p : points) {
            if (!p.row.valid() || m.locate(p.row, nullptr, nullptr) || !identities.insert(p.row).second)
                return reject("Each point needs a distinct fresh row ID.");
            for (Vec3 v : {p.position, p.incoming, p.outgoing})
                if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
                    return reject("Path coordinates must be finite.");
        }
        int index = 0, id = 0;
        std::set< int > used;
        for (const auto& rail : collect(m))
            if (rail.zone == zone) {
                ++index;
                used.insert(rail.id);
            }
        while (used.count(id))
            ++id;
        Op op;
        op.label = "Create path";
        AddRow rail;
        rail.id = freshRail;
        rail.zone = zone;
        rail.table = "jmp/path/commonpathinfo";
        rail.cells = {{columnHash("l_id"), Cell{CellType::Int, id, 0, {}}},
                      {columnHash("no"), Cell{CellType::Int, index, 0, {}}},
                      {columnHash("num_pnt"), Cell{CellType::Int, static_cast< int >(points.size()), 0, {}}},
                      {columnHash("closed"), Cell{CellType::StringOffset, 0, 0, closed ? "CLOSE" : "OPEN"}},
                      {columnHash("type"), Cell{CellType::StringOffset, 0, 0, "Bezier"}}};
        op.adds.push_back(std::move(rail));
        for (int i = 0; i < static_cast< int >(points.size()); ++i) {
            const auto& p = points[i];
            AddRow add;
            add.id = p.row;
            add.zone = zone;
            add.table = "jmp/path/commonpathpointinfo." + std::to_string(index);
            add.cells.push_back({columnHash("id"), Cell{CellType::Int, i, 0, {}}});
            const auto coords = movePoint(p, p.position);
            for (const auto& cell : coords.sets)
                add.cells.push_back({cell.column, cell.after});
            op.adds.push_back(std::move(add));
        }
        return op;
    }
    Vec3 evaluate(const Point& a, const Point& b, float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        const Vec3 ab = lerp(a.position, a.outgoing, t), bc = lerp(a.outgoing, b.incoming, t), cd = lerp(b.incoming, b.position, t);
        return lerp(lerp(ab, bc, t), lerp(bc, cd, t), t);
    }
    Point split(Point* a, Point* b, float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        const Vec3 ab = lerp(a->position, a->outgoing, t), bc = lerp(a->outgoing, b->incoming, t), cd = lerp(b->incoming, b->position, t);
        const Vec3 abc = lerp(ab, bc, t), bcd = lerp(bc, cd, t);
        Point mid{{}, lerp(abc, bcd, t), abc, bcd};
        a->outgoing = ab;
        b->incoming = cd;
        return mid;
    }
    Op insertPoint(const Rail& rail, int index, float t, RowId freshPoint, std::string* error) {
        if (!freshPoint.valid() || rail.points.size() < 2 || index < 0 || index >= static_cast< int >(rail.points.size()) ||
            (!rail.closed && index + 1 == static_cast< int >(rail.points.size()))) {
            if (error)
                *error = "Choose a segment of a path and a fresh point ID.";
            return {};
        }
        auto a = rail.points[index], b = rail.points[(index + 1) % rail.points.size()];
        Point point = split(&a, &b, std::clamp(t, 0.01f, 0.99f));
        point.row = freshPoint;
        Op op = moveHandle(a, true, a.outgoing);
        op.label = "Insert path point";
        auto next = moveHandle(b, false, b.incoming);
        op.sets.insert(op.sets.end(), next.sets.begin(), next.sets.end());
        AddRow add;
        add.id = point.row;
        add.zone = rail.zone;
        add.table = rail.table;
        add.copyOf = a.row;
        add.at = index + 1;
        op.adds.push_back(add);
        op.sets.push_back({rail.row, columnHash("num_pnt"), Cell{CellType::Int, static_cast< int >(rail.points.size()) + 1, 0, {}}});
        auto values = movePoint(point, point.position);
        op.sets.insert(op.sets.end(), values.sets.begin(), values.sets.end());
        op.sets.push_back({point.row, columnHash("id"), Cell{CellType::Int, index + 1, 0, {}}});
        for (int i = index + 1; i < static_cast< int >(rail.points.size()); ++i)
            op.sets.push_back({rail.points[i].row, columnHash("id"), Cell{CellType::Int, i + 1, 0, {}}});
        return op;
    }
    Op deletePoint(const Rail& rail, RowId row, std::string* error) {
        if (rail.points.size() <= 2 || std::none_of(rail.points.begin(), rail.points.end(), [&](const Point& p) { return p.row == row; })) {
            if (error)
                *error = "A path needs at least two points.";
            return {};
        }
        Op op;
        op.label = "Delete path point";
        op.deletes.push_back(row);
        op.sets.push_back({rail.row, columnHash("num_pnt"), Cell{CellType::Int, static_cast< int >(rail.points.size()) - 1, 0, {}}});
        int index = 0;
        for (const auto& p : rail.points)
            if (p.row != row)
                op.sets.push_back({p.row, columnHash("id"), Cell{CellType::Int, index++, 0, {}}});
        return op;
    }
    Op movePoint(const Point& p, Vec3 to) {
        Op op;
        op.label = "Move path point";
        const auto delta = DebugDraw::vector(to) - DebugDraw::vector(p.position);
        setPosition(op, p.row, 0, to);
        setPosition(op, p.row, 1, DebugDraw::point(DebugDraw::vector(p.incoming) + delta));
        setPosition(op, p.row, 2, DebugDraw::point(DebugDraw::vector(p.outgoing) + delta));
        return op;
    }
    Op moveHandle(const Point& p, bool outgoing, Vec3 to) {
        Op op;
        op.label = "Move path handle";
        setPosition(op, p.row, outgoing ? 2 : 1, to);
        return op;
    }
    Op linkPath(const ModelView& m, RowId row, const Rail& rail, std::string* error) {
        int zone = -1, railZone = -1;
        const auto* id = m.cell(rail.row, columnHash("l_id"));
        if (!m.locate(row, &zone, nullptr) || !live(m, row) || !m.locate(rail.row, &railZone, nullptr) || !live(m, rail.row) || zone != railZone ||
            zone != rail.zone || rail.id < 0 || !id || id->i != rail.id || m.railPoints(rail.row).size() < 2) {
            if (error)
                *error = "Choose a path with at least two points in the object's zone.";
            return {};
        }
        Op op;
        op.label = "Link path";
        op.sets.push_back({row, columnHash("CommonPath_ID"), Cell{CellType::Int, rail.id, 0, {}}});
        return op;
    }
    Op unlink(RowId row, const std::string& field) {
        Op op;
        op.label = "Unlink " + field;
        op.sets.push_back({row, columnHash(field.c_str()), Cell{CellType::Int, -1, 0, {}}});
        return op;
    }
    std::vector< Socket > sockets(const ModelView& m, RowId row) {
        std::vector< Socket > result;
        int zone = -1;
        if (!m.locate(row, &zone, nullptr) || !live(m, row))
            return result;
        const char* name = m.objectName(row);
        const auto* info = name ? Catalog::find(name) : nullptr;
        if (!info)
            return result;
        for (const auto& sw : info->switches) {
            result.push_back({row, zone, integer(m, row, sw.field.c_str()), sw.field, sw.role});
        }
        return result;
    }
    std::vector< Socket > allSockets(const ModelView& m) {
        std::vector< Socket > result;
        for (int z = 0; z < m.zoneCount(); ++z)
            for (int t = 0; t < m.tableCount(z); ++t) {
                const std::string path = m.tablePath(z, t);
                if (path.rfind("jmp/placement/", 0) != 0 && path.rfind("jmp/mapparts/", 0) != 0 && path.rfind("jmp/childobj/", 0) != 0)
                    continue;
                m.forEachRow(z, t, [&](RowId row) {
                    if (!live(m, row))
                        return;
                    auto found = sockets(m, row);
                    result.insert(result.end(), found.begin(), found.end());
                });
            }
        return result;
    }
    std::vector< Wire > wires(const ModelView& m) {
        struct Net {
            std::vector< Socket > read, write;
        };
        std::map< std::pair< int, int >, Net > nets;
        for (const auto& s : allSockets(m)) {
            if (s.id < 0 || (s.id >= 128 && s.id < 1000) || s.id >= 1128)
                continue;
            auto& net = nets[{s.id >= 1000 ? -1 : s.zone, s.id}];
            (s.role == Catalog::SwitchRole::Write ? net.write : net.read).push_back(s);
        }
        std::vector< Wire > result;
        for (const auto& [key, net] : nets)
            for (const auto& from : net.write)
                for (const auto& to : net.read) {
                    if (from.row != to.row && (m.rowMissions(from.row) & m.rowMissions(to.row)))
                        result.push_back({from, to});
                }
        return result;
    }
    Op linkSwitchOp(const ModelView& m, const Socket& source, const Socket& target, std::string* error) {
        auto reject = [&](const char* message) {
            if (error)
                *error = message;
            return Op{};
        };
        const auto a = sockets(m, source.row), b = sockets(m, target.row);
        const auto sa = std::find_if(a.begin(), a.end(), [&](const Socket& s) { return s.field == source.field && s.role == source.role; });
        const auto sb = std::find_if(b.begin(), b.end(), [&](const Socket& s) { return s.field == target.field && s.role == target.role; });
        if (sa == a.end() || sb == b.end() || sa->role == sb->role || sa->row == sb->row)
            return reject("Connect an output to an input on another object.");
        auto valid = [](int id) { return (id >= 0 && id < 128) || (id >= 1000 && id < 1128); };
        const bool global = sa->zone != sb->zone || sa->id >= 1000 || sb->id >= 1000;
        int id = -1;
        if (global) {
            if (sa->id >= 1000 && sa->id < 1128)
                id = sa->id;
            else if (sb->id >= 1000 && sb->id < 1128)
                id = sb->id;
        } else {
            const Socket& writer = sa->role == Catalog::SwitchRole::Write ? *sa : *sb;
            const Socket& reader = sa->role == Catalog::SwitchRole::Read ? *sa : *sb;
            if (valid(writer.id))
                id = writer.id;
            else if (valid(reader.id))
                id = reader.id;
        }
        // Reserve every raw SW column, including unknown classes and inactive missions.
        std::vector< Socket > raw;
        for (int z = 0; z < m.zoneCount(); ++z)
            for (int t = 0; t < m.tableCount(z); ++t)
                m.forEachRow(z, t, [&](RowId row) {
                    if (!live(m, row))
                        return;
                    for (const auto& c : m.columns(z, t))
                        if (c.name && std::string(c.name).rfind("SW_", 0) == 0) {
                            const int value = integer(m, row, c.name);
                            if (valid(value))
                                raw.push_back({row, z, value, c.name});
                        }
                });
        if (id < 0) {
            const int first = global ? 1000 : 0;
            for (int candidate = first; candidate < first + 128; ++candidate) {
                const bool used =
                    std::any_of(raw.begin(), raw.end(), [&](const Socket& s) { return s.id == candidate && (global || s.zone == sa->zone); });
                if (!used) {
                    id = candidate;
                    break;
                }
            }
        }
        if (id < 0)
            return reject(global ? "All 128 galaxy-wide switches are in use." : "All 128 switches in this zone are in use.");
        Op op;
        op.label = "Link switches";
        std::map< std::pair< RowId, std::string >, int > changes;
        auto sameNet = [&](const Socket& s, const Socket& endpoint) {
            return valid(endpoint.id) && s.id == endpoint.id && (s.id >= 1000 || s.zone == endpoint.zone);
        };
        for (const auto& s : raw)
            if ((sameNet(s, *sa) || sameNet(s, *sb)) && s.id != id)
                changes[{s.row, s.field}] = id;
        changes[{sa->row, sa->field}] = id;
        changes[{sb->row, sb->field}] = id;
        for (const auto& [key, value] : changes)
            op.sets.push_back({key.first, columnHash(key.second.c_str()), Cell{CellType::Int, value, 0, {}}});
        return op;
    }
    std::vector< Check > computeChecks(const ModelView& m) {
        struct Use {
            std::vector< RowId > readers, writers;
        };
        std::map< std::pair< int, int >, std::vector< Socket > > nets;
        for (const auto& s : allSockets(m)) {
            if (s.id < 0 || (s.id >= 128 && s.id < 1000) || s.id >= 1128)
                continue;
            nets[{s.id >= 1000 ? -1 : s.zone, s.id}].push_back(s);
        }
        std::vector< Check > result;
        for (const auto& [key, net] : nets)
            for (int mission = 1; mission <= std::min(32, m.missionCount()); ++mission) {
                Use use;
                for (const auto& s : net)
                    if (m.rowMissions(s.row) & (1u << (mission - 1)))
                        (s.role == Catalog::SwitchRole::Read ? use.readers : use.writers).push_back(s.row);
                if (use.readers.empty() || !use.writers.empty())
                    continue;
                std::sort(use.readers.begin(), use.readers.end());
                use.readers.erase(std::unique(use.readers.begin(), use.readers.end()), use.readers.end());
                result.push_back({"switch-no-writer:" + std::to_string(key.first) + ":" + std::to_string(key.second) + ":" + std::to_string(mission),
                                  Severity::Warning,
                                  "Switch " + std::to_string(key.second) + " has receivers but no known object turns it on or off in mission " +
                                      std::to_string(mission),
                                  use.readers,
                                  "",
                                  {},
                                  "pick-switch"});
            }
        return result;
    }
    std::uint32_t switchColor(const std::string& field) {
        if (field == "SW_APPEAR")
            return 0xff79e66a;
        if (field == "SW_DEAD")
            return 0xff6262ff;
        if (field == "SW_A")
            return 0xffffda48;
        if (field == "SW_B")
            return 0xffff8dc3;
        if (field == "SW_SLEEP" || field == "SW_AWAKE")
            return 0xff65c9ff;
        if (field == "CommonPath_ID")
            return 0xff38ccfa;
        return 0xffe7e7e7;
    }
    std::uint32_t switchColor(int id) {
        static constexpr std::uint32_t colors[] = {0xff75d9ff, 0xffffb57a, 0xff8fe6a5, 0xffd991fa, 0xfff0ce7e, 0xff8799ff, 0xffd7e873};
        return colors[static_cast< unsigned >(std::max(id, 0)) % (sizeof(colors) / sizeof(colors[0]))];
    }
}  // namespace PetariEditor::Paths
