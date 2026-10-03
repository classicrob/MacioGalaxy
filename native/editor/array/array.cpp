#include <algorithm>
#include <cmath>
#include <petari/editor/array.hpp>

namespace PetariEditor::Arrays {
    namespace {
        namespace G = Gizmo;
        Parameters settings;
        std::uint64_t version = 0;
        View snapshot;
        Input pending;
        float finite(float value, float fallback) {
            return std::isfinite(value) ? value : fallback;
        }
        void flatten(Polyline& line, G::V3 a, G::V3 b, G::V3 c, G::V3 d, int depth) {
            const float polygon = G::length(b - a) + G::length(c - b) + G::length(d - c);
            if (depth == 12 || polygon - G::length(d - a) < 0.05f) {
                if (G::length(d - line.points.back()) > 1e-5f) {
                    line.distances.push_back(line.length() + G::length(d - line.points.back()));
                    line.points.push_back(d);
                }
                return;
            }
            const auto ab = (a + b) * 0.5f, bc = (b + c) * 0.5f, cd = (c + d) * 0.5f;
            const auto abc = (ab + bc) * 0.5f, bcd = (bc + cd) * 0.5f, mid = (abc + bcd) * 0.5f;
            flatten(line, a, ab, abc, mid, depth + 1);
            flatten(line, mid, bcd, cd, d, depth + 1);
        }
        G::Basis zoneBasis(const Mtx34& z) {
            return {{z.m[0][0], z.m[1][0], z.m[2][0]}, {z.m[0][1], z.m[1][1], z.m[2][1]}, {z.m[0][2], z.m[1][2], z.m[2][2]}};
        }
        G::V3 inverse(const G::Basis& b, G::V3 v) {
            return {G::dot(b.x, v), G::dot(b.y, v), G::dot(b.z, v)};
        }
    }  // namespace
    G::Basis alignAlong(G::V3 forward, G::V3 up) {
        const auto z = G::normalized(forward, {0, 0, 1});
        auto x = G::cross(up, z);
        if (G::length(x) < 1e-5f)
            x = G::cross(std::fabs(z.y) < .9f ? G::V3{0, 1, 0} : G::V3{1, 0, 0}, z);
        x = G::normalized(x);
        return {x, G::cross(z, x), z};
    }
    Parameters bounded(Parameters p) {
        p.count = std::clamp(p.count, 1, kMaxCopies);
        p.rows = std::clamp(p.rows, 1, 16);
        p.columns = std::clamp(p.columns, 1, 16);
        p.spacing = std::clamp(finite(p.spacing, 150), 1.f, 100000.f);
        p.radius = std::clamp(finite(p.radius, 400), 1.f, 100000.f);
        p.arcDegrees = std::clamp(finite(p.arcDegrees, 180), 1.f, 359.f);
        p.startDistance = std::max(0.f, finite(p.startDistance, 0));
        return p;
    }
    Polyline railPolyline(const Paths::Rail& rail, const Mtx34& zone) {
        Polyline line;
        line.closed = rail.closed;
        if (rail.points.empty())
            return line;
        const auto world = [&](Vec3 p) { return DebugDraw::vector(DebugDraw::transform(zone, p)); };
        line.points.push_back(world(rail.points.front().position));
        line.distances.push_back(0);
        const size_t segments = rail.closed ? rail.points.size() : rail.points.size() - 1;
        for (size_t i = 0; i < segments; ++i) {
            const auto& a = rail.points[i];
            const auto& b = rail.points[(i + 1) % rail.points.size()];
            flatten(line, world(a.position), world(a.outgoing), world(b.incoming), world(b.position), 0);
        }
        return line;
    }
    Sample atDistance(const Polyline& line, float distance) {
        if (line.points.size() < 2 || line.length() < 1e-5f)
            return {line.points.empty() ? G::V3{} : line.points.front(), {0, 0, 1}};
        distance = std::clamp(finite(distance, 0), 0.f, line.length());
        auto it = std::upper_bound(line.distances.begin(), line.distances.end(), distance);
        const size_t i = std::clamp< size_t >(it - line.distances.begin(), 1, line.points.size() - 1);
        const auto delta = line.points[i] - line.points[i - 1];
        const float span = line.distances[i] - line.distances[i - 1];
        return {line.points[i - 1] + delta * ((distance - line.distances[i - 1]) / span), G::normalized(delta, {0, 0, 1})};
    }
    std::vector< Sample > layout(const Parameters& parameters, G::V3 origin, const G::Basis& frame, const Polyline* rail) {
        const auto p = bounded(parameters);
        std::vector< Sample > result;
        if (p.pattern == Pattern::Path) {
            if (!rail || rail->length() < 1e-4f || p.startDistance > rail->length())
                return result;
            const float remaining = rail->length() - p.startDistance;
            const bool fullLoop = rail->closed && p.startDistance == 0;
            const int count =
                p.pathSpacing ? std::min(kMaxCopies, 1 + static_cast< int >(std::min(255.f, std::floor(remaining / p.spacing)))) : p.count;
            const float step = p.pathSpacing ? p.spacing : remaining / std::max(1, fullLoop ? count : count - 1);
            for (int i = 0; i < count; ++i) {
                const float distance = p.startDistance + step * i;
                if (fullLoop && i > 0 && distance >= rail->length() - 1e-4f)
                    break;
                result.push_back(atDistance(*rail, distance));
            }
            return result;
        }
        const int count = p.pattern == Pattern::Grid ? p.rows * p.columns : p.count;
        for (int i = 0; i < count; ++i) {
            G::V3 offset, forward = frame.z;
            if (p.pattern == Pattern::Line)
                offset = frame.z * (p.spacing * (i + 1));
            else if (p.pattern == Pattern::Grid) {
                // Start one step ahead so no copy coincides with the source.
                offset = frame.x * (p.spacing * (i % p.columns)) + frame.z * (p.spacing * (1 + i / p.columns));
            } else {
                const float angle = (p.pattern == Pattern::Ring ? 360.f * i / count : p.arcDegrees * i / std::max(1, count - 1)) * G::kDeg;
                const auto outward = frame.x * std::cos(angle) + frame.z * std::sin(angle);
                offset = outward * p.radius;
                forward = frame.x * (-std::sin(angle)) + frame.z * std::cos(angle);
                if (p.facing == Facing::Outward)
                    forward = outward;
                if (p.facing == Facing::Inward)
                    forward = outward * -1.f;
            }
            result.push_back({origin + offset, forward});
        }
        return result;
    }
    Op makeOp(const ModelView& model, const std::vector< Copy >& copies, const std::vector< RowId >& ids, std::string* error) {
        Op op;
        auto fail = [&](const char* message) {
            if (error)
                *error = message;
            return Op{};
        };
        if (copies.empty() || copies.size() > kMaxCopies || ids.size() != copies.size())
            return fail("Choose between 1 and 256 copies.");
        op.label = "Arrayed " + std::to_string(copies.size()) + " objects";
        std::vector< RowId > used;
        for (size_t i = 0; i < copies.size(); ++i) {
            const auto& c = copies[i];
            int zone, table;
            if (!model.locate(c.source, &zone, &table) || !model.hasTransform(c.source) || model.state(c.source) == RowState::Deleted)
                return fail("The source object is no longer available.");
            if (!ids[i].valid() || model.locate(ids[i], nullptr, nullptr) || std::find(used.begin(), used.end(), ids[i]) != used.end())
                return fail("Each copy needs a fresh row ID.");
            used.push_back(ids[i]);
            const auto z = model.zoneWorldMatrix(zone);
            const auto b = zoneBasis(z);
            const auto pos = DebugDraw::inverseTransform(z, DebugDraw::point(c.position));
            const auto dir = G::toEulerDegrees({inverse(b, c.basis.x), inverse(b, c.basis.y), inverse(b, c.basis.z)});
            const float values[] = {pos.x, pos.y, pos.z, dir.x, dir.y, dir.z};
            const char* columns[] = {"pos_x", "pos_y", "pos_z", "dir_x", "dir_y", "dir_z"};
            AddRow add;
            add.id = ids[i];
            add.zone = zone;
            add.table = model.tablePath(zone, table);
            add.copyOf = c.source;
            for (int j = 0; j < 6; ++j) {
                if (!std::isfinite(values[j]))
                    return fail("An array transform is not finite.");
                Cell value;
                value.type = CellType::Float;
                value.f = values[j];
                add.cells.push_back({columnHash(columns[j]), value});
            }
            op.adds.push_back(std::move(add));
        }
        if (error)
            error->clear();
        return op;
    }
    std::uint64_t revision() {
        return version;
    }
    const Parameters& parameters() {
        return settings;
    }
    void configure(Parameters p) {
        settings = bounded(p);
        ++version;
    }
    const View& view() {
        return snapshot;
    }
    void publish(View v) {
        snapshot = std::move(v);
    }
    void send(Input input) {
        pending.begin |= input.begin;
        pending.apply |= input.apply;
        pending.cancel |= input.cancel;
        pending.dragBegin |= input.dragBegin;
        pending.dragMove |= input.dragMove;
        pending.dragEnd |= input.dragEnd;
        if (input.dragBegin || input.dragMove) {
            pending.x = input.x;
            pending.y = input.y;
        }
    }
    Input take() {
        const auto out = pending;
        pending = {};
        return out;
    }
}  // namespace PetariEditor::Arrays
