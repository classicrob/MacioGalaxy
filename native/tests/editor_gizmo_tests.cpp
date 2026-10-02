// The level editor's transform math (petari/editor/gizmo_math.hpp).
#include <petari/editor/gizmo_math.hpp>

#include <cstdio>
#include <cstdlib>

namespace {

using namespace PetariEditor::Gizmo;

int gFailures = 0;
int gChecks = 0;
void check(bool ok, const char* what) {
    ++gChecks;
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++gFailures;
    }
}
bool near(float a, float b, float tolerance = 1e-3f) {
    return std::fabs(a - b) <= tolerance;
}
bool near(V3 a, V3 b, float tolerance = 1e-3f) {
    return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}
bool sameRotation(const Basis& a, const Basis& b) {
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}

void testEuler() {
    // MR::makeMtxTR's columns for a known angle set (computed independently from Rz*Ry*Rx).
    const Basis b = fromEulerDegrees({30.0f, 45.0f, 60.0f});
    check(near(b.x, {0.353553f, 0.612372f, -0.707107f}), "X column of Rz*Ry*Rx");
    check(near(length(b.y), 1.0f) && near(dot(b.x, b.y), 0.0f) && near(dot(b.y, b.z), 0.0f), "orthonormal");
    check(near(cross(b.x, b.y), b.z), "right-handed");
    // Round trips, including negative and large angles.
    const V3 cases[] = {{0, 0, 0}, {30, 45, 60}, {-170, 10, 95}, {0, 90, 0}, {12, -89.5f, -33}, {179, 0, -179}};
    for (V3 e : cases) {
        const Basis r = fromEulerDegrees(e);
        check(sameRotation(fromEulerDegrees(toEulerDegrees(r)), r), "Euler round trip gives the same rotation");
    }
    check(near(toEulerDegrees(fromEulerDegrees({10, 20, 30})), {10, 20, 30}, 1e-2f), "plain angles come back as typed");
}

void testFrames() {
    // A planet's side: gravity pulls along -X, so up is +X; front follows the object's front, flattened.
    const Basis f = gravityFrame({1, 0, 0}, {0.3f, 0, 1});
    check(near(f.y, {1, 0, 0}) && near(f.z, {0, 0, 1}) && near(f.x, {0, -1, 0}), "gravity frame on a planet's side (right = up x front)");
    // Front along up (looking straight down): still a valid frame.
    const Basis g = gravityFrame({0, 1, 0}, {0, 5, 0});
    check(near(length(g.z), 1.0f) && near(dot(g.z, g.y), 0.0f), "front along up falls back");

    // Standing up for a new gravity keeps the heading.
    const Basis upright = fromEulerDegrees({0, 35, 0});  // turned 35 degrees about Y
    const Basis tilted = alignUp(upright, {0, 0, 1});     // now on a wall facing +Z
    check(near(tilted.y, {0, 0, 1}), "aligned up is the new up");
    check(near(alignUp(tilted, {0, 1, 0}).z, upright.z), "aligning back restores the heading");
    check(sameRotation(alignUp(upright, {0, 1, 0}), upright), "already upright: unchanged");
    check(near(alignUp(upright, {0, -1, 0}).y, {0, -1, 0}), "upside down is handled");
}

void testDrags() {
    // A ray looking down -Z at height 5, against the X axis through the origin: x of the ray's origin.
    float t = 0.0f;
    check(closestOnAxis({3, 5, 10}, {0, 0, -1}, {0, 0, 0}, {1, 0, 0}, &t) && near(t, 3.0f), "drag along X follows the cursor");
    check(!closestOnAxis({0, 0, 10}, {1, 0, 0}, {0, 0, 0}, {1, 0, 0}, &t), "a ray along the axis is refused");
    V3 hit;
    check(rayPlane({0, 10, 0}, normalized({1, -1, 0}), {0, 0, 0}, {0, 1, 0}, &hit) && near(hit, {10, 0, 0}), "ray meets the ground plane");
    check(!rayPlane({0, 10, 0}, {0, 1, 0}, {0, 0, 0}, {0, 1, 0}, &hit), "a ray away from the plane misses");
    check(near(angleAbout({0, 1, 0}, {1, 0, 0}, {0, 0, -1}), kPi * 0.5f), "a quarter turn about up");
    check(near(angleAbout({0, 1, 0}, {1, 5, 0}, {1, -3, 0}), 0.0f), "height does not count as turning");
}

void testSnapping() {
    check(near(snap(149.0f, 100.0f), 100.0f) && near(snap(151.0f, 100.0f), 200.0f) && near(snap(7.0f, 0.0f), 7.0f), "grid steps");
    const Basis side = gravityFrame({1, 0, 0}, {0, 0, 1});
    // On a planet's side the grid lies on the plane across +X: y and z snap, x (height) is kept.
    check(near(snapOnPlane({1037, 260, -140}, {1000, 0, 0}, side, 100.0f), {1037, 300, -100}), "grid on the tangent plane");
}

}  // namespace

int main() {
    testEuler();
    testFrames();
    testDrags();
    testSnapping();
    if (gFailures != 0) {
        std::fprintf(stderr, "editor_gizmo_tests: %d of %d checks failed\n", gFailures, gChecks);
        return 1;
    }
    std::printf("editor_gizmo_tests: %d checks passed\n", gChecks);
    return 0;
}
