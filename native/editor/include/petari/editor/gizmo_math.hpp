// The level editor's transform math (docs/dev/level-editor/UX.md section 6): gizmo drags, gravity
// frames, and the game's rotation convention. Header-only, no game or SDK dependency, so it is
// unit-tested alone (native/tests/editor_gizmo_tests.cpp) and shared by the UI and the game side.
#pragma once

#include <cmath>

namespace PetariEditor::Gizmo {

struct V3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(V3 a) { return std::sqrt(dot(a, a)); }
inline V3 normalized(V3 a, V3 fallback = {0.0f, 1.0f, 0.0f}) {
    const float l = length(a);
    return l > 1e-6f ? a * (1.0f / l) : fallback;
}

constexpr float kPi = 3.14159265358979f;
constexpr float kDeg = kPi / 180.0f;

// A rotation as three columns: the object's own X (right), Y (up) and Z (front) in world space.
struct Basis {
    V3 x{1, 0, 0}, y{0, 1, 0}, z{0, 0, 1};
};

// The game's placement rotation (MR::makeMtxTR): R = Rz * Ry * Rx, angles in degrees (BCSV dir_x/y/z).
inline Basis fromEulerDegrees(V3 degrees) {
    const float sx = std::sin(degrees.x * kDeg), cx = std::cos(degrees.x * kDeg);
    const float sy = std::sin(degrees.y * kDeg), cy = std::cos(degrees.y * kDeg);
    const float sz = std::sin(degrees.z * kDeg), cz = std::cos(degrees.z * kDeg);
    Basis b;
    b.x = {cz * cy, sz * cy, -sy};
    b.y = {cz * sy * sx - sz * cx, sz * sy * sx + cz * cx, cy * sx};
    b.z = {cz * sy * cx + sz * sx, sz * sy * cx - cz * sx, cy * cx};
    return b;
}

// The inverse: degrees with y in -90..90 (at the poles, x takes the whole turn and z is 0).
inline V3 toEulerDegrees(const Basis& b) {
    const float sy = -b.x.z;
    if (std::fabs(sy) < 0.99999f) {
        const float y = std::asin(sy);
        const float x = std::atan2(b.y.z, b.z.z);
        const float z = std::atan2(b.x.y, b.x.x);
        return {x / kDeg, y / kDeg, z / kDeg};
    }
    // Gimbal lock: R = Rz * Ry(+-90) * Rx; only x - z (or x + z) is defined. Put it all in x.
    const float y = sy > 0.0f ? kPi * 0.5f : -kPi * 0.5f;
    const float x = std::atan2(sy > 0.0f ? b.y.x : -b.y.x, b.y.y);
    return {x / kDeg, y / kDeg, 0.0f};
}

// Rotates v about the unit axis k by angle (radians), right-handed.
inline V3 rotate(V3 v, V3 k, float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    return v * c + cross(k, v) * s + k * (dot(k, v) * (1.0f - c));
}
inline Basis rotate(const Basis& b, V3 axis, float angle) {
    return {rotate(b.x, axis, angle), rotate(b.y, axis, angle), rotate(b.z, axis, angle)};
}

// The gravity frame at a point (UX section 6): up away from gravity, front the object's front
// flattened onto the ground (else any horizontal direction), right completing it.
inline Basis gravityFrame(V3 up, V3 frontHint) {
    Basis b;
    b.y = normalized(up);
    V3 front = frontHint - b.y * dot(frontHint, b.y);
    if (length(front) < 1e-4f) {
        const V3 any = std::fabs(b.y.y) < 0.9f ? V3{0, 1, 0} : V3{0, 0, 1};
        front = any - b.y * dot(any, b.y);
    }
    b.z = normalized(front);
    b.x = cross(b.y, b.z);
    return b;
}

// Stands an object up for a new up, keeping its heading (Shift+End, and sliding over a planet):
// the smallest rotation that takes its Y onto the new up.
inline Basis alignUp(const Basis& b, V3 newUp) {
    const V3 up = normalized(newUp);
    const V3 axis = cross(b.y, up);
    const float s = length(axis), c = dot(b.y, up);
    if (s < 1e-6f) {
        if (c > 0.0f) return b;
        // Upside down: half a turn about the object's own front.
        return rotate(b, normalized(b.z, V3{0, 0, 1}), kPi);
    }
    return rotate(b, axis * (1.0f / s), std::atan2(s, c));
}

// A ray against an axis line: the parameter along the axis (from its origin, in units of the
// unit direction) of the point nearest the ray. False when the ray runs along the axis.
inline bool closestOnAxis(V3 rayOrigin, V3 rayDir, V3 axisOrigin, V3 axisDir, float* t) {
    const V3 w = axisOrigin - rayOrigin;
    const float a = dot(axisDir, axisDir), b = dot(axisDir, rayDir), c = dot(rayDir, rayDir);
    const float d = dot(axisDir, w), e = dot(rayDir, w);
    const float denom = a * c - b * b;
    if (std::fabs(denom) < 1e-6f) return false;
    *t = (b * e - c * d) / denom;
    return true;
}

// A ray against a plane: the hit point. False when parallel or behind the ray.
inline bool rayPlane(V3 rayOrigin, V3 rayDir, V3 planePoint, V3 normal, V3* hit) {
    const float denom = dot(rayDir, normal);
    if (std::fabs(denom) < 1e-6f) return false;
    const float t = dot(planePoint - rayOrigin, normal) / denom;
    if (t < 0.0f) return false;
    *hit = rayOrigin + rayDir * t;
    return true;
}

// The angle (radians, right-handed about axis) from `from` to `to`, both seen in the plane across axis.
inline float angleAbout(V3 axis, V3 from, V3 to) {
    const V3 a = from - axis * dot(from, axis), b = to - axis * dot(to, axis);
    return std::atan2(dot(cross(a, b), axis), dot(a, b));
}

inline float snap(float value, float step) {
    return step > 0.0f ? std::round(value / step) * step : value;
}

// Grid snapping on the gravity frame's tangent plane through `origin` (UX section 6): the offset
// from origin is snapped along the frame's right and front; height along up is kept.
inline V3 snapOnPlane(V3 point, V3 origin, const Basis& frame, float step) {
    const V3 d = point - origin;
    return origin + frame.x * snap(dot(d, frame.x), step) + frame.y * dot(d, frame.y) + frame.z * snap(dot(d, frame.z), step);
}

}  // namespace PetariEditor::Gizmo
