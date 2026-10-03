#pragma once
#include <algorithm>
#include <cmath>
#include <petari/editor/edit_api.hpp>
#include <petari/editor/gizmo_math.hpp>
#include <vector>

namespace PetariEditor::DebugDraw {
    inline Gizmo::V3 vector(Vec3 p) {
        return {p.x, p.y, p.z};
    }
    inline Vec3 point(Gizmo::V3 p) {
        return {p.x, p.y, p.z};
    }
    struct Line {
        Vec3 a, b;
        std::uint32_t color = 0xffffffff;  // ABGR, as ImGui.
        float width = 2;
    };
    struct Triangle {
        Vec3 a, b, c;
        std::uint32_t color = 0xffffffff;
    };
    // Bound each immediate draw well below GX's u16 vertex count. Lines can expand
    // to four vertices and six indices per segment in renderer backends.
    inline constexpr std::size_t kMaxBatchVertices = 4096;
    inline constexpr std::size_t kMaxBatchLines = kMaxBatchVertices / 4;
    inline constexpr std::size_t kMaxBatchTriangles = kMaxBatchVertices / 3;
    inline std::uint8_t lineWidth(float width) {
        return static_cast< std::uint8_t >(std::clamp(std::isfinite(width) ? width * 6.0f : 6.0f, 6.0f, 255.0f));
    }
    inline std::size_t lineBatchEnd(const std::vector< Line >& lines, std::size_t base) {
        if (base >= lines.size())
            return base;
        const auto width = lineWidth(lines[base].width);
        std::size_t end = base + 1;
        while (end < lines.size() && end - base < kMaxBatchLines && lineWidth(lines[end].width) == width)
            ++end;
        return end;
    }
    struct Mesh {
        std::vector< Line > lines;
        std::vector< Triangle > triangles;
    };
    // Game thread, inside EditorDraw::draw3D, before its final GX restoration.
    void draw(const Mesh& mesh, float xrayAlpha = 0.15f);

    struct Camera {
        Vec3 eye, forward{0, 0, -1}, up{0, 1, 0};
        float fovy = 45, aspect = 1;
        Gizmo::V3 right() const {
            return Gizmo::normalized(Gizmo::cross(vector(forward), vector(up)), {1, 0, 0});
        }
        Gizmo::V3 screenUp() const {
            return Gizmo::cross(right(), Gizmo::normalized(vector(forward)));
        }
        bool project(Vec3 p, float* x, float* y) const {
            const auto d = vector(p) - vector(eye);
            const float depth = Gizmo::dot(d, Gizmo::normalized(vector(forward)));
            if (depth < 1 || aspect <= 0 || fovy <= 0 || fovy >= 179)
                return false;
            const float tangent = std::tan(fovy * 0.5f * Gizmo::kDeg);
            *x = Gizmo::dot(d, right()) / (depth * tangent * aspect);
            *y = Gizmo::dot(d, screenUp()) / (depth * tangent);
            return std::isfinite(*x) && std::isfinite(*y);
        }
        Vec3 ray(float x, float y) const {
            const float tangent = std::tan(fovy * 0.5f * Gizmo::kDeg);
            return point(Gizmo::normalized(vector(forward) + right() * (x * tangent * aspect) + screenUp() * (y * tangent)));
        }
        bool dragPlane(float x, float y, Vec3 anchor, Vec3* out) const {
            const auto direction = vector(ray(x, y));
            const float denominator = Gizmo::dot(direction, vector(forward));
            if (std::fabs(denominator) < 1e-5f)
                return false;
            const float t = Gizmo::dot(vector(anchor) - vector(eye), vector(forward)) / denominator;
            if (t < 1)
                return false;
            *out = point(vector(eye) + direction * t);
            return true;
        }
    };
    inline Vec3 transform(const Mtx34& matrix, Vec3 p) {
        return {matrix.m[0][0] * p.x + matrix.m[0][1] * p.y + matrix.m[0][2] * p.z + matrix.m[0][3],
                matrix.m[1][0] * p.x + matrix.m[1][1] * p.y + matrix.m[1][2] * p.z + matrix.m[1][3],
                matrix.m[2][0] * p.x + matrix.m[2][1] * p.y + matrix.m[2][2] * p.z + matrix.m[2][3]};
    }
    // Zones have rotation + translation, never scale (StageObjInfo).
    inline Vec3 inverseTransform(const Mtx34& matrix, Vec3 p) {
        p = {p.x - matrix.m[0][3], p.y - matrix.m[1][3], p.z - matrix.m[2][3]};
        return {matrix.m[0][0] * p.x + matrix.m[1][0] * p.y + matrix.m[2][0] * p.z,
                matrix.m[0][1] * p.x + matrix.m[1][1] * p.y + matrix.m[2][1] * p.z,
                matrix.m[0][2] * p.x + matrix.m[1][2] * p.y + matrix.m[2][2] * p.z};
    }
    inline void arrow(Mesh& mesh, Vec3 from, Vec3 to, Vec3 up, std::uint32_t color, float width = 2) {
        using namespace Gizmo;
        const V3 delta = vector(to) - vector(from);
        const float size = std::min(length(delta) * 0.25f, 80.0f);
        const V3 axis = normalized(delta), side = normalized(cross(axis, vector(up)), {1, 0, 0});
        mesh.lines.push_back({from, to, color, width});
        mesh.lines.push_back({to, point(vector(to) - axis * size + side * (size * 0.45f)), color, width});
        mesh.lines.push_back({to, point(vector(to) - axis * size - side * (size * 0.45f)), color, width});
    }
}  // namespace PetariEditor::DebugDraw
