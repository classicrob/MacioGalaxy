#ifdef PETARI_NATIVE
#include "Game/Camera/EditorPaths.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/GravityUtil.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <petari/editor/paths_view.hpp>
#include <petari/editor_pick.hpp>
#include <petari/host_allocation.hpp>
#include <set>

namespace EditorPaths {
    namespace {
        namespace E = PetariEditor;
        namespace P = E::Paths;
        namespace V = E::PathsView;
        namespace D = E::DebugDraw;
        D::Mesh mesh;
        const E::ModelView* cachedModel = nullptr;
        std::uint64_t cachedRevision = 0;
        std::string cachedGalaxy;
        std::vector< P::Rail > cachedRails;
        std::vector< P::Wire > cachedWires;
        struct Drag {
            bool active = false;
            P::Point original, current;
            int handle = 0, zone = 0;
            E::Vec3 anchor, press;
            std::uint64_t revision = 0;
        } drag;
        constexpr std::uint32_t gold = 0xff38ccfa, cyan = 0xffffdb66;
        std::vector< E::RowId > selection() {
            std::vector< E::RowId > rows;
            for (const auto& item : PetariNative::EditorPick::selection())
                if (item.row)
                    rows.push_back({item.row});
            return rows;
        }
        bool selected(const std::vector< E::RowId >& rows, E::RowId row) {
            return std::find(rows.begin(), rows.end(), row) != rows.end();
        }
        E::Vec3 anchor(const E::ModelView& model, E::RowId row) {
            if (LiveActor* actor = E::actorForRow(row)) {
                float c[3], radius;
                EditorPicking::bounds(actor, c, &radius);
                return {c[0], c[1], c[2]};
            }
            int zone = 0;
            model.locate(row, &zone, nullptr);
            return D::transform(model.zoneWorldMatrix(zone), model.transform(row).pos);
        }
        E::Vec3 upAt(E::Vec3 p) {
            TVec3f gravity(0, -1, 0);
            MR::calcGravityVector(nullptr, TVec3f(p.x, p.y, p.z), &gravity, nullptr, 0);
            return D::point(E::Gizmo::normalized({-gravity.x, -gravity.y, -gravity.z}));
        }
        void segment(V::View& out, const D::Camera& camera, E::RowId rail, int index, float t0, float t1, E::Vec3 a, E::Vec3 b, std::uint32_t color) {
            mesh.lines.push_back({a, b, color, 2});
            V::Curve c;
            c.rail = rail;
            c.segment = index;
            c.t0 = t0;
            c.t1 = t1;
            if (camera.project(a, &c.x0, &c.y0) && camera.project(b, &c.x1, &c.y1))
                out.curves.push_back(c);
        }
        void mark(V::View& out, const D::Camera& camera, E::RowId rail, E::RowId point, int handle, E::Vec3 p) {
            V::Mark m;
            m.rail = rail;
            m.point = point;
            m.handle = handle;
            if (camera.project(p, &m.x, &m.y))
                out.marks.push_back(m);
        }
        std::uint32_t mixColor(std::uint32_t a, std::uint32_t b, float t) {
            std::uint32_t result = 0xff000000;
            for (int shift : {0, 8, 16}) {
                const float from = (a >> shift) & 255, to = (b >> shift) & 255;
                result |= static_cast< std::uint32_t >(from + (to - from) * t) << shift;
            }
            return result;
        }
        void curveWire(const D::Camera& camera, E::Vec3 a, E::Vec3 b, V::WireMark* out) {
            const auto delta = D::vector(b) - D::vector(a);
            const float rise = std::min(E::Gizmo::length(delta) * 0.35f, 2500.0f);
            P::Point first{{}, a, {}, D::point(D::vector(a) + delta * (1.0f / 3) + D::vector(upAt(a)) * rise)};
            P::Point last{{}, b, D::point(D::vector(a) + delta * (2.0f / 3) + D::vector(upAt(b)) * rise), {}};
            const float width = V::wireFocused(out->to) ? 4.5f : 3.0f;
            const auto fromColor = P::switchColor(out->from.field), toColor = P::switchColor(out->to.field);
            std::array< E::Vec3, 33 > points;
            for (int i = 0; i <= 32; ++i)
                points[i] = P::evaluate(first, last, i / 32.0f);
            // A dark edge preserves contrast against sun, bright models and busy textures.
            for (int i = 1; i <= 32; ++i)
                mesh.lines.push_back({points[i - 1], points[i], 0xee221911, width + 2});
            const double seconds = std::chrono::duration< double >(std::chrono::steady_clock::now().time_since_epoch()).count();
            const int phase = static_cast< int >(std::fmod(seconds * 12, 6));
            for (int i = 1; i <= 32; ++i) {
                auto color = mixColor(fromColor, toColor, (i - 0.5f) / 32.0f);
                if ((i + 6 - phase) % 6 < 3)
                    color = mixColor(color, 0xffffffff, 0.18f);
                mesh.lines.push_back({points[i - 1], points[i], color, width});
                V::Curve c;
                if (camera.project(points[i - 1], &c.x0, &c.y0) && camera.project(points[i], &c.x1, &c.y1))
                    out->segments.push_back(c);
                if (i == 16)
                    D::arrow(mesh, points[i - 1], points[i], upAt(points[i]), color, width);
            }
            out->midpointVisible = camera.project(points[16], &out->x, &out->y);
            out->fromVisible = camera.project(a, &out->fromX, &out->fromY);
            out->toVisible = camera.project(b, &out->toX, &out->toY);
        }

    }  // namespace
    void update(const EditorPicking::View& input) {
        PetariNative::HostAllocationScope host;
        mesh = {};
        V::View out;
        const E::ModelView* model = E::model();
        if (!model || V::visibility() == V::Visibility::None) {
            drag.active = false;
            V::take();
            V::publish(std::move(out));
            return;
        }
        D::Camera camera{{input.eye[0], input.eye[1], input.eye[2]},
                         {input.forward[0], input.forward[1], input.forward[2]},
                         {input.up[0], input.up[1], input.up[2]},
                         input.fovy,
                         input.aspect};
        const auto rows = selection();
        if (cachedModel != model || cachedRevision != model->revision() || cachedGalaxy != model->galaxy()) {
            cachedModel = model;
            cachedRevision = model->revision();
            cachedGalaxy = model->galaxy();
            cachedRails = P::collect(*model);
            cachedWires = P::wires(*model);
        }
        auto rails = cachedRails;
        const auto request = V::take();
        if (drag.active && drag.revision != model->revision())
            drag.active = false;
        if (request.begin) {
            for (const auto& rail : rails)
                for (const auto& p : rail.points)
                    if (p.row == request.point) {
                        drag = {};
                        drag.original = drag.current = p;
                        drag.handle = request.handle;
                        drag.zone = rail.zone;
                        drag.revision = model->revision();
                        const auto local = request.handle == 0 ? p.position : request.handle == 1 ? p.incoming : p.outgoing;
                        drag.anchor = D::transform(model->zoneWorldMatrix(rail.zone), local);
                        drag.active = camera.dragPlane(request.x, request.y, drag.anchor, &drag.press);
                    }
        }
        if (drag.active && request.move) {
            E::Vec3 cursor;
            if (camera.dragPlane(request.x, request.y, drag.anchor, &cursor)) {
                const auto world = D::point(D::vector(drag.anchor) + D::vector(cursor) - D::vector(drag.press));
                const auto local = D::inverseTransform(model->zoneWorldMatrix(drag.zone), world);
                drag.current = drag.original;
                if (drag.handle == 0) {
                    const auto delta = D::vector(local) - D::vector(drag.original.position);
                    drag.current.position = local;
                    drag.current.incoming = D::point(D::vector(drag.original.incoming) + delta);
                    drag.current.outgoing = D::point(D::vector(drag.original.outgoing) + delta);
                } else if (drag.handle == 1)
                    drag.current.incoming = local;
                else
                    drag.current.outgoing = local;
            }
        }
        if (request.cancel)
            drag.active = false;
        if (drag.active) {
            for (auto& rail : rails)
                for (auto& p : rail.points)
                    if (p.row == drag.original.row)
                        p = drag.current;
            out.dragging = true;
            if (request.end) {
                E::Op op = drag.handle == 0 ?
                               P::movePoint(drag.original, drag.current.position) :
                               P::moveHandle(drag.original, drag.handle == 2, drag.handle == 1 ? drag.current.incoming : drag.current.outgoing);
                bool changed = false;
                for (const auto& set : op.sets) {
                    const auto* old = model->cell(set.row, set.column);
                    changed = changed || !old || std::fabs(old->f - set.after.f) > 0.0001f;
                }
                if (changed) {
                    E::Command command{E::Command::Kind::Apply};
                    command.op = std::move(op);
                    E::enqueue(std::move(command));
                }
                drag.active = false;
            }
        }
        for (const auto& rail : rails) {
            if (!model->zonePlaced(rail.zone))
                continue;
            bool focus = selected(rows, rail.row);
            for (const auto& p : rail.points)
                focus = focus || selected(rows, p.row);
            std::vector< E::RowId > riders;
            for (E::RowId row : rows) {
                int zone = -1;
                model->locate(row, &zone, nullptr);
                const E::Cell* c = model->cell(row, E::columnHash("CommonPath_ID"));
                if (zone == rail.zone && c && c->i == rail.id) {
                    focus = true;
                    riders.push_back(row);
                }
            }
            if (!focus && V::visibility() != V::Visibility::All && !V::linkingPath())
                continue;
            const auto matrix = model->zoneWorldMatrix(rail.zone);
            const auto color = focus ? gold : 0x8875d9ff;
            const int count = static_cast< int >(rail.points.size());
            for (int i = 0; i < count; ++i) {
                const auto& p = rail.points[i];
                const auto world = D::transform(matrix, p.position);
                mark(out, camera, rail.row, p.row, 0, world);
                if (selected(rows, p.row)) {
                    for (int h = 1; h <= 2; ++h) {
                        const auto handle = D::transform(matrix, h == 1 ? p.incoming : p.outgoing);
                        mesh.lines.push_back({world, handle, cyan, 1});
                        mark(out, camera, rail.row, p.row, h, handle);
                    }
                }
                if (i + 1 == count && !rail.closed)
                    continue;
                if (count < 2)
                    continue;
                const auto& next = rail.points[(i + 1) % count];
                auto previous = world;
                for (int s = 1; s <= 32; ++s) {
                    const auto to = D::transform(matrix, P::evaluate(p, next, s / 32.0f));
                    segment(out, camera, rail.row, i, (s - 1) / 32.0f, s / 32.0f, previous, to, color);
                    if (s == 16)
                        D::arrow(mesh, previous, to, upAt(to), color);
                    previous = to;
                }
            }
            for (auto row : riders) {
                if (rail.points.empty())
                    continue;
                const auto a = anchor(*model, row);
                auto nearest = D::transform(matrix, rail.points.front().position);
                float distance = E::Gizmo::length(D::vector(a) - D::vector(nearest));
                for (const auto& point : rail.points) {
                    const auto p = D::transform(matrix, point.position);
                    const float d = E::Gizmo::length(D::vector(a) - D::vector(p));
                    if (d < distance) {
                        distance = d;
                        nearest = p;
                    }
                }
                mesh.lines.push_back({a, nearest, cyan, 1});
            }
        }
        for (const auto& wire : cachedWires) {
            const bool focus = selected(rows, wire.from.row) || selected(rows, wire.to.row);
            if (!focus && V::visibility() != V::Visibility::All)
                continue;
            if (!(model->rowMissions(wire.from.row) & model->rowMissions(wire.to.row) & (1u << (model->scenario() - 1))))
                continue;
            V::WireMark w;
            w.from = wire.from;
            w.to = wire.to;
            curveWire(camera, anchor(*model, wire.from.row), anchor(*model, wire.to.row), &w);
            out.wires.push_back(std::move(w));
        }
        for (auto row : rows) {
            if (!model->hasTransform(row))
                continue;
            float x, y;
            if (!camera.project(anchor(*model, row), &x, &y))
                continue;
            auto sockets = P::sockets(*model, row);
            const char* name = model->objectName(row);
            const auto* info = name ? E::Catalog::find(name) : nullptr;
            bool path = false;
            if (info)
                for (const auto& link : info->links)
                    path = path || link.kind == E::Catalog::LinkKind::Rail;
            if (path || model->cell(row, E::columnHash("CommonPath_ID")))
                sockets.push_back({row, 0, -1, "CommonPath_ID"});
            for (std::size_t i = 0; i < sockets.size(); ++i) {
                const auto& socket = sockets[i];
                out.sockets.push_back({row, socket.field, x, y, P::switchColor(socket.field), socket.role == E::Catalog::SwitchRole::Write});
            }
        }
        V::publish(std::move(out));
    }
    void draw() {
        D::draw(mesh);
    }
}  // namespace EditorPaths
#endif
