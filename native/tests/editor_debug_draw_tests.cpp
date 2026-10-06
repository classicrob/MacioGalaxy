#include <cassert>
#include <cmath>
#include <petari/editor/debug_draw.hpp>
using namespace PetariEditor;
int main() {
    DebugDraw::Camera c{{2, 3, 4}, {0, 0, -1}, {0, 1, 0}, 60, 1.8f};
    for (float x : {-0.9f, 0.0f, 0.9f})
        for (float y : {-0.9f, 0.0f, 0.9f}) {
            const auto ray = c.ray(x, y);
            const auto p = DebugDraw::point(DebugDraw::vector(c.eye) + DebugDraw::vector(ray) * 100);
            float sx = 0, sy = 0;
            assert(c.project(p, &sx, &sy));
            assert(std::fabs(sx - x) < 1e-5f && std::fabs(sy - y) < 1e-5f);
        }
    float x, y;
    assert(!c.project({2, 3, 5}, &x, &y));
    Vec3 drag;
    assert(c.dragPlane(0.5f, 0.2f, {0, 0, -100}, &drag));
    assert(std::fabs(drag.z + 100) < 1e-4f);
    Mtx34 zone{{{0, 0, 1, 40}, {0, 1, 0, 20}, {-1, 0, 0, 60}}};
    auto p = DebugDraw::inverseTransform(zone, DebugDraw::transform(zone, {7, 11, 13}));
    assert(p.x == 7 && p.y == 11 && p.z == 13);
    DebugDraw::Mesh mesh;
    DebugDraw::arrow(mesh, {0, 0, 0}, {100, 0, 0}, {0, 1, 0}, 0xffffffff);
    assert(mesh.lines.size() == 3);
    // Large homogeneous overlays and width transitions preserve every segment.
    mesh.lines.assign(100000, {{0, 0, 0}, {1, 1, 1}, 0xffffffff, 2});
    mesh.lines[1500].width = 3;
    std::size_t total = 0, batches = 0;
    for (std::size_t base = 0; base < mesh.lines.size();) {
        const auto end = DebugDraw::lineBatchEnd(mesh.lines, base);
        assert(end > base && end <= mesh.lines.size());
        assert((end - base) * 4 <= DebugDraw::kMaxBatchVertices);
        for (std::size_t i = base; i < end; ++i)
            assert(DebugDraw::lineWidth(mesh.lines[i].width) == DebugDraw::lineWidth(mesh.lines[base].width));
        total += end - base;
        ++batches;
        base = end;
    }
    assert(total == 100000 && batches > 90);
    assert(DebugDraw::kMaxBatchTriangles * 3 <= DebugDraw::kMaxBatchVertices);
    assert(DebugDraw::lineBatchEnd(mesh.lines, mesh.lines.size()) == mesh.lines.size());
}
