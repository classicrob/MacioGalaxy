// Blockout brushes. See petari/editor/terrain/brush.hpp.
#include <petari/editor/terrain/brush.hpp>
#include <algorithm>
#include <cmath>

namespace PetariEditor::Terrain {

namespace {
    constexpr float kMinSize = 50.0f;  // smaller brushes are not useful and make thin prisms
    constexpr float kPi = 3.14159265358979f;

    int clampIndex(int value, std::size_t count, int fallback) {
        if (value < 0) {
            return fallback;
        }
        return std::min(value, static_cast< int >(count) - 1);
    }
}  // namespace

const std::vector< std::string >& brushShapeNames() {
    static const std::vector< std::string > sNames = {"Box", "Sphere", "Cylinder", "Ramp"};
    return sNames;
}

const std::vector< std::string >& gravityModeNames() {
    static const std::vector< std::string > sNames = {"The stage's", "Fitted to the shape", "Down only"};
    return sNames;
}

const std::vector< MaterialPreset >& materialPalette() {
    // Planet materials from the disc (cloned on the player's machine, never shipped), and the
    // footstep sound each gets.
    static const std::vector< MaterialPreset > sPalette = {
        {"Checker", "BathtubPlanet", "bathtubplanet.bdl", "CheckMat_v", soundCode("Stone")},
        {"Grass", "FlowerPlanet", "flowerplanet.bdl", "gras_v", soundCode("Lawn")},
        {"Soil", "FlowerPlanet", "flowerplanet.bdl", "Soil02_v", soundCode("Soil")},
        {"Stone", "FlowerPlanet", "flowerplanet.bdl", "Stone_v", soundCode("Stone")},
        {"Wood", "FlowerPlanet", "flowerplanet.bdl", "Wood03_v", soundCode("WoodThick")},
        {"Metal", "AssemblyBlockPlanet", "assemblyblockplanet.bdl", "MetalA_v", soundCode("Metal")},
        {"Ice", "IceMountainPlanet", "icemountainplanet.bdl", "Ice01_v", soundCode("Ice")},
        {"Snow", "IceMountainPlanet", "icemountainplanet.bdl", "Snow01_v", soundCode("Snow")},
        {"Rock", "IceMountainPlanet", "icemountainplanet.bdl", "Rock_006_C_v", soundCode("Stone")},
        {"Lava", "LavaRotatePlanet", "lavarotateplanet.bdl", "LavaMat_v", soundCode("Stone")},
    };
    return sPalette;
}

const std::vector< SurfacePreset >& surfacePresets() {
    static const std::vector< SurfacePreset > sPresets = {
        {"Normal", floorCode("Normal"), wallCode("Normal")},
        {"Ice", floorCode("Ice"), wallCode("Normal")},
        {"Sand", floorCode("Sand"), wallCode("Normal")},
        {"Slippery", floorCode("Slip"), wallCode("NotWallJump")},
        {"Burns", floorCode("DamageFire"), wallCode("Normal")},
        {"Death", floorCode("Death"), wallCode("Normal")},
        {"Snow", floorCode("Snow"), wallCode("Normal")},
        {"No wall jump", floorCode("Normal"), wallCode("NotWallJump")},
    };
    return sPresets;
}

Brush brushFromRow(int arg0, int arg1, int arg2, int arg3, const Vec3& scale) {
    Brush brush;
    brush.shape = static_cast< BrushShape >(clampIndex(arg0, brushShapeNames().size(), 0));
    brush.material = clampIndex(arg1, materialPalette().size(), 0);
    brush.surface = clampIndex(arg2, surfacePresets().size(), 0);
    brush.gravity = arg3 == 0 ? GravityMode::Stage : arg3 == 2 ? GravityMode::DownOnly : GravityMode::Fitted;  // -1 (unset): fitted
    auto size = [](float s) { return std::max(std::fabs(s) * 1000.0f, kMinSize); };
    brush.size = {size(scale.x), size(scale.y), size(scale.z)};
    if (brush.shape == BrushShape::Sphere) {
        brush.size = {brush.size.x, brush.size.x, brush.size.x};
    } else if (brush.shape == BrushShape::Cylinder) {
        brush.size.z = brush.size.x;
    }
    return brush;
}

Mesh makeCylinder(const Vec3& base, float radius, float height, int segments, const Surface& surface, float uvScale) {
    Mesh mesh;
    mesh.surfaces.push_back(surface);
    segments = std::max(segments, 3);
    auto vertex = [&](const Vec3& p, const Vec3& n, float u, float v) {
        mesh.positions.push_back(p);
        mesh.normals.push_back(n);
        mesh.uvs.push_back(u);
        mesh.uvs.push_back(v);
        return static_cast< std::uint32_t >(mesh.positions.size() - 1);
    };
    const float circumference = 2.0f * kPi * radius;
    // The side: a ring of quads, normals outward; UVs wrap the circumference.
    for (int s = 0; s < segments; s++) {
        const float a0 = 2.0f * kPi * s / segments, a1 = 2.0f * kPi * (s + 1) / segments;
        const Vec3 n0{std::cos(a0), 0, -std::sin(a0)}, n1{std::cos(a1), 0, -std::sin(a1)};
        const float u0 = circumference * s / segments / uvScale, u1 = circumference * (s + 1) / segments / uvScale;
        const std::uint32_t b0 = vertex({base.x + radius * n0.x, base.y, base.z + radius * n0.z}, n0, u0, 0);
        const std::uint32_t b1 = vertex({base.x + radius * n1.x, base.y, base.z + radius * n1.z}, n1, u1, 0);
        const std::uint32_t t0 = vertex({base.x + radius * n0.x, base.y + height, base.z + radius * n0.z}, n0, u0, height / uvScale);
        const std::uint32_t t1 = vertex({base.x + radius * n1.x, base.y + height, base.z + radius * n1.z}, n1, u1, height / uvScale);
        // Segments run counter-clockwise seen from +y (angle towards -z), so (b0, b1, t1) and
        // (b0, t1, t0) face outward.
        mesh.indices.insert(mesh.indices.end(), {b0, b1, t1, b0, t1, t0});
    }
    // The caps: fans, planar UVs.
    for (int cap = 0; cap < 2; cap++) {
        const float y = base.y + (cap ? height : 0.0f);
        const Vec3 n{0, cap ? 1.0f : -1.0f, 0};
        const std::uint32_t centre = vertex({base.x, y, base.z}, n, base.x / uvScale, base.z / uvScale);
        for (int s = 0; s < segments; s++) {
            const float a0 = 2.0f * kPi * s / segments, a1 = 2.0f * kPi * (s + 1) / segments;
            const Vec3 p0{base.x + radius * std::cos(a0), y, base.z - radius * std::sin(a0)};
            const Vec3 p1{base.x + radius * std::cos(a1), y, base.z - radius * std::sin(a1)};
            const std::uint32_t v0 = vertex(p0, n, p0.x / uvScale, p0.z / uvScale);
            const std::uint32_t v1 = vertex(p1, n, p1.x / uvScale, p1.z / uvScale);
            if (cap) {
                mesh.indices.insert(mesh.indices.end(), {centre, v0, v1});
            } else {
                mesh.indices.insert(mesh.indices.end(), {centre, v1, v0});
            }
        }
    }
    mesh.triangleSurface.assign(mesh.triangleCount(), 0);
    return mesh;
}

Mesh makeRamp(const Vec3& base, const Vec3& size, const Surface& surface, float uvScale) {
    Mesh mesh;
    mesh.surfaces.push_back(surface);
    const float hx = size.x * 0.5f, hz = size.z * 0.5f, h = size.y;
    // Corners: bottom front/back, top back (the high edge).
    const Vec3 bfl{base.x - hx, base.y, base.z - hz}, bfr{base.x + hx, base.y, base.z - hz};
    const Vec3 bbl{base.x - hx, base.y, base.z + hz}, bbr{base.x + hx, base.y, base.z + hz};
    const Vec3 tbl{base.x - hx, base.y + h, base.z + hz}, tbr{base.x + hx, base.y + h, base.z + hz};
    auto face = [&](std::initializer_list< Vec3 > corners, const Vec3& n, int uAxis, int vAxis) {
        const std::uint32_t first = static_cast< std::uint32_t >(mesh.positions.size());
        for (const Vec3& p : corners) {
            const float c[3] = {p.x, p.y, p.z};
            mesh.positions.push_back(p);
            mesh.normals.push_back(n);
            mesh.uvs.push_back(c[uAxis] / uvScale);
            mesh.uvs.push_back(c[vAxis] / uvScale);
        }
        // Corners are given counter-clockwise seen from outside: a fan.
        for (std::uint32_t i = 1; i + 1 < corners.size(); i++) {
            mesh.indices.insert(mesh.indices.end(), {first, first + i, first + i + 1});
        }
    };
    const float slope = std::sqrt(h * h + size.z * size.z);
    const Vec3 up{0, size.z / slope, -h / slope};
    face({bfl, bfr, bbr, bbl}, {0, -1, 0}, 0, 2);   // bottom
    face({bbl, bbr, tbr, tbl}, {0, 0, 1}, 0, 1);    // back
    face({bfl, tbl, tbr, bfr}, up, 0, 2);           // slope
    face({bfl, bbl, tbl}, {-1, 0, 0}, 2, 1);        // left
    face({bfr, tbr, bbr}, {1, 0, 0}, 2, 1);         // right
    mesh.triangleSurface.assign(mesh.triangleCount(), 0);
    return mesh;
}

Mesh brushMesh(const Brush& brush) {
    const SurfacePreset& preset = surfacePresets()[static_cast< std::size_t >(brush.surface)];
    Surface surface;
    surface.floor = preset.floor;
    surface.wall = preset.wall;
    surface.sound = materialPalette()[static_cast< std::size_t >(brush.material)].sound;
    const Vec3& s = brush.size;
    switch (brush.shape) {
    case BrushShape::Sphere: {
        const float r = s.x * 0.5f;
        // About one ring per 150 units of radius, within 12-32 rings (1.5k-4k triangles).
        const int rings = std::clamp(static_cast< int >(r / 150.0f), 12, 32);
        return makeSphere({0, r, 0}, r, rings, rings * 4 / 3, surface);
    }
    case BrushShape::Cylinder: {
        const int segments = std::clamp(static_cast< int >(s.x / 100.0f), 16, 64);
        return makeCylinder({0, 0, 0}, s.x * 0.5f, s.y, segments, surface);
    }
    case BrushShape::Ramp:
        return makeRamp({0, 0, 0}, s, surface);
    case BrushShape::Box:
    default:
        return makeBox({0, s.y * 0.5f, 0}, {s.x * 0.5f, s.y * 0.5f, s.z * 0.5f}, surface);
    }
}

GravityFit fitGravity(const Brush& brush) {
    GravityFit fit;
    if (brush.gravity == GravityMode::Stage) {
        return fit;
    }
    const Vec3& s = brush.size;
    const float largest = std::max({s.x, s.y, s.z});
    fit.range = std::clamp(largest, 1000.0f, 4000.0f);
    if (brush.gravity == GravityMode::DownOnly) {
        fit.kind = GravityFit::Kind::Cube;  // the bounds, whatever the shape
        fit.center = {0, s.y * 0.5f, 0};
        fit.half = {s.x * 0.5f, s.y * 0.5f, s.z * 0.5f};
        fit.activeFaces = 4;  // +y only
        return fit;
    }
    switch (brush.shape) {
    case BrushShape::Sphere:
        fit.kind = GravityFit::Kind::Point;
        fit.radius = s.x * 0.5f;
        fit.center = {0, fit.radius, 0};
        break;
    case BrushShape::Cylinder:
        fit.kind = GravityFit::Kind::Segment;
        fit.radius = s.x * 0.5f;
        fit.a = {0, 0, 0};
        fit.b = {0, s.y, 0};
        break;
    case BrushShape::Ramp:
        fit.kind = GravityFit::Kind::Cube;
        fit.center = {0, s.y * 0.5f, 0};
        fit.half = {s.x * 0.5f, s.y * 0.5f, s.z * 0.5f};
        fit.activeFaces = 4;  // +y only: straight down onto the slope
        break;
    case BrushShape::Box:
    default:
        fit.kind = GravityFit::Kind::Cube;
        fit.center = {0, s.y * 0.5f, 0};
        fit.half = {s.x * 0.5f, s.y * 0.5f, s.z * 0.5f};
        fit.activeFaces = 0x3F;
        break;
    }
    return fit;
}

}  // namespace PetariEditor::Terrain
