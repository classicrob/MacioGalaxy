#pragma once
// The headless world for placement helpers (AGENT_API.md "Placing on the ground"): the collision of
// every row placed in the current mission (its ObjectData/<class>.arc .kcl, terrain brushes from the
// terrain library's mesh), in world space, with ray and nearest-surface queries; and an approximate
// gravity direction from the gravity rows. Live mode asks the running game instead (exact).
#include "level_internal.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace PetariEditor::LevelApi {

struct Triangle {
    G::V3 a, b, c;
    G::V3 n;  // the face normal (outward: the side Mario stands on)
};

/// The triangles of a .kcl (big-endian, as in ObjectData archives), in the model's own space. Empty when unreadable.
std::vector< Triangle > readKclTriangles(const Formats::Bytes& kcl);

struct Hit {
    float distance = 0;
    G::V3 point, normal;
    RowId row;  // the collider's row
};

struct Gravity {
    G::V3 down{0, -1, 0};
    std::string source;  // "GlobalPointGravity EggStarGalaxy:common/planetobjinfo:3", "nearest surface", "default"
    RowId row;
};

class World {
public:
    /// Rebuilds the colliders when the model's collision rows changed (archives are cached by class).
    void sync(const EditModel& model);

    /// The first surface along the ray (dir normalized); frontOnly: only surfaces facing the ray (ground seen from above).
    std::optional< Hit > ray(G::V3 origin, G::V3 dir, float maxDistance, bool frontOnly = true) const;
    std::optional< Hit > nearest(G::V3 point, float maxDistance) const;
    Gravity gravityAt(const EditModel& model, G::V3 point) const;

    struct Collider {
        RowId row;
        std::string name;
        G::V3 lo, hi;
        std::vector< Triangle > tris;  // world space
    };
    const std::vector< Collider >& colliders() const { return mColliders; }
    const std::vector< std::string >& skipped() const { return mSkipped; }  // classes with an archive but no .kcl read

private:
    std::vector< Collider > mColliders;
    std::vector< std::string > mSkipped;
    std::string mSignature;
    std::unordered_map< std::string, std::shared_ptr< const std::vector< Triangle > > > mCache;  // class -> local triangles (null: none)
};

}  // namespace PetariEditor::LevelApi
