#pragma once
#include <petari/editor/debug_draw.hpp>
#include <petari/editor/edit_api.hpp>
#include <string>
#include <vector>

namespace PetariEditor::Gravity {
    enum class Shape { Point, Cube, Cone, Disk, Torus, Parallel, Segment, Wire, Unknown };
    enum class Bound { Sphere, Box, Cylinder };
    struct Field {
        RowId row;
        std::uintptr_t identity = 0;
        std::string name;
        Shape shape = Shape::Unknown;
        Bound bound = Bound::Sphere;
        Vec3 center, x{1, 0, 0}, y{0, 1, 0}, z{0, 0, 1};
        float radius = 100, height = 0, innerRadius = 0, topCut = 0, degrees = 360;
        float range = -1, distant = 0;
        int priority = 0;
        bool active = true, inverse = false;
        std::vector< Vec3 > points;
    };
    using Line = DebugDraw::Line;
    using Triangle = DebugDraw::Triangle;
    using Mesh = DebugDraw::Mesh;
    struct Probe {
        bool valid = false, covered = false, onGround = false;
        Vec3 position, direction;
        // Valid when onGround: exact collision hit and face normal; position is the lifted gravity sample.
        Vec3 groundPosition, groundNormal;
        RowId winner;
        std::uintptr_t winnerIdentity = 0;
        std::string name;
        int priority = 0, beaten = 0, blended = 0;
    };
    struct DeadGround {
        RowId row;
        Vec3 position, normal;
    };
    struct View {
        std::vector< Field > fields;
        Probe probe;
        std::vector< DeadGround > deadGround;
        int registered = 0;
        float probeX = 0, probeY = 0;
    };
    const char* shapeName(Shape shape);
    const char* placementName(Shape shape, Bound bound = Bound::Sphere);
    Shape shapeForPlacement(const char* name);
    std::uint32_t color(Shape shape, unsigned alpha = 180);
    Mesh geometry(const Field& field, bool selected = false);
    // A single undoable edit. Changing shape also resets shape-specific arguments to safe defaults.
    Op shapeOp(const ModelView& model, RowId row, Shape shape, Bound bound = Bound::Sphere);
    Op numericOp(const ModelView& model, const std::vector< RowId >& rows, const char* column, float value, bool integer = false);
    std::vector< Check > makeChecks(const View& view, const std::vector< RowId >& baselineGround = {});
    void setVisible(bool visible);
    bool visible();
    void setAirProbe(bool air);
    bool airProbe();
    void publish(View view);
    View view();
    void reset();
}  // namespace PetariEditor::Gravity
