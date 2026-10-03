// The terrain brush's catalog class and checks, from rows (brush_catalog.hpp).
#include <petari/editor/terrain/brush_catalog.hpp>
#include <petari/editor/check_providers.hpp>
#include <petari/editor/terrain/brush.hpp>
#include <cstdio>
#include <cstring>

namespace PetariEditor::Terrain {

Catalog::ClassInfo brushClassInfo() {
    Catalog::ClassInfo info;
    info.name = kBrushClass;
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
    for (const MaterialPreset& preset : materialPalette()) {
        materials.push_back(preset.label);
    }
    for (const SurfacePreset& preset : surfacePresets()) {
        surfaces.push_back(preset.label);
    }
    info.args = {enumArg(0, "Shape", brushShapeNames(), "Its size is the scale: 1 = 1000 units (a sphere uses the X scale)"),
                 enumArg(1, "Material", materials, "A planet material from the game"),
                 enumArg(2, "Surface", surfaces, "How it feels to walk on"),
                 enumArg(3, "Gravity", gravityModeNames(), "Fitted: a cube, point or cylinder field around it; Down only: straight down over its top")};
    info.args[3].defaultValue = 1;
    for (std::size_t i = 0; i < brushShapeNames().size(); i++) {
        info.presets.push_back({brushShapeNames()[i] + " brush", {{0, static_cast< std::int32_t >(i)}}});
    }
    return info;
}

void registerBrushClass() {
    Catalog::addClass(brushClassInfo());
}

void brushChecks(const ModelView& model, std::vector< Check >& out) {
    auto intArg = [&](RowId id, const char* pColumn) {
        const Cell* pCell = model.cell(id, columnHash(pColumn));
        return pCell != nullptr ? pCell->i : -1;
    };
    for (int zone = 0; zone < model.zoneCount(); zone++) {
        for (int table = 0; table < model.tableCount(zone); table++) {
            model.forEachRow(zone, table, [&](RowId id) {
                const char* pName = model.objectName(id);
                if (pName == nullptr || std::strcmp(pName, kBrushClass) != 0 || model.state(id) == RowState::Deleted) {
                    return;
                }
                const RowTransform t = model.transform(id);
                const Brush brush = brushFromRow(intArg(id, "Obj_arg0"), intArg(id, "Obj_arg1"), intArg(id, "Obj_arg2"), intArg(id, "Obj_arg3"),
                                                 {t.scale.x, t.scale.y, t.scale.z});
                const std::size_t triangles = brushMesh(brush).triangleCount();
                if (triangles > 16000) {
                    char key[48];
                    std::snprintf(key, sizeof(key), "brush-tris:%llx", static_cast< unsigned long long >(id.v));
                    Check check;
                    check.key = key;
                    check.severity = Severity::Warning;
                    check.message = "Terrain brush has " + std::to_string(triangles) + " triangles; a collision file holds about 16,000";
                    check.rows = {id};
                    out.push_back(check);
                }
            });
        }
    }
}

void registerBrushChecks() {
    registerCheckProvider(&brushChecks);
}

}  // namespace PetariEditor::Terrain
