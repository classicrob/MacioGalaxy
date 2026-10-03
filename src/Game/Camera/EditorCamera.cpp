#ifdef PETARI_NATIVE
// The level editor's camera over the frozen scene (Game/Camera/EditorCamera.hpp).
#include "Game/Camera/EditorCamera.hpp"
#include "Game/AudioLib/AudSystem.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/Camera/EditorAreas.hpp"
#include "Game/Camera/EditorGizmo.hpp"
#include "Game/Camera/EditorGravity.hpp"
#include "Game/Camera/EditorPaths.hpp"
#include "Game/Camera/EditorPicking.hpp"
#include "Game/System/GameSystemFunction.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include <petari/editor/edit_api.hpp>
#include <petari/editor_input.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
    // PETARI_EDITOR_PICK_TRACE=1: log what picking and zooming hit.
    bool pickTrace() {
        static const bool trace = std::getenv("PETARI_EDITOR_PICK_TRACE") != nullptr;
        return trace;
    }
    struct V {
        float x, y, z;
    };
    V add(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    V sub(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    V mul(V a, float s) { return {a.x * s, a.y * s, a.z * s}; }
    float dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    V cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
    float length(V a) { return std::sqrt(dot(a, a)); }
    V normalized(V a, V fallback) {
        const float l = length(a);
        return l > 1e-6f ? mul(a, 1.0f / l) : fallback;
    }
    // Rodrigues rotation of v about the unit axis k.
    V rotate(V v, V k, float angle) {
        const float c = std::cos(angle), s = std::sin(angle);
        return add(add(mul(v, c), mul(cross(k, v), s)), mul(k, dot(k, v) * (1.0f - c)));
    }
    TVec3f tv(V v) { return TVec3f(v.x, v.y, v.z); }
    V vt(const TVec3f& v) { return {v.x, v.y, v.z}; }

    constexpr float kDeg = 3.14159265f / 180.0f;
    constexpr float kRayLength = 400000.0f;  // a galaxy's span
    constexpr float kMinDistance = 60.0f;    // closest zoom to the pivot
    // "Up" turns to a new gravity over about 0.4 s (the game's camera does the same between fields).
    constexpr float kUpBlend = 0.12f;

    struct State {
        bool active = false;
        bool known = false;  // the editor camera has a view of its own to come back to
        TPos3f savedView;
        float savedFovy = 45.0f;
        V eye{0, 0, 0}, forward{0, 0, -1}, up{0, 1, 0};
        V upTarget{0, 1, 0};
        V pivot{0, 0, -1000};
        float fovy = 45.0f;
        char stage[48] = {};  // the galaxy the view belongs to
    } gState;

    void setView() {
        const V right = normalized(cross(gState.forward, gState.up), V{1, 0, 0});
        const V up = cross(right, gState.forward);
        const V back = mul(gState.forward, -1.0f);
        TPos3f view;
        const V axes[3] = {right, up, back};
        for (int r = 0; r < 3; r++) {
            view.mMtx[r][0] = axes[r].x;
            view.mMtx[r][1] = axes[r].y;
            view.mMtx[r][2] = axes[r].z;
            view.mMtx[r][3] = -dot(axes[r], gState.eye);
        }
        MR::setCameraViewMtx(view, false, false, tv(gState.eye));
        MR::setFovy(gState.fovy);
    }

    // Away from gravity at a point, or the current up where nothing pulls (deep space).
    V gravityUp(V at, V fallback) {
        TVec3f gravity(0.0f, 0.0f, 0.0f);
        if (MR::calcGravityVectorOrZero(nullptr, tv(at), &gravity, nullptr, 0) && length(vt(gravity)) > 1e-4f) {
            return normalized(mul(vt(gravity), -1.0f), fallback);
        }
        return fallback;
    }

    // The ray through the cursor (-1..1 across the viewport, y up) from the eye.
    V cursorRay(float x, float y) {
        const V right = normalized(cross(gState.forward, gState.up), V{1, 0, 0});
        const V up = cross(right, gState.forward);
        const float tanY = std::tan(gState.fovy * 0.5f * kDeg);
        const float tanX = tanY * MR::getAspect();
        return normalized(add(gState.forward, add(mul(right, x * tanX), mul(up, y * tanY))), gState.forward);
    }

    bool hitMap(V from, V direction, V* hit) {
        TVec3f point;
        if (MR::getFirstPolyOnLineToMap(&point, nullptr, tv(from), tv(mul(direction, kRayLength)))) {
            *hit = vt(point);
            return true;
        }
        return false;
    }

    // Rotates the eye about the pivot, and the view with it (so a pivot off the view's axis does not jump).
    void orbitAbout(V axis, float angle) {
        const V offset = rotate(sub(gState.eye, gState.pivot), axis, angle);
        const V forward = normalized(rotate(gState.forward, axis, angle), gState.forward);
        if (std::fabs(dot(forward, gState.up)) > 0.995f) {
            return;  // never past straight up or down
        }
        gState.eye = add(gState.pivot, offset);
        gState.forward = forward;
    }

    // The view has no roll by construction (setView takes "right" from forward x up); only a view
    // looking straight along the new up needs a nudge so "right" stays defined.
    void keepViewDefined() {
        if (std::fabs(dot(gState.forward, gState.up)) > 0.995f) {
            const V side = normalized(cross(gState.up, V{0.3f, 0.5f, 0.8f}), V{1, 0, 0});
            gState.forward = normalized(add(gState.forward, mul(side, 0.2f)), gState.forward);
        }
    }

    void log(const char* when) {
        std::fprintf(stderr, "[editor] %s: eye (%.1f, %.1f, %.1f) pivot (%.1f, %.1f, %.1f) up (%.3f, %.3f, %.3f)\n", when,
                     gState.eye.x, gState.eye.y, gState.eye.z, gState.pivot.x, gState.pivot.y, gState.pivot.z, gState.up.x,
                     gState.up.y, gState.up.z);
    }
}  // namespace

namespace EditorCamera {
    bool takeEnterRequest(bool canPause) {
        PetariEditorInput in;
        petari_editor_take_input(&in);
        if (in.toggle == 0 && in.enter == 0) {
            return false;
        }
        if (in.enabled && canPause) {
            return true;
        }
        petari_editor_set_active(0);  // not now (a demo, a talk, the pause menu...): input goes back to the game
        return false;
    }

    void start() {
        gState.active = true;
        gState.savedView = MR::getCameraViewMtx();
        gState.savedFovy = MR::getFovy();
        const char* stage = MR::getCurrentStageName();
        if (stage == nullptr || std::strncmp(stage, gState.stage, sizeof(gState.stage)) != 0) {
            gState.known = false;  // another galaxy: start from the game's view
            std::snprintf(gState.stage, sizeof(gState.stage), "%s", stage != nullptr ? stage : "");
        }
        if (!gState.known) {
            // The first time: exactly the game's view (entering Edit never changes what is on screen).
            const TPos3f& inv = MR::getCameraInvViewMtx();
            gState.eye = {inv.mMtx[0][3], inv.mMtx[1][3], inv.mMtx[2][3]};
            gState.up = normalized({inv.mMtx[0][1], inv.mMtx[1][1], inv.mMtx[2][1]}, V{0, 1, 0});
            gState.forward = normalized({-inv.mMtx[0][2], -inv.mMtx[1][2], -inv.mMtx[2][2]}, V{0, 0, -1});
            gState.fovy = gState.savedFovy;
            V hit;
            gState.pivot = hitMap(gState.eye, gState.forward, &hit) ? hit : add(gState.eye, mul(gState.forward, 1500.0f));
            gState.upTarget = gState.up;
            gState.known = true;
        }
        AudWrap::getSystem()->enterPauseMenu();
        GameSystemFunction::onPauseBeginAllRumble();
        petari_editor_set_active(1);
        PetariEditor::notifyEditEntered();  // Runtime loads the EditModel and binds actors on its next tick
        setView();
        log("enter");
    }

    bool update() {
        PetariEditorInput in;
        petari_editor_take_input(&in);
        if (in.leave > 0 || !in.enabled) {
            return true;  // Runtime's leaveEdit (Play), or the editor switched off
        }
        if (in.toggle > 0) {
            // Tab in Edit mode plays (UX section 3): Runtime reloads the layout if anything changed, then
            // leaves Edit (leaveEdit) and puts Mario at the play marker. Without a model, just leave.
            if (PetariEditor::model() == nullptr) return true;
            PetariEditor::Command play{PetariEditor::Command::Kind::Play};
            play.from = in.playFrom == 1 ? PetariEditor::PlayFrom::Resume
                        : in.playFrom == 2 ? PetariEditor::PlayFrom::MissionStart
                                           : PetariEditor::PlayFrom::Marker;
            PetariEditor::enqueue(std::move(play));
            std::fprintf(stderr, "[editor] play (%s)\n", in.playFrom == 1 ? "resume" : in.playFrom == 2 ? "mission start" : "marker");
        }
        // The pivot under the cursor, for an orbit that starts now.
        if (in.repivot > 0) {
            V hit;
            if (hitMap(gState.eye, cursorRay(in.cursorX, in.cursorY), &hit)) {
                gState.pivot = hit;
            }
            if (!in.worldUp) gState.upTarget = gravityUp(gState.pivot, gState.upTarget);
        }
        if (in.worldUp) gState.upTarget = V{0, 1, 0};
        // Up turns toward the gravity at the pivot.
        if (dot(gState.up, gState.upTarget) < 0.99999f) {
            gState.up = normalized(add(gState.up, mul(sub(gState.upTarget, gState.up), kUpBlend)), gState.upTarget);
            keepViewDefined();
        }
        const V right = normalized(cross(gState.forward, gState.up), V{1, 0, 0});
        const float distance = std::fmax(length(sub(gState.pivot, gState.eye)), kMinDistance);

        // Orbit about the pivot: yaw about up, pitch about the camera's right.
        if (in.orbitYaw != 0.0f) orbitAbout(gState.up, -in.orbitYaw * kDeg);
        if (in.orbitPitch != 0.0f) orbitAbout(normalized(cross(gState.forward, gState.up), right), in.orbitPitch * kDeg);

        // Fly: look in place, then move. The pivot keeps its distance in front.
        if (in.lookYaw != 0.0f || in.lookPitch != 0.0f) {
            gState.forward = normalized(rotate(gState.forward, gState.up, -in.lookYaw * kDeg), gState.forward);
            const V r = normalized(cross(gState.forward, gState.up), right);
            const V pitched = normalized(rotate(gState.forward, r, in.lookPitch * kDeg), gState.forward);
            if (std::fabs(dot(pitched, gState.up)) < 0.995f) gState.forward = pitched;
            gState.pivot = add(gState.eye, mul(gState.forward, distance));
        }
        if (in.moveRight != 0.0f || in.moveForward != 0.0f || in.moveUp != 0.0f) {
            // Faster far from things, slower close up.
            const float step = std::fmin(std::fmax(distance * 0.02f, 4.0f), 600.0f) * (in.speed > 0.0f ? in.speed : 1.0f);
            const V r = normalized(cross(gState.forward, gState.up), right);
            const V delta = add(add(mul(r, in.moveRight * step), mul(gState.forward, in.moveForward * step)), mul(gState.up, in.moveUp * step));
            gState.eye = add(gState.eye, delta);
            gState.pivot = add(gState.pivot, delta);
        }

        // Pan: the view slides; the point under the cursor stays under it.
        if (in.panRight != 0.0f || in.panUp != 0.0f) {
            const V r = normalized(cross(gState.forward, gState.up), right);
            const V u = cross(r, gState.forward);
            const float height = 2.0f * distance * std::tan(gState.fovy * 0.5f * kDeg);
            const V delta = add(mul(r, -in.panRight * height), mul(u, -in.panUp * height));
            gState.eye = add(gState.eye, delta);
            gState.pivot = add(gState.pivot, delta);
        }

        // Zoom toward what is under the cursor (it becomes the pivot), slowing near it; never through it.
        if (in.dolly != 0.0f) {
            V target;
            const bool hit = hitMap(gState.eye, cursorRay(in.cursorX, in.cursorY), &target);
            if (!hit) target = gState.pivot;
            if (pickTrace()) {
                std::fprintf(stderr, "[editor] dolly: hit %d target (%.0f %.0f %.0f) eye (%.0f %.0f %.0f)\n", hit ? 1 : 0, target.x, target.y,
                             target.z, gState.eye.x, gState.eye.y, gState.eye.z);
            }
            const V toTarget = sub(target, gState.eye);
            const float d = length(toTarget);
            float next = d * std::pow(0.85f, in.dolly);
            next = std::fmax(next, kMinDistance);
            if (d > 1e-3f) {
                gState.eye = sub(target, mul(toTarget, next / d));
                gState.pivot = target;
            }
        }

        // Frame: the selection, or Mario while nothing is selected. F again from the same place: closer.
        if (in.frame > 0) {
            float center[3], radius = 0.0f;
            if (EditorPicking::selectionBounds(center, &radius)) {
                gState.pivot = {center[0], center[1], center[2]};
                if (!in.worldUp) gState.upTarget = gravityUp(gState.pivot, gState.upTarget);
                const float fit = std::fmax(radius, 50.0f) / std::tan(gState.fovy * 0.5f * kDeg) * 1.3f;
                gState.eye = sub(gState.pivot, mul(gState.forward, fit));
            } else if (const TVec3f* mario = MR::getPlayerPos()) {
                gState.pivot = vt(*mario);
                if (!in.worldUp) gState.upTarget = gravityUp(gState.pivot, gState.upTarget);
                gState.eye = sub(gState.pivot, mul(gState.forward, 900.0f));
            }
        }
        setView();

        // What is under the cursor, and clicks (Game/Camera/EditorPicking.hpp).
        EditorPicking::View view;
        const V r = normalized(cross(gState.forward, gState.up), V{1, 0, 0});
        const V u = cross(r, gState.forward);
        view.eye[0] = gState.eye.x, view.eye[1] = gState.eye.y, view.eye[2] = gState.eye.z;
        view.forward[0] = gState.forward.x, view.forward[1] = gState.forward.y, view.forward[2] = gState.forward.z;
        view.up[0] = u.x, view.up[1] = u.y, view.up[2] = u.z;
        view.fovy = gState.fovy;
        view.aspect = MR::getAspect();
        view.cursorX = in.cursorX;
        view.cursorY = in.cursorY;
        EditorAreas::update(view);  // M3 areas: update outline hits before picking.
        EditorPicking::update(view);
        EditorGravity::update(view);  // M3 gravity: live field snapshots and probe.
        EditorPaths::update(view);  // M3 paths: publish curves and handle edits.
        EditorGizmo::update(view);
        return false;
    }

    void end() {
        const TPos3f& inv = MR::getCameraInvViewMtx();
        const TVec3f eye(inv.mMtx[0][3], inv.mMtx[1][3], inv.mMtx[2][3]);
        MR::setCameraViewMtx(gState.savedView, false, false, eye);
        MR::setFovy(gState.savedFovy);
        GameSystemFunction::onPauseEndAllRumble();
        AudWrap::getSystem()->exitPauseMenu();
        gState.active = false;
        EditorGizmo::reset();
        EditorPicking::reset();
        petari_editor_set_active(0);
        PetariEditor::notifyEditLeft();
        log("leave");
    }

    bool isActive() {
        return gState.active;
    }

    void abandon() {
        if (!gState.active) return;
        gState.active = false;
        EditorGizmo::reset();
        EditorPicking::reset();  // the old scene's actors are gone: drop them before anything looks
        GameSystemFunction::onPauseEndAllRumble();
        AudWrap::getSystem()->exitPauseMenu();
        petari_editor_set_active(0);
        log("abandon (the scene was rebuilt)");
    }
}  // namespace EditorCamera
#endif
