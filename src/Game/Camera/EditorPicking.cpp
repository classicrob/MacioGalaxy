#ifdef PETARI_NATIVE
// The level editor's picking (Game/Camera/EditorPicking.hpp).
#include "Game/Camera/EditorPicking.hpp"
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
#include <petari/editor/edit_api.hpp>
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

    struct Hit {
        LiveActor* actor = nullptr;
        V center{0, 0, 0};
        float radius = 0.0f;
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

    Pick::Item item(LiveActor* actor, V center, float radius, const EditorPicking::View& view) {
        Pick::Item out;
        out.actor = reinterpret_cast< std::uintptr_t >(actor);
        const PetariEditor::RowId row = PetariEditor::rowForActor(actor);
        out.row = row.v;
        const char* name = nullptr;
        if (row.valid() && PetariEditor::model() != nullptr) name = PetariEditor::model()->objectName(row);
        const std::string fallback = name == nullptr ? className(actor) : std::string();
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
}  // namespace

namespace EditorPicking {
    void update(const View& view) {
        PetariNative::HostAllocationScope host;
        std::vector< LiveActor* >& selection = selected();
        // A cleared selection (Esc, or a rebuilt scene whose actors are gone) is dropped before any
        // stored actor is looked at.
        if (Pick::takeClear()) selection.clear();
        // Selected actors that died (a re-create, a delete) leave the selection.
        for (size_t i = 0; i < selection.size();) {
            if (pickable(selection[i])) {
                i++;
            } else {
                selection.erase(selection.begin() + static_cast< long >(i));
            }
        }

        const V forward = of(view.forward);
        const V right = normalized(cross(forward, of(view.up)));
        const V up = cross(right, forward);
        const float tanY = std::tan(view.fovy * 0.5f * kDeg), tanX = tanY * view.aspect;
        const V ray = normalized(add(forward, add(mul(right, view.cursorX * tanX), mul(up, view.cursorY * tanY))));

        // Select by name (search, test drivers): the first pickable object whose row or class name matches.
        if (char name[64]; Pick::takeSelectByName(name, sizeof(name))) {
            if (ClippingActorInfoList* list = allActors()) {
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
            std::fprintf(stderr, "[editor] select %s: %d object(s)\n", name, static_cast< int >(selection.size()));
        }

        const Pick::Click click = Pick::takeClick();
        const bool wantHover = Pick::hoverWanted();
        Hit under;
        if (wantHover || click != Pick::Click::None) under = pick(of(view.eye), ray);
        gHover = wantHover ? under.actor : nullptr;

        if (click != Pick::Click::None) {
            auto at = std::find(selection.begin(), selection.end(), under.actor);
            const bool already = at != selection.end();
            if (click == Pick::Click::Replace) {
                selection.clear();
                if (under.actor != nullptr) selection.push_back(under.actor);
            } else if (under.actor != nullptr) {
                if (click == Pick::Click::Toggle && already) {
                    selection.erase(at);
                } else if (!already) {
                    selection.push_back(under.actor);
                }
            }
            std::fprintf(stderr, "[editor] select: %d object(s)%s\n", static_cast< int >(selection.size()),
                         under.actor != nullptr ? "" : " (nothing under the cursor)");
        }

        std::vector< Pick::Item > published;
        for (LiveActor* actor : selection) {
            const Hit h = describe(actor);
            published.push_back(item(actor, h.center, h.radius, view));
        }
        if (wantHover && under.actor != nullptr) {
            const Pick::Item hover = item(under.actor, under.center, under.radius, view);
            Pick::publish(&hover, published);
        } else {
            Pick::publish(nullptr, published);
        }
    }

    bool selectionBounds(float center[3], float* radius) {
        if (gSelected == nullptr || gSelected->empty()) return false;
        V lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        for (LiveActor* actor : *gSelected) {
            if (!pickable(actor)) continue;
            const Hit h = describe(actor);
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
        gHover = nullptr;
        if (gSelected != nullptr) gSelected->clear();
        Pick::publish(nullptr, {});
    }
}  // namespace EditorPicking
#endif
