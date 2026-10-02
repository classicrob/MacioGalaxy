// SceneSync for terrain brushes. See terrain_sync.hpp.
#include "terrain_sync.hpp"

#include "edit_model.hpp"
#include "runtime_core.hpp"

#include "Game/Editor/EditorScene.hpp"
#include "Game/Editor/EditorTerrain.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "checks.hpp"
#include <petari/editor/catalog.hpp>
#include <petari/editor/terrain/brush.hpp>
#include <petari/editor/terrain/kcl_writer.hpp>
#include <petari/editor/terrain/model_writer.hpp>
#include <petari/host_allocation.hpp>
#include <petari/kcl_collision.hpp>
#include <petari/platform/dvd.hpp>
#include <revolution/dvd.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <vector>

namespace PetariEditor::TerrainSync {
namespace {
    namespace T = Terrain;

    struct Built {
        T::BrushShape shape = T::BrushShape::Box;
        T::Vec3 size;
        int material = -1;
        int surface = -1;
        bool fittedGravity = false;
        bool sameGeometry(const T::Brush& b) const {
            // The material sets the surface's sound too, so it is part of the collision.
            return shape == b.shape && size.x == b.size.x && size.y == b.size.y && size.z == b.size.z && surface == b.surface &&
                   material == b.material;
        }
    };

    struct LiveBrush {
        EditorTerrain::Part* part = nullptr;
        Built built;
        std::size_t triangles = 0;
    };

    std::map< RowId, LiveBrush >& live() {
        static std::map< RowId, LiveBrush > sLive;
        return sLive;
    }

    // Collision buffers the scene's parts use in place: freed when the next scene is bound.
    std::vector< void* >& sceneBuffers() {
        static std::vector< void* > sBuffers;
        return sBuffers;
    }

    // Names actors and resource holders keep pointers to: they live for the process.
    const char* persistent(const std::string& text) {
        static std::deque< std::string > sNames;
        PetariNative::HostAllocationScope host;
        sNames.push_back(text);
        return sNames.back().c_str();
    }

    std::optional< Formats::Bytes > readDisc(const std::string& path) {
        // Through the mounted disc (enabled mod files win, as for the game).
        const s32 entry = DVDConvertPathToEntrynum(path.c_str());
        if (entry < 0) {
            return std::nullopt;
        }
        std::ifstream in(PetariNative::Platform::DVD::hostPathForEntry(entry), std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        return Formats::Bytes(std::istreambuf_iterator< char >(in), {});
    }

    // The palette's materials, cloned once per session from the player's disc.
    const T::MaterialClone* materialClone(int index, std::string* pError) {
        static std::map< int, T::MaterialClone > sClones;
        auto it = sClones.find(index);
        if (it != sClones.end()) {
            return &it->second;
        }
        const T::MaterialPreset& preset = T::materialPalette()[static_cast< std::size_t >(index)];
        const auto raw = readDisc("/ObjectData/" + preset.donorArchive + ".arc");
        const auto archive = raw ? Formats::Archive::parse(*raw) : std::nullopt;
        const Formats::Bytes* pModel = archive ? archive->file(preset.donorModel) : nullptr;
        if (pModel == nullptr) {
            *pError = "the material " + preset.label + " needs " + preset.donorArchive + " from the disc";
            return nullptr;
        }
        auto clone = T::cloneMaterial(*pModel, preset.material, pError);
        if (!clone) {
            return nullptr;
        }
        return &sClones.emplace(index, std::move(*clone)).first->second;
    }

    std::int32_t intArg(const EditModel& model, RowId id, const char* pColumn) {
        const Cell* pCell = model.cell(id, columnHash(pColumn));
        return pCell != nullptr ? pCell->i : -1;
    }

    std::string idText(RowId id) {
        char text[24];
        std::snprintf(text, sizeof(text), "%llx", static_cast< unsigned long long >(id.v));
        return text;
    }

    void placePart(EditorTerrain::Part* pPart, std::int32_t zoneId, const RowTransform& t) {
        const f32 pos[3] = {t.pos.x, t.pos.y, t.pos.z}, dir[3] = {t.dirDeg.x, t.dirDeg.y, t.dirDeg.z},
                  scale[3] = {t.scale.x, t.scale.y, t.scale.z};
        EditorScene::applyTransform(pPart, zoneId, pos, dir, scale);
    }
}  // namespace

void registerWithEditor() {
    static bool sDone = false;
    // The class (Browser cards, the Inspector's named args) after every catalog load (addClass
    // replaces it); the check once.
    Catalog::ClassInfo info;
    info.name = T::kBrushClass;
    info.displayName = "Terrain brush";
    info.description = "A blockout shape you can walk on, in a material from the game, with its own gravity";
    info.aliases = {"brush", "terrain", "blockout", "box", "sphere", "cylinder", "ramp", "platform"};
    info.category = Catalog::Category::Planet;
    info.placeable = true;
    info.curated = true;
    info.keepable = true;
    info.hasCollision = true;
    auto enumArg = [](int index, const char* pName, const std::vector< std::string >& labels, const char* pHelp) {
        Catalog::Arg arg;
        arg.index = index;
        arg.name = pName;
        arg.type = Catalog::ArgType::Enum;
        arg.defaultValue = 0;
        arg.help = pHelp;
        for (std::size_t i = 0; i < labels.size(); i++) {
            arg.choices.push_back({static_cast< std::int32_t >(i), labels[i]});
        }
        return arg;
    };
    std::vector< std::string > materials, surfaces;
    for (const T::MaterialPreset& preset : T::materialPalette()) {
        materials.push_back(preset.label);
    }
    for (const T::SurfacePreset& preset : T::surfacePresets()) {
        surfaces.push_back(preset.label);
    }
    info.args = {enumArg(0, "Shape", T::brushShapeNames(), "Its size is the scale: 1 = 1000 units (a sphere uses the X scale)"),
                 enumArg(1, "Material", materials, "A planet material from the game"),
                 enumArg(2, "Surface", surfaces, "How it feels to walk on"),
                 enumArg(3, "Gravity", {"The stage's", "Fitted to the shape"}, "Fitted: a cube, point or cylinder field around it")};
    info.args[3].defaultValue = 1;
    for (std::size_t i = 0; i < T::brushShapeNames().size(); i++) {
        info.presets.push_back({T::brushShapeNames()[i] + " brush", {{0, static_cast< std::int32_t >(i)}}});
    }
    Catalog::addClass(info);
    if (sDone) {
        return;
    }
    sDone = true;
    registerCheckProvider([](const ModelView& model, std::vector< Check >& out) {
        for (const auto& [id, brush] : live()) {
            if (brush.triangles > 16000) {
                Check check;
                check.key = "brush-tris:" + idText(id);
                check.severity = Severity::Warning;
                check.message = "Terrain brush has " + std::to_string(brush.triangles) + " triangles; a collision file holds about 16,000";
                check.rows = {id};
                out.push_back(check);
            }
        }
        (void)model;
    });
}

bool isBrush(const char* pName) {
    return pName != nullptr && std::strcmp(pName, T::kBrushClass) == 0;
}

void syncBrush(const EditModel& model, RowId id) {
    int zone = 0, table = 0;
    if (!model.locate(id, &zone, &table)) {
        return;
    }
    const RowTransform t = model.transform(id);
    const std::int32_t zoneId = model.zoneId(zone);
    T::Brush brush;
    {
        PetariNative::HostAllocationScope host;
        brush = T::brushFromRow(intArg(model, id, "Obj_arg0"), intArg(model, id, "Obj_arg1"), intArg(model, id, "Obj_arg2"),
                                intArg(model, id, "Obj_arg3"), {t.scale.x, t.scale.y, t.scale.z});
    }
    LiveBrush& entry = live()[id];

    // Collision and model: rebuilt when the brush's geometry or look changed.
    const bool geometry = entry.part == nullptr || !entry.built.sameGeometry(brush);
    const bool look = geometry;
    std::string error;
    // A new archive name each time: the game caches resource holders by name. The model file in
    // it is <name>.bmd, as the game looks it up.
    static int sVersion = 0;
    const std::string name = look ? "EditorBrush_" + idText(id) + "_" + std::to_string(++sVersion) : std::string();
    void* pKcl = nullptr;
    void* pPa = nullptr;
    Formats::Bytes archive;
    if (geometry || look) {
        PetariNative::HostAllocationScope host;
        const T::Mesh mesh = T::brushMesh(brush);
        if (geometry) {
            const T::KclResult kcl = T::buildKcl(mesh);
            if (!kcl.error.empty()) {
                Core::reportError("Brush: " + kcl.error);
                return;
            }
            pKcl = std::malloc(kcl.kcl.size());
            pPa = std::malloc(kcl.pa.size());
            std::memcpy(pKcl, kcl.kcl.data(), kcl.kcl.size());
            std::memcpy(pPa, kcl.pa.data(), kcl.pa.size());
            if (const char* pError = PetariNative::KCL::convertInPlace(pKcl, static_cast< u32 >(kcl.kcl.size()))) {
                std::free(pKcl);
                std::free(pPa);
                Core::reportError(std::string("Brush collision: ") + pError);
                return;
            }
            sceneBuffers().push_back(pKcl);
            sceneBuffers().push_back(pPa);
            entry.triangles = static_cast< std::size_t >(kcl.stats.triangles);
        }
        if (const T::MaterialClone* pMaterial = materialClone(brush.material, &error)) {
            const T::ModelResult result = T::buildModel(mesh, *pMaterial);
            if (result.error.empty()) {
                std::string base = name;
                for (char& c : base) {
                    c = static_cast< char >(std::tolower(static_cast< unsigned char >(c)));
                }
                archive = Formats::Archive::create(base, {{base + ".bmd", result.model}}).build();
            } else {
                error = result.error;
            }
        }
    }
    if (!error.empty()) {
        Core::reportError("Brush: " + error);  // collision still updates; the look stays as it was
    }

    if (entry.part == nullptr) {
        const char* pError = "";
        entry.part = EditorTerrain::createPart(persistent("EditorBrush_" + idText(id)), TVec3f(0, 0, 0), pKcl, pPa, &pError);
        if (entry.part == nullptr) {
            live().erase(id);
            Core::reportError(std::string("Brush: ") + pError);
            return;
        }
    } else if (geometry) {
        entry.part->setCollision(pKcl, pPa);
    }
    placePart(entry.part, zoneId, t);
    entry.part->setBuiltScale(TVec3f(t.scale.x, t.scale.y, t.scale.z));

    if (!archive.empty()) {
        const char* pName = persistent(name);
        EditorTerrain::injectArchive(persistent("/ObjectData/" + name + ".arc"), archive.data(), static_cast< u32 >(archive.size()));
        const char* pError = "";
        LiveActor* pModel = EditorTerrain::createModel(pName, pName, entry.part->mBaseMtx.toMtxPtr(), &pError);
        if (pModel != nullptr) {
            entry.part->setModel(pModel);
        } else {
            Core::reportError(std::string("Brush model: ") + pError);
        }
    }

    // Gravity fitted to the brush, in its own space (the part's matrix carries it).
    const T::GravityFit fit = T::fitGravity(brush);
    EditorTerrain::Part::GravityShape shape;
    using Kind = EditorTerrain::Part::GravityShape::Kind;
    shape.kind = fit.kind == T::GravityFit::Kind::Cube ? Kind::Cube
                 : fit.kind == T::GravityFit::Kind::Point ? Kind::Point
                 : fit.kind == T::GravityFit::Kind::Segment ? Kind::Segment : Kind::None;
    shape.center.set(fit.center.x, fit.center.y, fit.center.z);
    shape.half.set(fit.half.x, fit.half.y, fit.half.z);
    shape.a.set(fit.a.x, fit.a.y, fit.a.z);
    shape.b.set(fit.b.x, fit.b.y, fit.b.z);
    shape.activeFaces = fit.activeFaces;
    shape.radius = fit.radius;
    shape.range = fit.range;
    if (!entry.part->setGravity(shape)) {
        Core::reportError("Brush gravity: the scene's 128 gravity fields are in use (a reload frees retired ones)");
    }

    entry.built.shape = brush.shape;
    entry.built.size = brush.size;
    entry.built.material = brush.material;
    entry.built.surface = brush.surface;
    entry.built.fittedGravity = brush.fittedGravity;
    Core::bindActor(id, entry.part);
}

void retireBrush(RowId id) {
    auto it = live().find(id);
    if (it == live().end()) {
        return;
    }
    if (it->second.part != nullptr) {
        it->second.part->retire();
    }
    live().erase(it);
    Core::unbindRow(id);
}

void sceneBound(const EditModel& model) {
    // The previous scene and its parts are gone.
    live().clear();
    for (void* pBuffer : sceneBuffers()) {
        std::free(pBuffer);
    }
    sceneBuffers().clear();
    for (int zone = 0; zone < model.zoneCount(); zone++) {
        if (!model.zonePlaced(zone)) {
            continue;
        }
        const std::uint32_t layers = model.activeLayers(zone, model.scenario());
        for (int table = 0; table < model.tableCount(zone); table++) {
            const std::string path = model.tablePath(zone, table);
            const int layer = model.tableLayer(zone, table);
            if (path.size() < 8 || path.compare(path.size() - 8, 8, "/objinfo") != 0 || layer < 0 || !(layers & (1u << layer))) {
                continue;
            }
            std::vector< RowId > brushes;
            model.forEachRow(zone, table, [&](RowId id) {
                if (model.state(id) != RowState::Deleted && isBrush(model.objectName(id))) {
                    brushes.push_back(id);
                }
            });
            for (RowId id : brushes) {
                syncBrush(model, id);
            }
        }
    }
}

std::size_t triangleCount(RowId id) {
    auto it = live().find(id);
    return it != live().end() ? it->second.triangles : 0;
}

J3DModel* modelForRow(RowId id) {
    auto it = live().find(id);
    if (it == live().end() || it->second.part == nullptr || it->second.part->mModel == nullptr) {
        return nullptr;
    }
    return MR::getJ3DModel(it->second.part->mModel);
}

}  // namespace PetariEditor::TerrainSync
