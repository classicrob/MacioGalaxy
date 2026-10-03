#pragma once
// Static model import. Neutral assets contain only user geometry/textures and disc-material references.
#include <array>
#include <filesystem>
#include <optional>
#include <petari/editor/terrain/mesh.hpp>
#include <petari/editor/terrain/model_writer.hpp>

namespace PetariEditor::Imports {
    using Formats::Bytes;
    using Terrain::Mesh;
    using Terrain::Vec3;

    struct Image {
        int width = 0, height = 0;
        Bytes rgba;
    };
    enum class TextureFormat : int { Auto = -1, I8 = 1, RGB5A3 = 5, CMPR = 14 };
    struct Texture {
        TextureFormat format = TextureFormat::CMPR;
        int width = 0, height = 0;
        Bytes data;
    };
    std::optional< Image > decodeImage(const Bytes& bytes, std::string* error);
    Texture encodeTexture(const Image& image, TextureFormat format, std::string* error);

    struct SourceMaterial {
        std::string name;
        std::array< float, 4 > color{1, 1, 1, 1};
        int image = -1;
        bool doubleSided = false;
    };
    struct Primitive {
        Mesh mesh;
        int material = 0;
    };
    struct Scene {
        std::string name;
        std::vector< Primitive > primitives;
        std::vector< SourceMaterial > materials;
        std::vector< Image > images;
        std::vector< std::string > warnings;
    };
    // Resolves local sibling buffers/textures, embedded GLB images and data URIs. No network requests.
    std::optional< Scene > load(const std::filesystem::path& path, std::string* error);

    enum class UpAxis { Y, Z, X };
    enum class Origin { Keep, Center, BottomCenter };
    enum class Collision { Mesh, Simplified, Bounds, None, External };
    struct MaterialOptions {
        int donor = -1;  // -1: imported look; otherwise Terrain::materialPalette index
        Terrain::Surface surface;
        TextureFormat format = TextureFormat::Auto;
    };
    struct Options {
        float scale = 100;
        UpAxis up = UpAxis::Y;
        Origin origin = Origin::BottomCenter;
        Collision collision = Collision::Simplified;
        float collisionRatio = .5f;
        bool generateLods = true;
        bool gravity = false;
        std::vector< MaterialOptions > materials;
    };
    struct Material {
        std::string name;
        std::array< float, 4 > color{1, 1, 1, 1};
        int donor = -1;
        bool doubleSided = false;
        Texture texture;
    };
    struct Part {
        Mesh mesh, collision, middle, low;
        int material = 0;
    };
    struct Asset {
        std::string name;
        std::vector< Material > materials;
        std::vector< Part > parts;
        bool gravity = false;
        std::vector< std::string > warnings;
    };
    // Each part has at most 16,000 collision triangles and <= 65,535 render vertices.
    // External collision uses the same scale/up-axis/origin transformation as the render scene.
    std::optional< Asset > prepare(const Scene& scene, const Options& options, std::string* error, const Scene* externalCollision = nullptr);
    Bytes serialize(const Asset& asset);
    std::optional< Asset > deserialize(const Bytes& bytes, std::string* error);
    std::string contentId(const Bytes& bytes);
    std::string className(const std::string& id, int part);
    bool parseClassName(const char* name, std::string* id = nullptr, int* part = nullptr);

    // A native single-stage textured/vertex-colour material; no disc data.
    Terrain::MaterialClone nativeMaterial(const Material& material);
}  // namespace PetariEditor::Imports
