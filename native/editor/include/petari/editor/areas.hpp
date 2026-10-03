#pragma once
#include <optional>
#include <petari/editor/debug_draw.hpp>
#include <string>
#include <vector>

namespace PetariEditor::Areas {
    enum class Shape { Box, BaseBox, Sphere, Bowl, Cylinder };
    enum class Kind { Camera, Death, Water, Switch, Message, Other };
    enum class Visibility { Selection, All, None };
    struct Definition {
        const char* name;
        const char* manager;
        Shape shape;
        Kind kind;
        int capacity;
    };
    struct Volume {
        RowId row;
        int zone = 0, priority = -1, cameraId = -1;
        unsigned categories = 1, missions = 0;
        const Definition* definition = nullptr;
        Vec3 origin, size;
        Gizmo::Basis axes;
        bool valid = true;
    };
    const Definition* definition(const char* name);
    const char* shapeName(Shape shape);
    const char* kindName(Kind kind);
    std::uint32_t color(Kind kind, unsigned alpha = 220);
    bool isAreaTable(const char* path);
    std::optional< Volume > describe(const ModelView& model, RowId row, const RowTransform* preview = nullptr);
    std::vector< Volume > collect(const ModelView& model);
    Vec3 center(const Volume& volume);
    float radius(const Volume& volume);
    bool contains(const Volume& volume, Vec3 point);
    bool overlaps(const Volume& a, const Volume& b);
    DebugDraw::Mesh geometry(const Volume& volume, bool selected);
    RowId cameraRow(const ModelView& model, const Volume& area);
    void computeChecks(const ModelView& model, std::vector< Check >& out);
    void registerCatalog();

    void setVisibility(bool cameras, Visibility visibility);
    Visibility visibility(bool cameras);
    bool shown(const Volume& volume, const std::vector< RowId >& selection);
    void setPreview(RowId row, const RowTransform& transform);
    void clearPreview(RowId row);
    void clearPreviews();
    const RowTransform* preview(RowId row);
}  // namespace PetariEditor::Areas
