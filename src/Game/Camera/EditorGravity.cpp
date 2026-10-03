#ifdef PETARI_NATIVE
#include "Game/Camera/EditorGravity.hpp"
#include "Game/Editor/EditorScene.hpp"
#include "Game/Editor/EditorTerrain.hpp"
#include "Game/Gravity.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Util.hpp"
#include "Game/Util/BaseMatrixFollowTargetHolder.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <petari/editor/check_providers.hpp>
#include <petari/editor/debug_draw.hpp>
#include <petari/editor/runtime.hpp>
#include <petari/editor/runtime_hooks.hpp>
#include <petari/editor_pick.hpp>
#include <petari/host_allocation.hpp>
#include <set>

namespace {
    using namespace PetariEditor;
    namespace G = Gravity;
    namespace D = DebugDraw;
    Vec3 vec(const TVec3f& v) {
        return {v.x, v.y, v.z};
    }
    TVec3f tv(Vec3 v) {
        return {v.x, v.y, v.z};
    }
    Vec3 add(Vec3 a, Vec3 b) {
        return {a.x + b.x, a.y + b.y, a.z + b.z};
    }
    Vec3 sub(Vec3 a, Vec3 b) {
        return {a.x - b.x, a.y - b.y, a.z - b.z};
    }
    Vec3 mul(Vec3 a, float s) {
        return {a.x * s, a.y * s, a.z * s};
    }
    float dot(Vec3 a, Vec3 b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }
    float length(Vec3 a) {
        return std::sqrt(dot(a, a));
    }
    Vec3 unit(Vec3 a) {
        float l = length(a);
        return l > 1e-5f ? mul(a, 1 / l) : Vec3{0, 1, 0};
    }
    Vec3 axis(const TPos3f& m, int column) {
        return {m.mMtx[0][column], m.mMtx[1][column], m.mMtx[2][column]};
    }
    PlanetGravityManager* manager() {
        return MR::isExistSceneObj(SceneObj_PlanetGravityManager) ? MR::getSceneObj< PlanetGravityManager >(SceneObj_PlanetGravityManager) : nullptr;
    }
    std::string typeOf(const void* object) {
        PetariNative::HostAllocationScope host;
        if (!object)
            return {};
        static std::map< std::uintptr_t, std::string > names;
        auto vtable = *reinterpret_cast< const std::uintptr_t* >(object);
        auto it = names.find(vtable);
        if (it != names.end())
            return it->second;
        std::string name;
        PetariNative::classifyCode(vtable, &name);
        return names.emplace(vtable, std::move(name)).first->second;
    }
    bool isGlobal(const LiveActor* actor) {
        return actor && typeOf(actor).find("GlobalGravityObj") != std::string::npos;
    }
    G::Shape shapeOf(PlanetGravity* g) {
        PetariNative::HostAllocationScope host;
        std::string name = typeOf(g);
        if (name.find("DiskTorusGravity") != std::string::npos)
            return G::Shape::Torus;
        for (int i = 0; i < 8; i++) {
            auto shape = static_cast< G::Shape >(i);
            if (name.find(std::string(G::shapeName(shape)) + "Gravity") != std::string::npos)
                return shape;
        }
        return G::Shape::Unknown;
    }
    G::Field describe(PlanetGravity* g) {
        G::Field f;
        f.identity = reinterpret_cast< std::uintptr_t >(g);
        f.shape = shapeOf(g);
        f.name = std::string(G::shapeName(f.shape)) + " gravity";
        f.range = g->mRange;
        f.distant = g->mDistant;
        f.priority = g->mPriority;
        f.inverse = g->mIsInverse;
        f.active = g->mActivated && g->mValidFollower && g->mAppeared;
        switch (f.shape) {
        case G::Shape::Point: {
            auto* p = static_cast< PointGravity* >(g);
            f.center = vec(p->mTranslation);
            f.radius = std::max(60.0f, p->mDistant);
            break;
        }
        case G::Shape::Cube: {
            auto* p = static_cast< CubeGravity* >(g);
            f.center = axis(p->mPosition, 3);
            f.x = axis(p->mPosition, 0);
            f.y = axis(p->mPosition, 1);
            f.z = axis(p->mPosition, 2);
            f.radius = length(add(add(f.x, f.y), f.z));
            break;
        }
        case G::Shape::Cone: {
            auto* p = static_cast< ConeGravity* >(g);
            f.center = axis(p->mWorldMtx, 3);
            f.y = axis(p->mWorldMtx, 1);
            f.radius = p->mWorldRadius;
            f.height = length(f.y);
            f.topCut = p->mTopCutRate;
            break;
        }
        case G::Shape::Disk: {
            auto* p = static_cast< DiskGravity* >(g);
            f.center = vec(p->mWorldPosition);
            f.y = vec(p->mWorldNormal);
            f.radius = p->mWorldRadius;
            f.degrees = p->mValidDegree;
            f.x = D::point(
                Gizmo::rotate(Gizmo::normalized(D::vector(vec(p->mWorldOppositeSideVecOrtho))), D::vector(f.y), -f.degrees * 0.5f * Gizmo::kDeg));
            f.z = D::point(Gizmo::cross(D::vector(f.y), D::vector(f.x)));
            break;
        }
        case G::Shape::Torus: {
            auto* p = static_cast< DiskTorusGravity* >(g);
            f.center = vec(p->mTranslation);
            f.y = vec(p->mRotation);
            f.radius = p->mWorldRadius;
            f.innerRadius = std::max(0.0f, p->mWorldRadius - p->mDiskRadius);
            break;
        }
        case G::Shape::Parallel: {
            auto* p = static_cast< ParallelGravity* >(g);
            f.center = vec(p->mWorldPlanePosition);
            f.y = vec(p->mWorldPlaneUpVec);
            f.bound = static_cast< G::Bound >(p->mRangeType);
            if (f.bound == G::Bound::Box) {
                f.center = axis(p->mWorldMtx, 3);
                f.x = axis(p->mWorldMtx, 0);
                f.y = axis(p->mWorldMtx, 1);
                f.z = axis(p->mWorldMtx, 2);
                f.radius = std::max({length(f.x), length(f.y), length(f.z)});
            } else if (f.bound == G::Bound::Cylinder) {
                f.radius = p->mCylinderRadius;
                f.height = p->mCylinderHeight;
            } else
                f.radius = p->mRange < 0 ? 500 : p->mRange;
            break;
        }
        case G::Shape::Segment: {
            auto* p = static_cast< SegmentGravity* >(g);
            f.points = {vec(p->mWorldGravityPoints[0]), vec(p->mWorldGravityPoints[1])};
            f.center = mul(add(f.points[0], f.points[1]), 0.5f);
            f.radius = length(sub(f.points[1], f.points[0])) * 0.5f;
            break;
        }
        case G::Shape::Wire: {
            auto* p = static_cast< WireGravity* >(g);
            for (int i = 0; i < p->mPoints.size(); i++)
                f.points.push_back(vec(p->mPoints[i]));
            if (!f.points.empty()) {
                f.center = f.points.front();
                f.radius = 0;
                for (auto point : f.points)
                    f.radius = std::max(f.radius, length(sub(point, f.center)));
            }
            break;
        }
        default:
            break;
        }
        return f;
    }
    struct State {
        G::View view;
        D::Mesh mesh;
        std::uint32_t scene = ~0u;
        std::uint64_t revision = 0;
        unsigned frame = 0;
        std::string checkSignature;
        std::map< const LiveActor*, std::string > bindingConfig;
    };
    State& state() {
        PetariNative::HostAllocationScope host;
        static State* s = new State;
        return *s;
    }
    G::Probe probe(Vec3 point, bool ground) {
        G::Probe p;
        p.valid = true;
        p.position = point;
        p.onGround = ground;
        auto* mgr = manager();
        if (!mgr)
            return p;
        TVec3f direction;
        GravityInfo info;
        p.covered = mgr->calcTotalGravityVector(&direction, &info, tv(point), GRAVITY_TYPE_NORMAL, 0);
        p.direction = vec(direction);
        if (p.covered) {
            p.priority = info.mLargestPriority;
            p.winnerIdentity = reinterpret_cast< std::uintptr_t >(info.mGravityInstance);
            auto found = std::find_if(state().view.fields.begin(), state().view.fields.end(),
                                      [&](const auto& f) { return f.identity == reinterpret_cast< std::uintptr_t >(info.mGravityInstance); });
            p.name = std::string(G::shapeName(shapeOf(info.mGravityInstance))) + " gravity";
            if (state().scene == Hooks::sceneSerial() && found != state().view.fields.end()) {
                p.winner = found->row;
                p.name = found->name;
            }
            for (int i = 0; i < mgr->nativeGravityCount(); i++) {
                auto* g = mgr->nativeGravityAt(i);
                TVec3f v;
                if (g->mAppeared && g->mActivated && g->mValidFollower && (g->mGravityType & GRAVITY_TYPE_NORMAL) && g->calcGravity(&v, tv(point))) {
                    if (g->mPriority == p.priority)
                        p.blended++;
                    else if (g->mPriority < p.priority)
                        p.beaten++;
                }
            }
        }
        return p;
    }
    bool groundAt(Vec3 eye, Vec3 ray, Vec3* pos, Vec3* normal, RowId* row) {
        TVec3f hit;
        ::Triangle triangle;
        if (!MR::getFirstPolyOnLineToMap(&hit, &triangle, tv(eye), tv(mul(ray, 400000))))
            return false;
        *normal = vec(*triangle.getFaceNormal());
        *pos = add(vec(hit), mul(*normal, 4));
        *row = triangle.mSensor ? rowForActor(triangle.mSensor->mHost) : RowId{};
        return true;
    }
    void checksProvider(const ModelView&, std::vector< Check >& out) {
        if (state().scene != Hooks::sceneSerial())
            return;
        auto c = G::makeChecks(state().view);
        out.insert(out.end(), c.begin(), c.end());
    }
    // Registered once, before the first edit operation. The engine creator applies exactly the same
    // shape parameters here as during placement, retaining the already registered gravity instance.
    void refreshFollower(GlobalGravityObj* actor) {
        if (!MR::isExistSceneObj(SceneObj_BaseMatrixFollowTargetHolder))
            return;
        auto* holder = MR::getSceneObj< BaseMatrixFollowTargetHolder >(SceneObj_BaseMatrixFollowTargetHolder);
        for (int i = 0; i < holder->mFollowers.size(); i++) {
            auto* follower = holder->mFollowers[i];
            if (follower->mFollowerObj == actor && follower->isEnableFollow())
                follower->update();
        }
    }
    bool preview(LiveActor* actor, int zone, const RowTransform& t) {
        if (!isGlobal(actor))
            return false;
        auto* obj = static_cast< GlobalGravityObj* >(actor);
        const float pos[] = {t.pos.x, t.pos.y, t.pos.z}, dir[] = {t.dirDeg.x, t.dirDeg.y, t.dirDeg.z}, scale[] = {t.scale.x, t.scale.y, t.scale.z};
        EditorScene::applyTransform(actor, zone, pos, dir, scale);
        obj->mGravityCreator->settingFromSRT(actor->mPosition, actor->mRotation, actor->mScale);
        if (model()) {
            const Cell* distant = model()->cell(rowForActor(actor), columnHash("Distant"));
            if (distant) {
                float value = distant->type == CellType::Float ? distant->f : static_cast< float >(distant->i);
                if (value >= 0)
                    obj->getGravity()->mDistant = value;
            }
        }
        obj->getGravity()->updateIdentityMtx();
        refreshFollower(obj);
        return true;
    }
    std::string bindingConfig(RowId row) {
        PetariNative::HostAllocationScope host;
        if (!model())
            return {};
        int zone = 0, table = 0;
        if (!model()->locate(row, &zone, &table))
            return {};
        std::string out;
        for (const auto& column : model()->columns(zone, table)) {
            const std::string name = column.name ? column.name : "";
            bool parameter = name == "name" || name == "Range" || name == "Distant" || name == "Priority" || name == "Inverse" ||
                             name == "Gravity_id" || name == "Gravity_type" || name == "Power" || name == "CommonPath_ID" ||
                             name.rfind("pos_", 0) == 0 || name.rfind("dir_", 0) == 0 || name.rfind("scale_", 0) == 0 ||
                             name.rfind("Obj_arg", 0) == 0;
            if (parameter)
                continue;
            if (const auto* value = model()->cell(row, column.hash))
                out += std::to_string(column.hash) + ":" + std::to_string(static_cast< int >(value->type)) + ":" + std::to_string(value->i) + ":" +
                       std::to_string(value->f) + ":" + value->s + ";";
        }
        return out;
    }
    bool updateLive(RowId row, LiveActor* actor, const JMapInfoIter& iter) {
        if (!isGlobal(actor) || !model())
            return false;
        auto config = state().bindingConfig.find(actor);
        if (config == state().bindingConfig.end() || config->second != bindingConfig(row))
            return false;
        auto* obj = static_cast< GlobalGravityObj* >(actor);
        auto* g = obj->getGravity();
        const auto shape = G::shapeForPlacement(model()->objectName(row));
        if (shape != shapeOf(g))
            return false;
        if (shape == G::Shape::Parallel) {
            auto* p = static_cast< ParallelGravity* >(g);
            const std::string name = model()->objectName(row);
            auto bound = name == "GlobalPlaneGravityInBox"      ? ParallelGravity::RangeType_Box :
                         name == "GlobalPlaneGravityInCylinder" ? ParallelGravity::RangeType_Cylinder :
                                                                  ParallelGravity::RangeType_Sphere;
            if (p->mRangeType != bound)
                return false;
        }
        TVec3f pos(0, 0, 0), rot(0, 0, 0), scale(1, 1, 1);
        MR::getJMapInfoTrans(iter, &pos);
        MR::getJMapInfoRotate(iter, &rot);
        MR::getJMapInfoScale(iter, &scale);
        actor->mPosition = pos;
        actor->mRotation = rot;
        actor->mScale = scale;
        g->mRange = -1;
        g->mDistant = 0;
        g->mPriority = 0;
        g->mGravityId = -1;
        g->mGravityType = GRAVITY_TYPE_NORMAL;
        g->mGravityPower = GRAVITY_POWER_NORMAL;
        g->mIsInverse = false;
        obj->mGravityCreator->settingFromSRT(pos, rot, scale);
        s32 a = -1, b = -1, c = -1;
        MR::getJMapInfoArg0WithInit(iter, &a);
        MR::getJMapInfoArg1WithInit(iter, &b);
        MR::getJMapInfoArg2WithInit(iter, &c);
        obj->mGravityCreator->settingFromJMapArgs(a, b, c);
        if (shape == G::Shape::Wire)
            obj->mGravityCreator->settingFromJMapOtherParam(iter);
        MR::settingGravityParamFromJMap(g, iter);
        g->updateIdentityMtx();
        refreshFollower(obj);
        obj->updateSwitch();
        if (auto* mgr = manager())
            mgr->nativeResortGravities();
        return true;
    }
}  // namespace
namespace EditorGravity {
    G::Probe query(Vec3 position) {
        PetariNative::HostAllocationScope host;
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
            return {};
        auto result = probe(position, false);
        if (result.covered && !result.winner.valid())
            forEachBoundRow([&](RowId row, LiveActor* actor) {
                PlanetGravity* field = isGlobal(actor) ? static_cast< GlobalGravityObj* >(actor)->getGravity() : EditorTerrain::partGravity(actor);
                if (field && reinterpret_cast< std::uintptr_t >(field) == result.winnerIdentity)
                    result.winner = row;
            });
        return result;
    }
    G::Probe queryGround(Vec3 origin, Vec3 direction) {
        PetariNative::HostAllocationScope host;
        if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z) ||
            !std::isfinite(length(direction)) || length(direction) < 1e-5f)
            return {};
        Vec3 point, normal;
        RowId row;
        if (!groundAt(origin, unit(direction), &point, &normal, &row))
            return {};
        auto result = query(point);
        result.onGround = true;
        return result;
    }
    void update(const EditorPicking::View& camera) {
        PetariNative::HostAllocationScope host;
        static bool installed = false;
        if (!installed) {
            registerCheckProvider(checksProvider);
            Runtime::registerPreviewHandler(preview);
            Runtime::registerLiveUpdater(updateLive);
            installed = true;
        }
        auto& s = state();
        auto* mgr = manager();
        if (!mgr) {
            s.view = {};
            G::reset();
            return;
        }
        if (s.scene != Hooks::sceneSerial() || s.revision != (model() ? model()->revision() : 0)) {
            if (s.scene != Hooks::sceneSerial())
                s.bindingConfig.clear();
            s.scene = Hooks::sceneSerial();
            s.revision = model() ? model()->revision() : 0;
            s.view.deadGround.clear();
            s.frame = 0;
        }
        s.view.fields.clear();
        s.view.registered = mgr->nativeGravityCount();
        std::map< PlanetGravity*, RowId > rows;
        forEachBoundRow([&](RowId r, LiveActor* a) {
            if (isGlobal(a)) {
                rows[static_cast< GlobalGravityObj* >(a)->getGravity()] = r;
                s.bindingConfig.emplace(a, bindingConfig(r));
            } else if (auto* field = EditorTerrain::partGravity(a)) {
                rows[field] = r;
            }
        });
        for (int i = 0; i < mgr->nativeGravityCount(); i++) {
            auto* g = mgr->nativeGravityAt(i);
            if (!g->mAppeared)
                continue;
            auto f = describe(g);
            f.row = rows[g];
            f.name += " #" + std::to_string(g->mGravityId >= 0 ? g->mGravityId : i + 1);
            s.view.fields.push_back(std::move(f));
        }
        D::Camera c{{camera.eye[0], camera.eye[1], camera.eye[2]},
                    {camera.forward[0], camera.forward[1], camera.forward[2]},
                    {camera.up[0], camera.up[1], camera.up[2]},
                    camera.fovy,
                    camera.aspect};
        Vec3 ray = c.ray(camera.cursorX, camera.cursorY), point, normal;
        RowId groundRow;
        bool hit = groundAt(c.eye, ray, &point, &normal, &groundRow);
        if (G::airProbe() || !hit) {
            float depth = 2000;
            const auto selection = PetariNative::EditorPick::selection();
            if (!selection.empty()) {
                const auto& anchor = selection.front();
                const float forwardDepth = dot(sub({anchor.center[0], anchor.center[1], anchor.center[2]}, c.eye), c.forward);
                depth = std::max(100.0f, forwardDepth / std::max(0.01f, dot(ray, c.forward)));
            }
            point = add(c.eye, mul(ray, depth));
            hit = false;
        }
        s.view.probe = probe(point, hit);
        s.view.probe.valid = PetariNative::EditorPick::hoverWanted();
        s.view.probeX = camera.cursorX;
        s.view.probeY = camera.cursorY;
        // Amortised visible-ground survey. Samples use collision and the engine's actual normal-gravity
        // query, so empty sky and out-of-range bounding volumes never become dead-ground warnings.
        for (int k = 0; k < 8; k++) {
            unsigned index = (s.frame * 8 + k) % 144;
            float x = (index % 12 + 0.5f) / 6 - 1, y = (index / 12 + 0.5f) / 6 - 1;
            Vec3 p, n;
            RowId r;
            if (groundAt(c.eye, c.ray(x, y), &p, &n, &r) && !probe(p, true).covered) {
                auto found = std::find_if(s.view.deadGround.begin(), s.view.deadGround.end(),
                                          [&](const auto& d) { return d.row == r && length(sub(d.position, p)) < 120; });
                if (found == s.view.deadGround.end() && s.view.deadGround.size() < 512)
                    s.view.deadGround.push_back({r, p, n});
            }
        }
        s.view.deadGround.erase(
            std::remove_if(s.view.deadGround.begin(), s.view.deadGround.end(), [](const auto& d) { return probe(d.position, true).covered; }),
            s.view.deadGround.end());
        s.frame++;
        std::string signature = std::to_string(s.view.registered);
        std::set< RowId > deadRows;
        for (auto& d : s.view.deadGround)
            deadRows.insert(d.row);
        for (auto r : deadRows)
            signature += ":" + std::to_string(r.v);
        if (signature != s.checkSignature) {
            s.checkSignature = signature;
            Runtime::invalidateChecks();
        }
        s.mesh = {};
        if (G::visible()) {
            auto selected = PetariNative::EditorPick::selection();
            for (auto& f : s.view.fields) {
                bool isSelected = std::any_of(selected.begin(), selected.end(), [&](const auto& item) { return item.row && item.row == f.row.v; });
                auto mesh = G::geometry(f, isSelected);
                s.mesh.lines.insert(s.mesh.lines.end(), mesh.lines.begin(), mesh.lines.end());
                s.mesh.triangles.insert(s.mesh.triangles.end(), mesh.triangles.begin(), mesh.triangles.end());
            }
            double seconds = std::chrono::duration< double >(std::chrono::steady_clock::now().time_since_epoch()).count();
            // Each field has a few flow samples; the denser cursor grid below resolves overlaps.
            for (const auto& field : s.view.fields) {
                if (!field.active)
                    continue;
                float r = std::max(150.0f, field.radius * 1.2f);
                for (Vec3 offset : {Vec3{r, 0, 0}, Vec3{-r, 0, 0}, Vec3{0, r, 0}, Vec3{0, -r, 0}, Vec3{0, 0, r}, Vec3{0, 0, -r}}) {
                    Vec3 p = add(field.center, offset);
                    auto pull = probe(p, false);
                    if (!pull.covered)
                        continue;
                    float phase = static_cast< float >(std::fmod(seconds * 0.6 + field.identity % 31 / 31.0, 1.0));
                    p = add(p, mul(pull.direction, phase * std::min(r / 4, 200.0f)));
                    auto winner =
                        std::find_if(s.view.fields.begin(), s.view.fields.end(), [&](const auto& f) { return f.identity == pull.winnerIdentity; });
                    D::arrow(s.mesh, p, add(p, mul(pull.direction, std::min(r / 5, 180.0f))), c.up,
                             winner != s.view.fields.end() ? G::color(winner->shape, 220) : 0xffe0e0e0);
                }
            }
            Vec3 anchor = s.view.probe.position;
            float span = std::clamp(length(sub(anchor, c.eye)) * 0.25f, 250.0f, 1800.0f);
            for (int a = -3; a <= 3; a++)
                for (int b = -3; b <= 3; b++) {
                    Vec3 p = add(anchor, D::point(c.right() * (a * span / 3) + c.screenUp() * (b * span / 3)));
                    auto pull = probe(p, false);
                    if (!pull.covered)
                        continue;
                    Vec3 v = pull.direction;
                    float phase = std::fmod(seconds * 0.6f + (a + 3) * 0.13f + (b + 3) * 0.19f, 1.0f);
                    p = add(p, mul(v, phase * span / 3));
                    auto winner =
                        std::find_if(s.view.fields.begin(), s.view.fields.end(), [&](const auto& f) { return f.identity == pull.winnerIdentity; });
                    D::arrow(s.mesh, p, add(p, mul(v, span / 7)), c.up, winner != s.view.fields.end() ? G::color(winner->shape, 240) : 0xffe0e0e0, 2);
                }
            for (auto& d : s.view.deadGround) {
                auto n = D::vector(unit(d.normal));
                auto x = Gizmo::normalized(Gizmo::cross(n, {0, 1, 0}), {1, 0, 0});
                auto z = Gizmo::cross(n, x);
                auto a = add(d.position, D::point((x + z) * 35)), b = add(d.position, D::point((x - z) * 35)),
                     cc = add(d.position, D::point((x + z) * -35)), dd = add(d.position, D::point((z - x) * 35));
                s.mesh.triangles.push_back({a, b, cc, 0x704040ff});
                s.mesh.triangles.push_back({a, cc, dd, 0x704040ff});
                s.mesh.lines.push_back({a, cc, 0xdd5555ff, 2});
                s.mesh.lines.push_back({b, dd, 0xdd5555ff, 2});
            }
            if (s.view.probe.valid && s.view.probe.covered)
                D::arrow(s.mesh, point, add(point, mul(s.view.probe.direction, 120)), c.up, 0xfff0f0ff, 3);
        }
        G::publish(s.view);
    }
    void draw() {
        if (G::visible())
            D::draw(state().mesh, 0.04f);
    }
    bool bounds(const LiveActor* actor, float center[3], float* radius) {
        if (!isGlobal(actor))
            return false;
        auto f = describe(const_cast< GlobalGravityObj* >(static_cast< const GlobalGravityObj* >(actor))->getGravity());
        center[0] = f.center.x;
        center[1] = f.center.y;
        center[2] = f.center.z;
        *radius = std::max(100.0f, f.radius);
        return true;
    }
    RowId pick(Vec3 eye, Vec3 ray, float* distance) {
        if (!G::visible())
            return {};
        RowId found;
        float best = 1e30f;
        // Closest approach to the visible field outlines; testing lines avoids swallowing every click
        // inside a planet-sized bounding sphere.
        for (const auto& f : state().view.fields) {
            if (!f.row.valid())
                continue;
            auto mesh = G::geometry(f);
            for (auto& l : mesh.lines) {
                Vec3 v = sub(l.b, l.a), w = sub(eye, l.a);
                float vv = dot(v, v), rv = dot(ray, v), den = vv - rv * rv;
                if (vv < 1e-5f || den < 1e-5f)
                    continue;
                float t = (rv * dot(v, w) - vv * dot(ray, w)) / den;
                float u = std::clamp((dot(v, w) + t * rv) / vv, 0.0f, 1.0f);
                Vec3 nearest = add(l.a, mul(v, u));
                t = dot(sub(nearest, eye), ray);
                if (t < 1 || t >= best)
                    continue;
                float tolerance = std::clamp(t * 0.007f, 12.0f, 100.0f);
                if (length(sub(add(eye, mul(ray, t)), nearest)) < tolerance) {
                    best = t;
                    found = f.row;
                }
            }
        }
        if (found.valid() && distance)
            *distance = best;
        return found;
    }
}  // namespace EditorGravity
#endif
