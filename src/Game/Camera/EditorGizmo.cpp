#ifdef PETARI_NATIVE
// The level editor's transform gizmo (Game/Camera/EditorGizmo.hpp, UX.md section 6).
#include "Game/Camera/EditorGizmo.hpp"
#include "Game/Camera/EditorPicking.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Util/CollisionPartsFilter.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include <petari/editor/edit_api.hpp>
#include <petari/editor/gizmo_math.hpp>
#include <petari/editor_pick.hpp>
#include <petari/host_allocation.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
    namespace G = PetariEditor::Gizmo;
    namespace UI = PetariNative::EditorGizmo;
    using G::V3;

    constexpr float kRayLength = 400000.0f;
    constexpr float kHandleSize = 0.16f;   // the gizmo's arm, in viewport heights
    constexpr float kGrab = 0.025f;        // how near (viewport heights) the cursor must be to a handle
    constexpr float kAngleSnap = 15.0f;    // degrees (UX section 6: angle snap 15, on by default)
    constexpr float kDropProbe = 5000.0f;  // how far along gravity End looks for the ground

    // Handles: the three axes, the centre (slide over the ground), and a ring per axis (rotate).
    enum Handle { None = -1, AxisX, AxisY, AxisZ, Center, RingX, RingY, RingZ, Body };

    struct Item {
        PetariEditor::RowId row;
        LiveActor* actor = nullptr;
        int zone = 0;
        V3 pos;
        G::Basis basis;
        V3 scale{1, 1, 1};
        float height = 0.0f;  // above the ground along gravity, for slides
    };

    struct Drag {
        bool active = false;
        bool moved = false;
        Handle handle = None;
        std::vector< Item >* items = nullptr;  // start transforms
        V3 origin;
        G::Basis frame;
        float startT = 0.0f;   // along the axis (move, scale)
        V3 startPoint;         // on the rotate plane
        float startScreen = 0.0f;
        V3 lastDelta;
        float lastAngle = 0.0f;
        float lastFactor = 1.0f;
    } gDrag;
    char gStatus[96] = {};  // the drag's footer line

    // The play marker (UX section 3): where Play puts Mario. It starts where Mario stood when Edit
    // was first entered in this galaxy; M drops it on the ground under the cursor.
    struct Marker {
        bool valid = false;
        V3 pos, up{0, 1, 0}, front{0, 0, 1};
        char stage[48] = {};
    } gMarker;

    void sendMarker() {
        PetariEditor::Command command{PetariEditor::Command::Kind::SetPlayMarker};
        command.markerPos = {gMarker.pos.x, gMarker.pos.y, gMarker.pos.z};
        command.markerUp = {gMarker.up.x, gMarker.up.y, gMarker.up.z};
        command.markerFront = {gMarker.front.x, gMarker.front.y, gMarker.front.z};
        PetariEditor::enqueue(std::move(command));
    }

    V3 v(const TVec3f& t) { return {t.x, t.y, t.z}; }
    TVec3f tv(V3 a) { return TVec3f(a.x, a.y, a.z); }

    G::Basis zoneBasis(const PetariEditor::Mtx34& m) {
        G::Basis b;
        b.x = {m.m[0][0], m.m[1][0], m.m[2][0]};
        b.y = {m.m[0][1], m.m[1][1], m.m[2][1]};
        b.z = {m.m[0][2], m.m[1][2], m.m[2][2]};
        return b;
    }
    V3 apply(const G::Basis& b, V3 p) { return b.x * p.x + b.y * p.y + b.z * p.z; }
    V3 applyInverse(const G::Basis& b, V3 p) { return {G::dot(b.x, p), G::dot(b.y, p), G::dot(b.z, p)}; }
    G::Basis compose(const G::Basis& outer, const G::Basis& inner) {
        return {apply(outer, inner.x), apply(outer, inner.y), apply(outer, inner.z)};
    }
    G::Basis composeInverse(const G::Basis& outer, const G::Basis& world) {
        return {applyInverse(outer, world.x), applyInverse(outer, world.y), applyInverse(outer, world.z)};
    }

    V3 gravityUp(V3 at, V3 fallback) {
        TVec3f gravity(0.0f, 0.0f, 0.0f);
        if (MR::calcGravityVectorOrZero(nullptr, tv(at), &gravity, nullptr, 0) && G::length(v(gravity)) > 1e-4f) {
            return G::normalized(v(gravity) * -1.0f, fallback);
        }
        return fallback;
    }

    bool hitGround(V3 from, V3 dir, const LiveActor* ignore, V3* hit) {
        TVec3f point;
        const CollisionPartsFilterActor filter(ignore);
        if (MR::getFirstPolyOnLineToMap(&point, nullptr, tv(from), tv(dir), ignore != nullptr ? &filter : nullptr, nullptr)) {
            *hit = v(point);
            return true;
        }
        return false;
    }

    // The selection's rows with their world transforms now.
    std::vector< Item > gather() {
        std::vector< Item > items;
        const PetariEditor::ModelView* model = PetariEditor::model();
        if (model == nullptr) return items;
        for (int i = 0; i < EditorPicking::selectedCount(); i++) {
            LiveActor* actor = EditorPicking::selectedAt(i);
            Item item;  // M3 areas: row-only selection uses the same transform operations.
            item.actor = actor;
            item.row = {EditorPicking::selectedRowAt(i)};
            if (!actor && !model->cell(item.row, PetariEditor::columnHash("pos_x")))
                continue;
            int table = 0;
            if (!item.row.valid() || !model->hasTransform(item.row) || !model->locate(item.row, &item.zone, &table)) continue;
            const PetariEditor::RowTransform t = model->transform(item.row);
            const PetariEditor::Mtx34 zone = model->zoneWorldMatrix(item.zone);
            const G::Basis zb = zoneBasis(zone);
            item.pos = apply(zb, {t.pos.x, t.pos.y, t.pos.z}) + V3{zone.m[0][3], zone.m[1][3], zone.m[2][3]};
            item.basis = compose(zb, G::fromEulerDegrees({t.dirDeg.x, t.dirDeg.y, t.dirDeg.z}));
            item.scale = {t.scale.x, t.scale.y, t.scale.z};
            const V3 up = gravityUp(item.pos, item.basis.y);
            V3 ground;
            item.height = hitGround(item.pos + up * 10.0f, up * -kDropProbe, actor, &ground) ? G::dot(item.pos - ground, up) : 0.0f;
            items.push_back(item);
        }
        return items;
    }

    PetariEditor::RowTransform toRow(const Item& item) {
        const PetariEditor::Mtx34 zone = PetariEditor::model()->zoneWorldMatrix(item.zone);
        const G::Basis zb = zoneBasis(zone);
        const V3 local = applyInverse(zb, item.pos - V3{zone.m[0][3], zone.m[1][3], zone.m[2][3]});
        const V3 euler = G::toEulerDegrees(composeInverse(zb, item.basis));
        PetariEditor::RowTransform t;
        t.pos = {local.x, local.y, local.z};
        t.dirDeg = {euler.x, euler.y, euler.z};
        t.scale = {item.scale.x, item.scale.y, item.scale.z};
        return t;
    }

    void preview(const std::vector< Item >& items) {
        PetariEditor::Command command{PetariEditor::Command::Kind::Preview};
        for (const Item& item : items) {
            command.rows.push_back(item.row);
            command.previews.push_back(toRow(item));
        }
        PetariEditor::enqueue(std::move(command));
    }

    PetariEditor::Cell floatCell(float value) {
        PetariEditor::Cell cell;
        cell.type = PetariEditor::CellType::Float;
        cell.f = value;
        return cell;
    }

    // One undo step for the whole gesture (TECH.md 2.8: drags coalesce).
    void commit(const std::vector< Item >& items, const char* verb) {
        const PetariEditor::ModelView* model = PetariEditor::model();
        PetariEditor::Command command{PetariEditor::Command::Kind::Apply};
        char label[96];
        const char* name = items.size() == 1 ? model->objectName(items[0].row) : nullptr;
        if (items.size() == 1) {
            std::snprintf(label, sizeof(label), "%s %s", verb, name != nullptr ? name : "object");
        } else {
            std::snprintf(label, sizeof(label), "%s %d objects", verb, static_cast< int >(items.size()));
        }
        command.op.label = label;
        static const char* const kColumns[9] = {"pos_x", "pos_y", "pos_z", "dir_x", "dir_y", "dir_z", "scale_x", "scale_y", "scale_z"};
        for (const Item& item : items) {
            const PetariEditor::RowTransform t = toRow(item);
            const float values[9] = {t.pos.x, t.pos.y, t.pos.z, t.dirDeg.x, t.dirDeg.y, t.dirDeg.z, t.scale.x, t.scale.y, t.scale.z};
            for (int c = 0; c < 9; c++) {
                command.op.sets.push_back({item.row, PetariEditor::columnHash(kColumns[c]), floatCell(values[c])});
            }
        }
        std::fprintf(stderr, "[editor] gizmo: %s (%d cells)\n", label, static_cast< int >(command.op.sets.size()));
        PetariEditor::enqueue(std::move(command));
    }

    // The gizmo's axes at the selection's first object.
    G::Basis axes(const Item& primary) {
        switch (UI::frame()) {
        case UI::Frame::World: return G::Basis{};
        case UI::Frame::Local: return primary.basis;
        case UI::Frame::Gravity: break;
        }
        return G::gravityFrame(gravityUp(primary.pos, primary.basis.y), primary.basis.z);
    }

    struct Camera {
        V3 eye, forward, right, up;
        float tanY, tanX;
        bool project(V3 p, float* x, float* y) const {
            const V3 d = p - eye;
            const float z = G::dot(d, forward);
            if (z < 1.0f) return false;
            *x = G::dot(d, right) / (z * tanX);
            *y = G::dot(d, up) / (z * tanY);
            return true;
        }
        V3 ray(float x, float y) const { return G::normalized(forward + right * (x * tanX) + up * (y * tanY)); }
        float worldPerHeight(V3 at) const { return std::fmax(G::dot(at - eye, forward), 1.0f) * 2.0f * tanY; }
    };

    Camera cameraOf(const EditorPicking::View& view) {
        Camera c;
        c.eye = {view.eye[0], view.eye[1], view.eye[2]};
        c.forward = {view.forward[0], view.forward[1], view.forward[2]};
        c.right = G::normalized(G::cross(c.forward, {view.up[0], view.up[1], view.up[2]}), {1, 0, 0});
        c.up = G::cross(c.right, c.forward);
        c.tanY = std::tan(view.fovy * 0.5f * G::kDeg);
        c.tanX = c.tanY * view.aspect;
        return c;
    }

    // Distance (in viewport heights) from the cursor to a projected segment.
    float segmentDistance(float px, float py, float ax, float ay, float bx, float by, float aspect) {
        ax *= aspect, bx *= aspect, px *= aspect;
        const float dx = bx - ax, dy = by - ay;
        const float len2 = dx * dx + dy * dy;
        float t = len2 > 1e-9f ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0f;
        t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
        const float ex = ax + dx * t - px, ey = ay + dy * t - py;
        return std::sqrt(ex * ex + ey * ey) * 0.5f;  // -1..1 spans two heights
    }

    constexpr std::uint32_t abgr(int r, int g, int b, int a = 255) {
        return static_cast< std::uint32_t >(a) << 24 | static_cast< std::uint32_t >(b) << 16 | static_cast< std::uint32_t >(g) << 8 |
               static_cast< std::uint32_t >(r);
    }
    constexpr std::uint32_t kGold = abgr(250, 204, 56), kCyan = abgr(102, 219, 255);
    constexpr std::uint32_t kAxisColor[3] = {abgr(255, 90, 90), abgr(95, 227, 106), abgr(90, 155, 255)};

    struct Geometry {
        std::vector< UI::Line > lines;
        std::vector< std::pair< Handle, UI::Line > > hits;  // for hit testing (unhighlighted copies)
    };

    void addSegment(Geometry& g, const Camera& cam, V3 a, V3 b, std::uint32_t color, float width, Handle handle) {
        UI::Line line;
        if (!cam.project(a, &line.x0, &line.y0) || !cam.project(b, &line.x1, &line.y1)) return;
        line.color = color;
        line.width = width;
        g.lines.push_back(line);
        if (handle != None) g.hits.push_back({handle, line});
    }

    Geometry build(const Camera& cam, V3 origin, const G::Basis& frame, UI::Mode mode, Handle hot) {
        Geometry g;
        const float size = cam.worldPerHeight(origin) * kHandleSize;
        const V3 axis[3] = {frame.x, frame.y, frame.z};
        auto colorOf = [&](Handle h, std::uint32_t base) { return h == hot || h == gDrag.handle ? kGold : base; };
        if (mode == UI::Mode::Move || mode == UI::Mode::Scale) {
            for (int i = 0; i < 3; i++) {
                const Handle h = static_cast< Handle >(AxisX + i);
                const V3 tip = origin + axis[i] * size;
                addSegment(g, cam, origin, tip, colorOf(h, kAxisColor[i]), 4.0f, h);
                // Arrow head (move) or a small square end (scale), drawn as two short strokes.
                const V3 side = G::normalized(G::cross(axis[i], cam.forward), axis[(i + 1) % 3]) * (size * 0.08f);
                const V3 back = tip - axis[i] * (size * 0.16f);
                if (mode == UI::Mode::Move) {
                    addSegment(g, cam, tip, back + side, colorOf(h, kAxisColor[i]), 4.0f, None);
                    addSegment(g, cam, tip, back - side, colorOf(h, kAxisColor[i]), 4.0f, None);
                } else {
                    addSegment(g, cam, tip - side, tip + side, colorOf(h, kAxisColor[i]), 7.0f, None);
                }
            }
        }
        if (mode == UI::Mode::Rotate) {
            for (int i = 0; i < 3; i++) {
                const Handle h = static_cast< Handle >(RingX + i);
                const V3 a = axis[(i + 1) % 3], b = axis[(i + 2) % 3];
                // The ring about up is the one you want most: gold-tinted and thickest (UX section 6).
                const std::uint32_t base = i == 1 ? abgr(250, 220, 120) : kAxisColor[i];
                const float width = i == 1 ? 4.0f : 2.5f;
                const int segments = 48;
                for (int s = 0; s < segments; s++) {
                    const float t0 = 2.0f * G::kPi * s / segments, t1 = 2.0f * G::kPi * (s + 1) / segments;
                    addSegment(g, cam, origin + (a * std::cos(t0) + b * std::sin(t0)) * size,
                               origin + (a * std::cos(t1) + b * std::sin(t1)) * size, colorOf(h, base), width, h);
                }
            }
        }
        // The centre: slide over the ground (move), uniform scale (scale).
        if (mode != UI::Mode::Select) {
            const V3 r = cam.right * (size * 0.07f), u = cam.up * (size * 0.07f);
            for (int s = 0; s < 4; s++) {
                const V3 c0 = (s == 0 ? r + u : s == 1 ? u - r : s == 2 ? (r + u) * -1.0f : r - u);
                const V3 c1 = (s == 0 ? u - r : s == 1 ? (r + u) * -1.0f : s == 2 ? r - u : r + u);
                addSegment(g, cam, origin + c0, origin + c1, colorOf(Center, kCyan), 3.0f, Center);
            }
        }
        return g;
    }

    Handle hitTest(const Geometry& g, float x, float y, float aspect) {
        Handle best = None;
        float bestDistance = kGrab;
        for (const auto& [handle, line] : g.hits) {
            const float d = segmentDistance(x, y, line.x0, line.y0, line.x1, line.y1, aspect);
            if (d < bestDistance || (handle == Center && d < kGrab * 1.5f && best != Center)) {
                if (handle == Center || d < bestDistance) {
                    best = handle;
                    bestDistance = d;
                }
            }
        }
        return best;
    }

    // Slides each object over the ground under its new place, keeping its height and standing it up
    // for the gravity there (UX section 6 "Moving on a planet").
    void settle(Item& item, const Item& start) {
        const V3 up = gravityUp(item.pos, start.basis.y);
        V3 ground;
        if (hitGround(item.pos + up * 2000.0f, up * -(4000.0f + kDropProbe), start.actor, &ground)) {
            item.pos = ground + up * start.height;
        }
        item.basis = G::alignUp(start.basis, up);
    }

    void applyDrag(const Camera& cam, float cx, float cy, bool snap, std::vector< Item >& items) {
        const std::vector< Item >& start = *gDrag.items;
        items = start;
        const V3 ray = cam.ray(cx, cy);
        const V3 axis[3] = {gDrag.frame.x, gDrag.frame.y, gDrag.frame.z};
        char status[96] = {};
        switch (gDrag.handle) {
        case AxisX:
        case AxisY:
        case AxisZ: {
            const V3 a = axis[gDrag.handle - AxisX];
            float t = 0.0f;
            if (!G::closestOnAxis(cam.eye, ray, gDrag.origin, a, &t)) return;
            if (UI::mode() == UI::Mode::Scale) {
                float factor = std::fabs(gDrag.startT) > 1e-3f ? t / gDrag.startT : 1.0f;
                factor = factor < 0.05f ? 0.05f : factor;
                if (snap) factor = std::fmax(0.05f, G::snap(factor, 0.1f));
                for (Item& item : items) {
                    // Scale along the gizmo axis, expressed on the object's own axes.
                    const V3 own[3] = {item.basis.x, item.basis.y, item.basis.z};
                    float* s[3] = {&item.scale.x, &item.scale.y, &item.scale.z};
                    for (int k = 0; k < 3; k++) {
                        const float along = std::fabs(G::dot(own[k], a));
                        *s[k] *= 1.0f + (factor - 1.0f) * along;
                    }
                }
                gDrag.lastFactor = factor;
                std::snprintf(status, sizeof(status), "Scale %c  x%.2f", "XYZ"[gDrag.handle - AxisX], factor);
            } else {
                const float d = t - gDrag.startT;  // (grid snapping is off by default, UX section 6)
                for (Item& item : items) item.pos = item.pos + a * d;
                gDrag.lastDelta = a * d;
                std::snprintf(status, sizeof(status), "Move %c  %+.1f", "XYZ"[gDrag.handle - AxisX], d);
            }
            break;
        }
        case RingX:
        case RingY:
        case RingZ: {
            const V3 a = axis[gDrag.handle - RingX];
            V3 point;
            if (!G::rayPlane(cam.eye, ray, gDrag.origin, a, &point)) return;
            float angle = G::angleAbout(a, gDrag.startPoint - gDrag.origin, point - gDrag.origin);
            if (snap) angle = G::snap(angle / G::kDeg, kAngleSnap) * G::kDeg;
            for (Item& item : items) {
                item.basis = G::rotate(item.basis, a, angle);
                item.pos = gDrag.origin + G::rotate(item.pos - gDrag.origin, a, angle);
            }
            gDrag.lastAngle = angle;
            std::snprintf(status, sizeof(status), "Rotate %c  %+.0f deg", "XYZ"[gDrag.handle - RingX], angle / G::kDeg);
            break;
        }
        case Center:
        case Body: {
            if (UI::mode() == UI::Mode::Scale && gDrag.handle == Center) {
                float x0 = 0, y0 = 0;
                cam.project(gDrag.origin, &x0, &y0);
                const float now = std::sqrt((cx - x0) * (cx - x0) + (cy - y0) * (cy - y0));
                float factor = gDrag.startScreen > 1e-3f ? now / gDrag.startScreen : 1.0f;
                factor = factor < 0.05f ? 0.05f : factor;
                if (snap) factor = std::fmax(0.05f, G::snap(factor, 0.1f));
                for (Item& item : items) item.scale = item.scale * factor;
                std::snprintf(status, sizeof(status), "Scale  x%.2f", factor);
                break;
            }
            // Slide over the ground under the cursor.
            V3 ground;
            if (!hitGround(cam.eye, ray * kRayLength, start[0].actor, &ground)) return;
            const V3 upThere = gravityUp(ground, start[0].basis.y);
            const V3 delta = ground + upThere * start[0].height - start[0].pos;
            for (size_t i = 0; i < items.size(); i++) {
                items[i].pos = start[i].pos + delta;
                settle(items[i], start[i]);
            }
            std::snprintf(status, sizeof(status), "Move on the ground  %.0f", G::length(delta));
            break;
        }
        default: return;
        }
        std::snprintf(gStatus, sizeof(gStatus), "%s", status);
    }

    // The Browser's Place (UX section 7): a copy of the example row (class and settings) on the ground
    // under the cursor, upright for the gravity there and facing the camera, in the zone that owns that
    // ground (else the example's). One op; the new row is selected once Runtime has created it.
    void place(const Camera& cam, float cx, float cy, PetariEditor::RowId example) {
        const PetariEditor::ModelView* model = PetariEditor::model();
        int exampleZone = 0, exampleTable = 0;
        if (model == nullptr || !model->locate(example, &exampleZone, &exampleTable)) return;
        TVec3f point;
        Triangle triangle;
        const V3 ray = cam.ray(cx, cy);
        if (!MR::getFirstPolyOnLineToMap(&point, &triangle, tv(cam.eye), tv(ray * kRayLength))) {
            std::fprintf(stderr, "[editor] place: no ground under the cursor\n");
            return;
        }
        int zone = exampleZone;
        if (triangle.mSensor != nullptr && triangle.mSensor->mHost != nullptr) {
            const PetariEditor::RowId host = PetariEditor::rowForActor(triangle.mSensor->mHost);
            int hostZone = 0, hostTable = 0;
            if (host.valid() && model->locate(host, &hostZone, &hostTable)) zone = hostZone;
        }
        Item item;
        item.zone = zone;
        item.pos = v(point);
        const V3 up = gravityUp(item.pos, {0, 1, 0});
        item.basis = G::gravityFrame(up, cam.forward * -1.0f);  // facing the camera
        const PetariEditor::RowTransform exampleTransform = model->transform(example);
        item.scale = {exampleTransform.scale.x, exampleTransform.scale.y, exampleTransform.scale.z};
        const PetariEditor::RowTransform t = toRow(item);

        PetariEditor::AddRow add;
        add.id = PetariEditor::newRowId();
        add.zone = zone;
        add.table = model->tablePath(exampleZone, exampleTable);
        add.copyOf = example;
        static const char* const kColumns[9] = {"pos_x", "pos_y", "pos_z", "dir_x", "dir_y", "dir_z", "scale_x", "scale_y", "scale_z"};
        const float values[9] = {t.pos.x, t.pos.y, t.pos.z, t.dirDeg.x, t.dirDeg.y, t.dirDeg.z, t.scale.x, t.scale.y, t.scale.z};
        for (int c = 0; c < 9; c++) add.cells.push_back({PetariEditor::columnHash(kColumns[c]), floatCell(values[c])});
        PetariEditor::Command command{PetariEditor::Command::Kind::Apply};
        const char* name = model->objectName(example);
        command.op.label = std::string("Placed ") + (name != nullptr ? name : "object");
        command.op.adds.push_back(add);
        std::fprintf(stderr, "[editor] place %s in %s at (%.0f, %.0f, %.0f)\n", name != nullptr ? name : "?", model->zoneName(zone), point.x,
                     point.y, point.z);
        PetariEditor::enqueue(std::move(command));
        PetariNative::EditorPick::selectRow(add.id.v, PetariNative::EditorPick::Click::Replace);
    }

    // Cmd+D (UX section 6): a copy of each selected object one step ahead along its gravity frame's front,
    // settled on the ground there; one op; the copies become the selection.
    void duplicate() {
        std::vector< Item > items = gather();
        if (items.empty()) return;
        const PetariEditor::ModelView* model = PetariEditor::model();
        PetariEditor::Command command{PetariEditor::Command::Kind::Apply};
        std::vector< PetariEditor::RowId > copies;
        for (const Item& start : items) {
            int zone = 0, table = 0;
            if (!model->locate(start.row, &zone, &table)) continue;
            Item copy = start;
            const G::Basis frame = G::gravityFrame(gravityUp(start.pos, start.basis.y), start.basis.z);
            copy.pos = start.pos + frame.z * 150.0f;
            settle(copy, start);
            const PetariEditor::RowTransform t = toRow(copy);
            PetariEditor::AddRow add;
            add.id = PetariEditor::newRowId();
            add.zone = zone;
            add.table = model->tablePath(zone, table);
            add.copyOf = start.row;
            static const char* const kColumns[3] = {"pos_x", "pos_y", "pos_z"};
            const float values[3] = {t.pos.x, t.pos.y, t.pos.z};
            for (int c = 0; c < 3; c++) add.cells.push_back({PetariEditor::columnHash(kColumns[c]), floatCell(values[c])});
            static const char* const kDir[3] = {"dir_x", "dir_y", "dir_z"};
            const float dir[3] = {t.dirDeg.x, t.dirDeg.y, t.dirDeg.z};
            for (int c = 0; c < 3; c++) add.cells.push_back({PetariEditor::columnHash(kDir[c]), floatCell(dir[c])});
            copies.push_back(add.id);
            command.op.adds.push_back(add);
        }
        const char* name = items.size() == 1 ? model->objectName(items[0].row) : nullptr;
        char label[96];
        if (items.size() == 1) std::snprintf(label, sizeof(label), "Duplicated %s", name != nullptr ? name : "object");
        else std::snprintf(label, sizeof(label), "Duplicated %d objects", static_cast< int >(items.size()));
        command.op.label = label;
        std::fprintf(stderr, "[editor] %s\n", label);
        PetariEditor::enqueue(std::move(command));
        for (size_t i = 0; i < copies.size(); i++) {
            PetariNative::EditorPick::selectRow(copies[i].v, i == 0 ? PetariNative::EditorPick::Click::Replace : PetariNative::EditorPick::Click::Add);
        }
    }

    void groundCommand(int what) {
        std::vector< Item > items = gather();
        if (items.empty()) return;
        for (size_t i = 0; i < items.size(); i++) {
            Item& item = items[i];
            const V3 up = gravityUp(item.pos, item.basis.y);
            if (what & static_cast< int >(UI::Ground::Drop)) {
                V3 ground;
                if (hitGround(item.pos + up * 10.0f, up * -kDropProbe, item.actor, &ground)) item.pos = ground;
            }
            if (what & static_cast< int >(UI::Ground::StandUp)) item.basis = G::alignUp(item.basis, up);
        }
        commit(items, what == static_cast< int >(UI::Ground::StandUp) ? "Stood up"
                      : what == static_cast< int >(UI::Ground::Drop)  ? "Dropped"
                                                                       : "Dropped and stood up");
    }
}  // namespace

namespace EditorGizmo {
    void update(const EditorPicking::View& view) {
        PetariNative::HostAllocationScope host;
        const UI::Take input = UI::take();
        if (input.ground != 0 && !gDrag.active) groundCommand(input.ground);
        if (PetariNative::EditorPick::takeDuplicate() && !gDrag.active) duplicate();

        const Camera cam = cameraOf(view);
        UI::View out;
        // The play marker: Mario's place the first time in this galaxy, M to move it.
        const char* stage = MR::getCurrentStageName();
        if (!gMarker.valid || stage == nullptr || std::strncmp(stage, gMarker.stage, sizeof(gMarker.stage)) != 0) {
            if (const TVec3f* mario = MR::getPlayerPos()) {
                gMarker.pos = v(*mario);
                gMarker.up = gravityUp(gMarker.pos, {0, 1, 0});
                gMarker.front = G::gravityFrame(gMarker.up, cam.forward).z;
                gMarker.valid = true;
                std::snprintf(gMarker.stage, sizeof(gMarker.stage), "%s", stage != nullptr ? stage : "");
                sendMarker();
            }
        }
        if (const std::uint64_t example = PetariNative::EditorPick::takePlace(); example != 0) {
            place(cam, view.cursorX, view.cursorY, PetariEditor::RowId{example});
        }
        if (PetariNative::EditorPick::takePlaceMarker()) {
            V3 ground;
            if (hitGround(cam.eye, cam.ray(view.cursorX, view.cursorY) * kRayLength, nullptr, &ground)) {
                gMarker.pos = ground;
                gMarker.up = gravityUp(ground, gMarker.up);
                // Facing away from the camera, flattened onto the ground.
                gMarker.front = G::gravityFrame(gMarker.up, cam.forward).z;
                sendMarker();
                std::fprintf(stderr, "[editor] play marker at (%.0f, %.0f, %.0f)\n", ground.x, ground.y, ground.z);
            }
        }
        if (gMarker.valid && cam.project(gMarker.pos, &out.markerX, &out.markerY)) {
            out.marker = true;
            out.markerRadius = 60.0f / cam.worldPerHeight(gMarker.pos);  // Mario's footprint
        }
        // Other missions' objects as ghosts (UX section 12): rows not in the mission being edited, placed
        // through their zone; the nearest few hundred in front of the camera.
        if (UI::showGhosts()) {
            if (const PetariEditor::ModelView* model = PetariEditor::model(); model != nullptr && model->scenario() > 0) {
                const std::uint32_t current = 1u << (model->scenario() - 1);
                std::vector< std::pair< float, UI::View::Ghost > > found;
                for (int zone = 0; zone < model->zoneCount(); zone++) {
                    if (!model->zonePlaced(zone)) continue;
                    const PetariEditor::Mtx34 m = model->zoneWorldMatrix(zone);
                    const G::Basis zb = zoneBasis(m);
                    for (int table = 0; table < model->tableCount(zone); table++) {
                        const char* path = model->tablePath(zone, table);
                        if (std::strstr(path, "objinfo") == nullptr || std::strstr(path, "stageobjinfo") != nullptr) continue;
                        model->forEachRow(zone, table, [&](PetariEditor::RowId row) {
                            if (model->state(row) == PetariEditor::RowState::Deleted || !model->hasTransform(row)) return;
                            if ((model->rowMissions(row) & current) != 0) return;
                            const PetariEditor::RowTransform t = model->transform(row);
                            const V3 world = apply(zb, {t.pos.x, t.pos.y, t.pos.z}) + V3{m.m[0][3], m.m[1][3], m.m[2][3]};
                            UI::View::Ghost ghost;
                            if (!cam.project(world, &ghost.x, &ghost.y) || std::fabs(ghost.x) > 1.1f || std::fabs(ghost.y) > 1.1f) return;
                            ghost.radius = 80.0f / cam.worldPerHeight(world);
                            const char* name = model->objectName(row);
                            std::snprintf(ghost.name, sizeof(ghost.name), "%s", name != nullptr ? name : "?");
                            found.push_back({G::length(world - cam.eye), ghost});
                        });
                    }
                }
                std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
                for (size_t i = 0; i < found.size() && i < 300; i++) out.ghosts.push_back(found[i].second);
            }
        }
        std::vector< Item > items = gDrag.active ? *gDrag.items : gather();
        if (items.empty()) {
            gDrag.active = false;
            UI::publish(out);
            return;
        }
        const Item& primary = items[0];
        const V3 origin = gDrag.active ? gDrag.origin : primary.pos;
        const G::Basis frame = gDrag.active ? gDrag.frame : axes(primary);
        const UI::Mode mode = UI::mode();
        Geometry geometry = build(cam, origin, frame, mode, None);
        const Handle hot = gDrag.active ? gDrag.handle : hitTest(geometry, view.cursorX, view.cursorY, view.aspect);
        geometry = build(cam, origin, frame, mode, hot);

        if (input.pressed && !gDrag.active) {
            Handle handle = hot;
            // Pressing on a selected object (no handle) slides it over the ground.
            if (handle == None && EditorPicking::hoverActor() != nullptr) {
                for (const Item& item : items) {
                    if (item.actor && item.actor == EditorPicking::hoverActor())
                        handle = Body;
                }
            }
            if (handle != None) {
                static std::vector< Item >* sStart = new std::vector< Item >();
                *sStart = items;
                gDrag = Drag{};
                gStatus[0] = '\0';
                gDrag.active = true;
                gDrag.handle = handle;
                gDrag.items = sStart;
                gDrag.origin = origin;
                gDrag.frame = frame;
                const V3 ray = cam.ray(view.cursorX, view.cursorY);
                const V3 axis[3] = {frame.x, frame.y, frame.z};
                if (handle >= AxisX && handle <= AxisZ) G::closestOnAxis(cam.eye, ray, origin, axis[handle - AxisX], &gDrag.startT);
                if (handle >= RingX && handle <= RingZ) G::rayPlane(cam.eye, ray, origin, axis[handle - RingX], &gDrag.startPoint);
                float x0 = 0, y0 = 0;
                cam.project(origin, &x0, &y0);
                gDrag.startScreen = std::sqrt((view.cursorX - x0) * (view.cursorX - x0) + (view.cursorY - y0) * (view.cursorY - y0));
            }
        }
        if (gDrag.active) {
            std::vector< Item > moved;
            applyDrag(cam, view.cursorX, view.cursorY, !input.snapSuspended, moved);
            if (!moved.empty()) {
                bool changed = false;
                for (size_t i = 0; i < moved.size(); i++) {
                    changed = changed || G::length(moved[i].pos - (*gDrag.items)[i].pos) > 0.01f ||
                              G::length(moved[i].basis.z - (*gDrag.items)[i].basis.z) > 1e-4f ||
                              G::length(moved[i].scale - (*gDrag.items)[i].scale) > 1e-4f;
                }
                gDrag.moved = gDrag.moved || changed;
                if (gDrag.moved) preview(moved);
                if (input.released) {
                    if (gDrag.moved) {
                        commit(moved, gDrag.handle >= RingX && gDrag.handle <= RingZ ? "Rotated"
                                      : UI::mode() == UI::Mode::Scale && gDrag.handle != Body ? "Scaled"
                                                                                               : "Moved");
                    }
                    gDrag.active = false;
                    gDrag.handle = None;
                }
            } else if (input.released) {
                gDrag.active = false;
                gDrag.handle = None;
            }
        }
        out.visible = mode != UI::Mode::Select;
        out.dragging = gDrag.active && gDrag.moved;
        out.lines = mode != UI::Mode::Select ? geometry.lines : std::vector< UI::Line >{};
        cam.project(origin, &out.originX, &out.originY);
        if (gDrag.active && gDrag.moved) std::snprintf(out.status, sizeof(out.status), "%s", gStatus);
        UI::publish(out);
    }

    bool dragging() {
        return gDrag.active;
    }

    void reset() {
        gDrag.active = false;
        UI::publish(UI::View{});
    }
}  // namespace EditorGizmo
#endif
