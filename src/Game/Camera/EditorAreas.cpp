#ifdef PETARI_NATIVE
#include "Game/Camera/EditorAreas.hpp"
#include "Game/AreaObj/AreaForm.hpp"
#include "Game/AreaObj/AreaObj.hpp"
#include "Game/AreaObj/AreaObjContainer.hpp"
#include "Game/AreaObj/WaterArea.hpp"
#include "Game/Editor/EditorScene.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/JMapUtil.hpp"
#include <algorithm>
#include <cstring>
#include <map>
#include <petari/editor/runtime.hpp>
#include <petari/editor/runtime_hooks.hpp>
#include <petari/editor_pick.hpp>
#include <petari/host_allocation.hpp>

namespace EditorAreas {
    namespace {
        using namespace PetariEditor;
        namespace A = Areas;
        namespace D = DebugDraw;
        using namespace Gizmo;
        struct Bound {
            AreaObj* area = nullptr;
            std::map< std::uint32_t, Cell > cells;
            bool valid = true, enabled = true, removed = false;
        };
        std::map< RowId, Bound > bindings;
        std::map< RowId, std::vector< D::Line > > outlines;
        D::Mesh mesh;
        std::uint32_t scene = 0;
        const ModelView* boundModel = nullptr;
        void snapshot(Bound& bound, RowId row) {
            PetariNative::HostAllocationScope host;
            const auto* m = model();
            int z, t;
            if (!m || !m->locate(row, &z, &t))
                return;
            bound.cells.clear();
            for (const auto& c : m->columns(z, t))
                if (const auto* value = m->cell(row, c.hash))
                    bound.cells[c.hash] = *value;
        }
        void bind() {
            PetariNative::HostAllocationScope host;
            if (scene == Hooks::sceneSerial() && boundModel == model() && !bindings.empty())
                return;
            scene = Hooks::sceneSerial();
            boundModel = model();
            bindings.clear();
            outlines.clear();
            mesh = {};
            A::clearPreviews();
            if (!model())
                return;
            for (const auto& placed : Hooks::areas()) {
                const auto row = Runtime::rowForPlacement(placed.table, placed.row);
                if (!row.valid())
                    continue;
                auto* area = static_cast< AreaObj* >(placed.actor);
                Bound bound;
                bound.area = area;
                bound.valid = area->mIsValid;
                bound.enabled = area->_15;
                snapshot(bound, row);
                bindings[row] = std::move(bound);
            }
        }
        bool same(const Cell& a, const Cell& b) {
            return a.type == b.type && a.i == b.i && a.f == b.f && a.s == b.s;
        }
        bool liveBounds(const A::Definition& d) {
            return std::strcmp(d.manager, "DeathArea") == 0 || std::strcmp(d.manager, "Water") == 0 || std::strcmp(d.manager, "MessageArea") == 0 ||
                   std::strcmp(d.name, "SwitchCube") == 0 || std::strcmp(d.name, "SwitchSphere") == 0 || std::strcmp(d.name, "SwitchCylinder") == 0;
        }
        bool runtimeArg(const A::Definition& d, std::uint32_t hash) {
            if (std::strcmp(d.manager, "MessageArea") == 0)
                return hash == columnHash("Obj_arg0");
            if (std::strcmp(d.manager, "DeathArea") == 0)
                return hash == columnHash("Obj_arg0");
            if (std::strcmp(d.name, "SwitchCube") == 0 || std::strcmp(d.name, "SwitchSphere") == 0 || std::strcmp(d.name, "SwitchCylinder") == 0)
                return hash == columnHash("Obj_arg0") || hash == columnHash("Obj_arg1") || hash == columnHash("Obj_arg2");
            if (std::strcmp(d.manager, "Water") == 0)
                return hash == columnHash("Obj_arg1") || hash == columnHash("Obj_arg2");
            return false;
        }
        bool transformColumn(std::uint32_t hash) {
            for (const char* key : {"pos_x", "pos_y", "pos_z", "dir_x", "dir_y", "dir_z", "scale_x", "scale_y", "scale_z"})
                if (hash == columnHash(key))
                    return true;
            return false;
        }
        void transform(AreaObj* area, const A::Volume& volume) {
            const TVec3f pos(volume.origin.x, volume.origin.y, volume.origin.z);
            const auto angles = toEulerDegrees(volume.axes);
            const TVec3f dir(angles.x, angles.y, angles.z), up(volume.axes.y.x, volume.axes.y.y, volume.axes.y.z);
            switch (area->mFormType) {
            case AreaForm::Type_Cube1:
            case AreaForm::Type_Cube2: {
                auto* form = area->getForm< AreaFormCube >();
                form->mTranslation = pos;
                form->mRotation = dir;
                form->mScale.set(volume.size.x / 500, volume.size.y / 500, volume.size.z / 500);
                form->updateBoxParam();
                break;
            }
            case AreaForm::Type_Sphere: {
                auto* form = area->getForm< AreaFormSphere >();
                form->mTranslation = pos;
                form->mRadius = volume.size.x;
                form->mUp = up;
                break;
            }
            case AreaForm::Type_Bowl: {
                auto* form = area->getForm< AreaFormBowl >();
                form->mTranslation = pos;
                form->_20 = volume.size.x;
                form->mUp = up;
                break;
            }
            case AreaForm::Type_Cylinder: {
                auto* form = area->getForm< AreaFormCylinder >();
                form->mTranslation = pos;
                form->mRotation = up;
                form->mRadius = volume.size.x;
                form->mHeight = volume.size.y;
                break;
            }
            }
        }
        bool rowPreview(RowId row, int, const RowTransform& t) {
            PetariNative::HostAllocationScope host;
            bind();
            const auto* m = model();
            if (!m)
                return false;
            const auto v = A::describe(*m, row, &t);
            if (!v)
                return false;
            A::setPreview(row, t);
            auto it = bindings.find(row);
            if (v->valid && liveBounds(*v->definition) && it != bindings.end() && !it->second.area->getFollowMtx())
                transform(it->second.area, *v);
            return true;
        }
        bool rowUpdate(RowId row, const JMapInfoIter& iter) {
            bind();
            A::clearPreview(row);
            const auto* m = model();
            int z = 0, t = 0;
            if (!m || !m->locate(row, &z, &t) || !A::isAreaTable(m->tablePath(z, t)))
                return false;
            const auto* spec = A::definition(m->objectName(row));
            if (!spec || !liveBounds(*spec))
                return false;
            auto it = bindings.find(row);
            const bool inactive = m->state(row) == RowState::Deleted || !(m->rowMissions(row) & (1u << (m->scenario() - 1)));
            if (inactive) {
                if (it != bindings.end()) {
                    if (!it->second.removed)
                        it->second.valid = it->second.area->mIsValid;
                    it->second.area->invalidate();
                    it->second.area->_15 = false;
                    it->second.removed = true;
                }
                return true;
            }
            const auto volume = A::describe(*m, row);
            if (!volume || !volume->valid)
                return false;
            if (it == bindings.end()) {
                // New placements can only enter a manager with room; retired slots still count.
                if (spec->kind != A::Kind::Death && spec->kind != A::Kind::Switch && spec->kind != A::Kind::Water)
                    return false;
                auto* manager = MR::getAreaObjManager(spec->manager);
                if (!manager || manager->getNumAreaObj() >= spec->capacity) {
                    Hooks::reportError("Area manager is full; Reload to reclaim deleted areas before adding more");
                    return false;
                }
                EditorScene::Created created;
                const char* error = "";
                if (!EditorScene::createFromRow(iter, &created, &error)) {
                    Hooks::reportError(error);
                    return false;
                }
                Bound bound;
                bound.area = static_cast< AreaObj* >(created.mObj);
                bound.valid = bound.area->mIsValid;
                bound.enabled = bound.area->_15;
                snapshot(bound, row);
                {
                    PetariNative::HostAllocationScope host;
                    bindings[row] = std::move(bound);
                }
                return true;
            }
            auto& bound = it->second;
            if (bound.area->getFollowMtx() || std::strcmp(bound.area->mName, spec->name) != 0)
                return false;
            for (const auto& c : m->columns(z, t)) {
                const auto* current = m->cell(row, c.hash);
                auto before = bound.cells.find(c.hash);
                if (current && (before == bound.cells.end() || !same(before->second, *current)) && !transformColumn(c.hash) &&
                    !runtimeArg(*spec, c.hash))
                    return false;
            }
            transform(bound.area, *volume);
            int* args[] = {&bound.area->mObjArg0, &bound.area->mObjArg1, &bound.area->mObjArg2, &bound.area->mObjArg3,
                           &bound.area->mObjArg4, &bound.area->mObjArg5, &bound.area->mObjArg6, &bound.area->mObjArg7};
            for (int i = 0; i < 8; ++i) {
                const auto key = "Obj_arg" + std::to_string(i);
                if (const auto* c = m->cell(row, columnHash(key.c_str())))
                    *args[i] = c->i;
            }
            if (spec->kind == A::Kind::Water)
                static_cast< WaterArea* >(bound.area)->_3C = bound.area->mObjArg2;
            if (bound.removed) {
                bound.area->mIsValid = bound.valid;
                bound.area->_15 = bound.enabled;
                bound.removed = false;
            }
            snapshot(bound, row);
            return true;
        }
    }  // namespace
    void registerHooks() {
        Runtime::registerRowPreviewHandler(&rowPreview);
        Runtime::registerRowUpdater(&rowUpdate);
    }
    void update(const EditorPicking::View&) {
        PetariNative::HostAllocationScope host;
        registerHooks();
        bind();
        mesh = {};
        outlines.clear();
        const auto* m = model();
        if (!m)
            return;
        std::vector< RowId > selection;
        for (const auto& item : PetariNative::EditorPick::selection())
            if (item.row)
                selection.push_back({item.row});
        for (auto volume : A::collect(*m)) {
            if (!(volume.missions & (1u << (m->scenario() - 1))) || !A::shown(volume, selection))
                continue;
            if (const auto* preview = A::preview(volume.row))
                if (auto v = A::describe(*m, volume.row, preview))
                    volume = *v;
            const bool selected = std::find(selection.begin(), selection.end(), volume.row) != selection.end();
            auto geometry = A::geometry(volume, selected);
            outlines[volume.row] = geometry.lines;
            mesh.lines.insert(mesh.lines.end(), geometry.lines.begin(), geometry.lines.end());
            mesh.triangles.insert(mesh.triangles.end(), geometry.triangles.begin(), geometry.triangles.end());
        }
    }
    void draw() {
        D::draw(mesh);
    }
    bool bounds(RowId row, float c[3], float* r) {
        const auto* m = model();
        if (!m)
            return false;
        const auto v = A::describe(*m, row, A::preview(row));
        if (!v || !v->valid)
            return false;
        const auto p = A::center(*v);
        c[0] = p.x;
        c[1] = p.y;
        c[2] = p.z;
        *r = A::radius(*v);
        return true;
    }
    RowId pick(Vec3 eyeValue, Vec3 rayValue, float* distance) {
        if (scene != Hooks::sceneSerial())
            return {};
        const auto eye = D::vector(eyeValue), ray = D::vector(rayValue);
        RowId found;
        float best = 1e30f;
        for (const auto& [row, lines] : outlines)
            for (const auto& line : lines) {
                const auto a = D::vector(line.a), v = D::vector(line.b) - a, w = eye - a;
                const float vv = dot(v, v), rv = dot(ray, v), den = vv - rv * rv;
                if (vv < 1e-5f || den < 1e-5f)
                    continue;
                float t = (rv * dot(v, w) - vv * dot(ray, w)) / den;
                const auto nearest = a + v * std::clamp((dot(v, w) + t * rv) / vv, 0.f, 1.f);
                t = dot(nearest - eye, ray);
                if (t > 1 && t < best && length(eye + ray * t - nearest) < std::clamp(t * 0.007f, 12.f, 100.f)) {
                    best = t;
                    found = row;
                }
            }
        if (found.valid() && distance)
            *distance = best;
        return found;
    }
}  // namespace EditorAreas
#endif
