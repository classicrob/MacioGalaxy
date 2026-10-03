#pragma once
// Reading collision back (docs/dev/level-editor/TECH.md 6.3): a disc or generated .kcl
// (big-endian, as archives hold it) as triangles, and rays against them. Pure C++ for tools that
// run without the game (the headless editor's ground placement, tests); the game itself reads KCL
// through KCollisionServer.
//
// Each prism is rebuilt as the game does (KCollisionServer::getPos): from its first vertex, face
// normal, three edge normals and height. Triangles keep the prism's attribute (its .pa row).
#include <petari/editor/formats/formats.hpp>
#include <petari/editor/terrain/mesh.hpp>
#include <optional>
#include <string>

namespace PetariEditor::Terrain {

/// The triangles of `kcl`: three positions per triangle (not shared), indices 0..3n-1, triangle i
/// from prism i + 1 (a degenerate prism is a point), and triangleSurface = each prism's attribute.
/// Nullopt and *pError when the file is malformed.
std::optional< Mesh > readKcl(const Formats::Bytes& kcl, std::string* pError = nullptr);

struct RayHit {
    Vec3 point;
    Vec3 normal;        // the triangle's face normal (unit)
    float distance = 0.0f;  // along the (normalised) direction
    std::size_t triangle = 0;
    bool front = true;  // the ray met the side the normal faces
};

/// The nearest triangle of `mesh` the ray from `origin` along `direction` meets within
/// `maxDistance` (both sides; see RayHit::front). Brute force over the triangles; disc KCLs hold at
/// most about 16k.
std::optional< RayHit > raycast(const Mesh& mesh, const Vec3& origin, const Vec3& direction, float maxDistance);

}  // namespace PetariEditor::Terrain
