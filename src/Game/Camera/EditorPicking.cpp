#ifdef PETARI_NATIVE
// The level editor's picking (Game/Camera/EditorPicking.hpp).
#include "Game/Camera/EditorPicking.hpp"
#include "Game/Camera/EditorGravity.hpp"
#include "Game/LiveActor/ClippingActorHolder.hpp"
#include "Game/LiveActor/ClippingActorInfo.hpp"
#include "Game/LiveActor/ClippingDirector.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include <petari/editor/debug_draw.hpp>
#include <petari/editor/edit_api.hpp>
#include <petari/editor/paths.hpp>
#include <petari/editor_pick.hpp>
#include <petari/host_allocation.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
    // PETARI_EDITOR_PICK_TRACE=1: log what picking and zooming hit.
    bool pickTrace() {
        static const bool trace = std::getenv("PETARI_EDITOR_PICK_TRACE") != nullptr;
        return trace;
    }
    namespace Pick = PetariNative::EditorPick;

    struct V {
        float x, y, z;
    };
    V sub(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    V add(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    V mul(V a, float s) { return {a.x * s, a.y * s, a.z * s}; }
    float dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    V cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
    V normalized(V a) {
        const float l = std::sqrt(dot(a, a));
        return l > 1e-6f ? mul(a, 1.0f / l) : a;
    }
    V of(const float v[3]) { return {v[0], v[1], v[2]}; }

    constexpr float kDeg = 3.14159265f / 180.0f;
    constexpr float kRayLength = 400000.0f;

    LiveActor* gHover = nullptr;

    // The selection, by actor (rows are the UI's; an actor the EditModel does not know is still selectable).
    std::vector< LiveActor* >* gSelected = nullptr;

    std::vector< LiveActor* >& selected() {
        if (gSelected == nullptr) {
            PetariNative::HostAllocationScope host;
            gSelected = new std::vector< LiveActor* >();
        }
        return *gSelected;
    }

    // The selection's rows, parallel to selected() (an invalid id for actors without a row).
    std::vector< PetariEditor::RowId >* gSelectedRows = nullptr;
    std::vector< PetariEditor::RowId >& selectedRows() {
        if (gSelectedRows == nullptr) {
            PetariNative::HostAllocationScope host;
            gSelectedRows = new std::vector< PetariEditor::RowId >();
        }
        return *gSelectedRows;
    }
    // Keeps the rows in step after the selection changed by actor.
    void syncRows(const std::vector< LiveActor* >& selection) {
        std::vector< PetariEditor::RowId >& rows = selectedRows();
        rows.clear();
        for (LiveActor* actor : selection) rows.push_back(PetariEditor::rowForActor(actor));
    }

    struct Hit {
        LiveActor* actor = nullptr;
        V center{0, 0, 0};
        float radius = 0.0f;
        PetariEditor::RowId row;
    };

    ClippingActorInfoList* allActors() {
        ClippingDirector* director = MR::getClippingDirector();
        if (director == nullptr || director->mActorHolder == nullptr) return nullptr;
        return director->mActorHolder->_18;  // every registered actor
    }

    bool pickable(const LiveActor* actor) {
        return actor != nullptr && !MR::isDead(actor) && &actor->mPosition != MR::getPlayerPos();
    }

    Hit describe(LiveActor* actor);
    Hit describeRow(PetariEditor::RowId row);

    // What the ray meets: the smallest object whose sphere it crosses in front of the ground (or around the
    // point where it meets the ground); else what owns the ground there (a planet, through its collision).
    Hit pick(V eye, V ray) {
        Hit best;
        ClippingActorInfoList* list = allActors();
        if (list == nullptr) return best;
        TVec3f ground;
        Triangle triangle;
        const bool hitGround = MR::getFirstPolyOnLineToMap(&ground, &triangle, TVec3f(eye.x, eye.y, eye.z),
                                                          TVec3f(ray.x * kRayLength, ray.y * kRayLength, ray.z * kRayLength));
        const V groundPoint{ground.x, ground.y, ground.z};
        const float groundT = hitGround ? dot(sub(groundPoint, eye), ray) : 1e30f;
        float gravityT = 0;
        const auto gravityRow = EditorGravity::pick({eye.x, eye.y, eye.z}, {ray.x, ray.y, ray.z}, &gravityT);  // M3 gravity: clickable outlines.
        if (gravityRow.valid() && gravityT <= groundT + 50.0f) {
            if (auto* actor = PetariEditor::actorForRow(gravityRow)) return describe(actor);
            return describeRow(gravityRow);
        }
        Hit around;  // the smallest sphere around the ground point
        for (int i = 0; i < list->_4; i++) {
            const ClippingActorInfo* info = list->mClippingActorList[i];
            if (info == nullptr || !pickable(info->mActor)) continue;
            const V center = info->_4 != nullptr ? V{info->_4->x, info->_4->y, info->_4->z}
                                                 : V{info->mActor->mPosition.x, info->mActor->mPosition.y, info->mActor->mPosition.z};
            const float radius = info->_8;
            if (!(radius > 0.0f)) continue;
            if (hitGround) {
                const V d = sub(groundPoint, center);
                if (dot(d, d) <= radius * radius && (around.actor == nullptr || radius < around.radius)) {
                    around = {info->mActor, center, radius};
                }
            }
            const V toCenter = sub(center, eye);
            const float t = dot(toCenter, ray);
            const float closest2 = dot(toCenter, toCenter) - t * t;
            if (t <= 0.0f || closest2 > radius * radius) continue;
            if (t - radius > groundT) continue;  // behind the ground
            // Objects standing on the ground count; planets (spheres far bigger than the distance to
            // their surface) are found through the ground point instead.
            if (radius > t * 0.5f) continue;
            if (best.actor == nullptr || radius < best.radius) best = {info->mActor, center, radius};
        }
        if (pickTrace()) {
            std::fprintf(stderr, "[editor] pick: %d actors, ground %d at t=%.0f, best r=%.0f, around r=%.0f, eye (%.0f %.0f %.0f) ray (%.3f %.3f %.3f)\n",
                         list->_4, hitGround ? 1 : 0, groundT, best.actor != nullptr ? best.radius : -1.0f,
                         around.actor != nullptr ? around.radius : -1.0f, eye.x, eye.y, eye.z, ray.x, ray.y, ray.z);
        }
        const bool trace = pickTrace();
        if (best.actor != nullptr) {
            if (trace) std::fprintf(stderr, "[editor] pick: object on the ray, r=%.0f at (%.0f %.0f %.0f)\n", best.radius, best.center.x, best.center.y, best.center.z);
            return best;
        }
        if (hitGround && triangle.mSensor != nullptr && pickable(triangle.mSensor->mHost)) {
            const Hit host = describe(triangle.mSensor->mHost);
            if (trace) std::fprintf(stderr, "[editor] pick: ground (%.0f %.0f %.0f) owned by r=%.0f at (%.0f %.0f %.0f) pos (%.0f %.0f %.0f)\n", ground.x, ground.y, ground.z,
                                    host.radius, host.center.x, host.center.y, host.center.z, host.actor->mPosition.x, host.actor->mPosition.y, host.actor->mPosition.z);
            return host;
        }
        return around;
    }

    std::string className(const LiveActor* actor) {
        // No RTTI in the game: the vtable's symbol ("vtable for Kuribo").
        std::string name;
        PetariNative::classifyCode(*reinterpret_cast< const std::uintptr_t* >(actor), &name);
        for (const char* prefix : {"vtable for ", "_ZTV"}) {
            if (name.rfind(prefix, 0) == 0) name.erase(0, std::strlen(prefix));
        }
        return name.empty() ? "Object" : name;
    }

    Pick::Item item(LiveActor* actor, V center, float radius, const EditorPicking::View& view, PetariEditor::RowId row = {}) {
        Pick::Item out;
        out.actor = reinterpret_cast< std::uintptr_t >(actor);
        if (!row.valid())
            row = PetariEditor::rowForActor(actor);
        out.row = row.v;
        const char* name = nullptr;
        if (row.valid() && PetariEditor::model() != nullptr) name = PetariEditor::model()->objectName(row);
        const std::string fallback = name == nullptr ? (actor ? className(actor) : "Path point") : std::string();
        std::snprintf(out.name, sizeof(out.name), "%s", name != nullptr ? name : fallback.c_str());
        out.center[0] = center.x;
        out.center[1] = center.y;
        out.center[2] = center.z;
        out.radius = radius;
        // Project with the editor camera.
        const V forward = of(view.forward);
        const V right = normalized(cross(forward, of(view.up)));
        const V up = cross(right, forward);
        const V p = sub(center, of(view.eye));
        const float z = dot(p, forward);
        const float tanY = std::tan(view.fovy * 0.5f * kDeg), tanX = tanY * view.aspect;
        if (z > 1.0f) {
            out.screenX = dot(p, right) / (z * tanX);
            out.screenY = dot(p, up) / (z * tanY);
            out.screenRadius = radius / (z * tanY) * 0.5f;
            out.onScreen = std::fabs(out.screenX) < 1.0f + out.screenRadius * 2.0f && std::fabs(out.screenY) < 1.0f + out.screenRadius * 2.0f;
        }
        return out;
    }

    // An actor's bounds: its clipping sphere, else its collision's (planets have no clipping sphere; their
    // collision knows its farthest vertex), else a small sphere at its position.
    Hit describe(LiveActor* actor) {
        Hit hit{actor, {actor->mPosition.x, actor->mPosition.y, actor->mPosition.z}, 100.0f};
        float gravityCenter[3], gravityRadius;
        if (EditorGravity::bounds(actor, gravityCenter, &gravityRadius))  // M3 gravity: model-less bounds.
            return {actor, {gravityCenter[0], gravityCenter[1], gravityCenter[2]}, gravityRadius};
        if (ClippingActorInfoList* list = allActors()) {
            if (const ClippingActorInfo* info = list->findOrNone(actor)) {
                if (info->_4 != nullptr) hit.center = {info->_4->x, info->_4->y, info->_4->z};
                hit.radius = info->_8;
                return hit;
            }
        }
        if (const CollisionParts* parts = actor->mCollisionParts; parts != nullptr && parts->_D8 > 0.0f) {
            hit.center = {parts->mBaseMatrix.mMtx[0][3], parts->mBaseMatrix.mMtx[1][3], parts->mBaseMatrix.mMtx[2][3]};
            hit.radius = parts->_D8;
        }
        return hit;
    }
    // M3: actorless table rows (paths, points, fields) remain first-class selections.
    bool selectableRow(PetariEditor::RowId row) {
        const auto* m = PetariEditor::model();
        return m && row.valid() && m->locate(row, nullptr, nullptr) && m->state(row) != PetariEditor::RowState::Deleted;
    }
    Hit describeRow(PetariEditor::RowId row) {
        namespace D = PetariEditor::DebugDraw;
        const auto* m = PetariEditor::model();
        Hit hit;
        hit.row = row;
        hit.radius = 35;
        float gravityCenter[3], gravityRadius;
        if (EditorGravity::bounds(row, gravityCenter, &gravityRadius))
            return {nullptr, {gravityCenter[0], gravityCenter[1], gravityCenter[2]}, gravityRadius, row};
        int zone = 0;
        if (!m || !m->locate(row, &zone, nullptr))
            return hit;
        const auto matrix = m->zoneWorldMatrix(zone);
        auto center = D::transform(matrix, m->transform(row).pos);
        if (const auto* x = m->cell(row, PetariEditor::columnHash("pnt0_x"))) {
            const auto* y = m->cell(row, PetariEditor::columnHash("pnt0_y"));
            const auto* z = m->cell(row, PetariEditor::columnHash("pnt0_z"));
            center = D::transform(matrix, {x->f, y ? y->f : 0, z ? z->f : 0});
        } else {
            for (const auto& rail : PetariEditor::Paths::collect(*m))
                if (rail.row == row && !rail.points.empty()) {
                    auto lo = rail.points.front().position, hi = lo;
                    for (const auto& p : rail.points) {
                        lo = {std::min(lo.x, p.position.x), std::min(lo.y, p.position.y), std::min(lo.z, p.position.z)};
                        hi = {std::max(hi.x, p.position.x), std::max(hi.y, p.position.y), std::max(hi.z, p.position.z)};
                    }
                    center = D::transform(matrix, {(lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2});
                    hit.radius = std::max(35.0f, PetariEditor::Gizmo::length(D::vector(hi) - D::vector(lo)) * 0.5f);
                }
        }
        hit.center = {center.x, center.y, center.z};
        return hit;
    }

}  // namespace

namespace EditorPicking {
    void update(const View& view) {
        PetariNative::HostAllocationScope host;
        std::vector< LiveActor* >& selection = selected();
        // A cleared selection (Esc, or a rebuilt scene whose actors are gone) is dropped before any
        // stored actor is looked at.
        if (Pick::takeClear()) {
            selection.clear();
            selectedRows().clear();
        }
        // Back in Edit (after a play-test or a reload): the selection comes back by its rows.
        if (selection.empty() && !selectedRows().empty()) {
            std::vector< PetariEditor::RowId > kept = selectedRows();
            selectedRows().clear();
            for (PetariEditor::RowId row : kept) {
                LiveActor* actor = PetariEditor::actorForRow(row);
                if (pickable(actor) || selectableRow(row)) {
                    selection.push_back(pickable(actor) ? actor : nullptr);
                    selectedRows().push_back(row);
                }
            }
        }
        // A selected actor that died was usually re-created from its edited row (Runtime's SceneSync):
        // the selection follows the row to the new actor. Otherwise (deleted) it leaves the selection.
        std::vector< PetariEditor::RowId >& rows = selectedRows();
        rows.resize(selection.size());
        for (size_t i = 0; i < selection.size();) {
            if (!pickable(selection[i]) && rows[i].valid()) {
                LiveActor* again = PetariEditor::actorForRow(rows[i]);
                if (pickable(again)) selection[i] = again;
            }
            if (pickable(selection[i]) || selectableRow(rows[i])) {
                if (!pickable(selection[i]))
                    selection[i] = nullptr;
                i++;
            } else {
                selection.erase(selection.begin() + static_cast< long >(i));
                rows.erase(rows.begin() + static_cast< long >(i));
            }
        }

        const V forward = of(view.forward);
        const V right = normalized(cross(forward, of(view.up)));
        const V up = cross(right, forward);
        const float tanY = std::tan(view.fovy * 0.5f * kDeg), tanX = tanY * view.aspect;
        const V ray = normalized(add(forward, add(mul(right, view.cursorX * tanX), mul(up, view.cursorY * tanY))));

        // Select by name (search, test drivers): the first pickable object whose row or class name matches.
        if (char name[64]; Pick::takeSelectByName(name, sizeof(name))) {
            // Rows first (every placed object Runtime bound), then actors by class.
            LiveActor* found = nullptr;
            if (const PetariEditor::ModelView* model = PetariEditor::model()) {
                PetariEditor::forEachBoundRow([&](PetariEditor::RowId row, LiveActor* actor) {
                    const char* rowName = model->objectName(row);
                    if (found == nullptr && pickable(actor) && rowName != nullptr && std::strcmp(rowName, name) == 0) found = actor;
                });
            }
            if (found != nullptr) {
                selection.clear();
                selection.push_back(found);
            } else if (ClippingActorInfoList* list = allActors()) {
                for (int i = 0; i < list->_4; i++) {
                    LiveActor* actor = list->mClippingActorList[i] != nullptr ? list->mClippingActorList[i]->mActor : nullptr;
                    if (!pickable(actor)) continue;
                    const PetariEditor::RowId row = PetariEditor::rowForActor(actor);
                    const char* rowName = row.valid() && PetariEditor::model() != nullptr ? PetariEditor::model()->objectName(row) : nullptr;
                    if ((rowName != nullptr && std::strcmp(rowName, name) == 0) || className(actor) == name) {
                        selection.clear();
                        selection.push_back(actor);
                        break;
                    }
                }
            }
            syncRows(selection);
            std::fprintf(stderr, "[editor] select %s: %d object(s)\n", name, static_cast< int >(selection.size()));
        }

        // Select a row's object (the Changes and Outliner lists).
        // A new row's actor appears when Runtime applies the op (a frame or two later): keep asking, in order.
        static std::vector< Pick::RowRequest >* sPending = new std::vector< Pick::RowRequest >();
        static int sPendingFrames = 0;
        for (const Pick::RowRequest& request : Pick::takeSelectRows()) {
            sPending->push_back(request);
            sPendingFrames = 60;
        }
        while (!sPending->empty()) {
            const Pick::RowRequest request = sPending->front();
            LiveActor* actor = PetariEditor::actorForRow(PetariEditor::RowId{request.row});
            if (!pickable(actor) && !selectableRow(PetariEditor::RowId{request.row})) {
                if (--sPendingFrames <= 0) sPending->clear();  // never appeared (a refused op)
                break;
            }
            if (request.mode == Pick::Click::Replace) {
                selection.clear();
                rows.clear();
            }
            const auto at = std::find(rows.begin(), rows.end(), PetariEditor::RowId{request.row});
            if (at == rows.end()) {
                selection.push_back(pickable(actor) ? actor : nullptr);
                rows.push_back({request.row});
            } else if (request.mode == Pick::Click::Toggle) {
                selection.erase(selection.begin() + (at - rows.begin()));
                rows.erase(at);
            }
            sPending->erase(sPending->begin());
        }

        const Pick::Click click = Pick::takeClick();
        const bool wantHover = Pick::hoverWanted();
        Hit under;
        if (wantHover || click != Pick::Click::None) under = pick(of(view.eye), ray);
        gHover = wantHover ? under.actor : nullptr;

        if (click != Pick::Click::None) {
            const auto hitRow = under.row.valid() ? under.row : PetariEditor::rowForActor(under.actor);
            std::size_t index = hitRow.valid() ? std::find(rows.begin(), rows.end(), hitRow) - rows.begin()
                                              : std::find(selection.begin(), selection.end(), under.actor) - selection.begin();
            const bool hit = under.actor != nullptr || selectableRow(hitRow);
            const bool already = index < selection.size();
            if (click == Pick::Click::Replace) {
                selection.clear();
                rows.clear();
                if (hit) {
                    selection.push_back(under.actor);
                    rows.push_back(hitRow);
                }
            } else if (hit) {
                if (click == Pick::Click::Toggle && already) {
                    rows.erase(rows.begin() + index);
                    selection.erase(selection.begin() + index);
                } else if (!already) {
                    selection.push_back(under.actor);
                    rows.push_back(hitRow);
                }
            }
            std::fprintf(stderr, "[editor] select: %d object(s)%s\n", static_cast< int >(selection.size()),
                         hit ? "" : " (nothing under the cursor)");
        }

        std::vector< Pick::Item > published;
        for (std::size_t i = 0; i < selection.size(); ++i) {
            LiveActor* actor = selection[i];
            const Hit h = actor ? describe(actor) : describeRow(rows[i]);
            published.push_back(item(actor, h.center, h.radius, view, rows[i]));
        }
        if (wantHover && (under.actor != nullptr || selectableRow(under.row))) {
            const Pick::Item hover = item(under.actor, under.center, under.radius, view, under.row);
            Pick::publish(&hover, published);
        } else {
            Pick::publish(nullptr, published);
        }
    }

    bool selectionBounds(float center[3], float* radius) {
        if (gSelected == nullptr || gSelected->empty()) return false;
        V lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        for (std::size_t i = 0; i < gSelected->size(); ++i) {
            LiveActor* actor = (*gSelected)[i];
            if (!pickable(actor) && (i >= selectedRows().size() || !selectableRow(selectedRows()[i])))
                continue;
            const Hit h = pickable(actor) ? describe(actor) : describeRow(selectedRows()[i]);
            lo = {std::fmin(lo.x, h.center.x - h.radius), std::fmin(lo.y, h.center.y - h.radius), std::fmin(lo.z, h.center.z - h.radius)};
            hi = {std::fmax(hi.x, h.center.x + h.radius), std::fmax(hi.y, h.center.y + h.radius), std::fmax(hi.z, h.center.z + h.radius)};
        }
        if (lo.x > hi.x) return false;
        center[0] = (lo.x + hi.x) * 0.5f;
        center[1] = (lo.y + hi.y) * 0.5f;
        center[2] = (lo.z + hi.z) * 0.5f;
        const V half = mul(sub(hi, lo), 0.5f);
        *radius = std::sqrt(dot(half, half));
        return true;
    }

    void bounds(LiveActor* actor, float center[3], float* radius) {
        const Hit h = describe(actor);
        center[0] = h.center.x;
        center[1] = h.center.y;
        center[2] = h.center.z;
        *radius = h.radius;
    }

    LiveActor* hoverActor() {
        return gHover != nullptr && pickable(gHover) ? gHover : nullptr;
    }

    int selectedCount() {
        return gSelected != nullptr ? static_cast< int >(gSelected->size()) : 0;
    }

    LiveActor* selectedAt(int index) {
        return pickable((*gSelected)[index]) ? (*gSelected)[index] : nullptr;
    }

    void reset() {
        // Leaving Edit (or a rebuilt scene): the actors go; the rows stay, so the selection comes back
        // when Edit is entered again (UX section 3: return with the edit state intact).
        gHover = nullptr;
        if (gSelected != nullptr) gSelected->clear();
        Pick::publish(nullptr, {});
    }
}  // namespace EditorPicking
#endif
