#pragma once
// Native mods GenericCharacter and GenericWorld: plain lit shapes drawn with
// GX direct (positions and normals in world space), in flat colours.
#ifdef PETARI_NATIVE
#include <JSystem/JGeometry/TVec.hpp>
#include <revolution/gx.h>
#include <revolution/mtx.h>

namespace GenericShapes {
    // The draw state: the camera view, one lit colour channel, Z on. Lights:
    // the player's when lightPlayer, else whatever the caller loads (an actor's).
    void begin(bool lightPlayer);
    void end();
    void color(const GXColor& rColor);

    TVec3f apply(const Mtx m, const TVec3f& v);
    TVec3f rotate(const Mtx m, const TVec3f& v);
    TVec3f unitNormal(const TVec3f& v);

    // An ellipsoid centre c, radii r in m's space; dome: the upper half only.
    void ellipsoid(const Mtx m, const TVec3f& c, const TVec3f& r, bool dome);
    // A capsule in world space: a sphere at each end and the side between.
    void capsule(const TVec3f& a, f32 ra, const TVec3f& b, f32 rb);
    // A closed cylinder in m's space along its y axis, centre c.
    void cylinder(const Mtx m, const TVec3f& c, f32 radius, f32 halfHeight);
    // A five-pointed star in m's xy plane (facing z), centre c, extruded halfDepth each way.
    void starPrism(const Mtx m, const TVec3f& c, f32 outer, f32 inner, f32 halfDepth);
    // An axis-aligned box in m's space.
    void box(const Mtx m, const TVec3f& lo, const TVec3f& hi);
    // A flat arrow in m's xy plane pointing +y: a shaft (half width shaftHalf) from y0 to
    // headY, then a head (half width headHalf) to the tip y1; extruded halfDepth along z.
    void arrowPrism(const Mtx m, f32 y0, f32 headY, f32 y1, f32 shaftHalf, f32 headHalf, f32 halfDepth);
}  // namespace GenericShapes

#endif
