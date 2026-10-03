#include "vendor/meshoptimizer/src/meshoptimizer.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <petari/editor/import.hpp>
#include <petari/editor/terrain/brush.hpp>
#include <stdexcept>

namespace PetariEditor::Imports {
    namespace {
        Vec3 axis(Vec3 v, UpAxis up) {
            if (up == UpAxis::Z) {
                return {v.x, v.z, -v.y};
            }
            if (up == UpAxis::X) {
                return {-v.y, v.x, v.z};
            }
            return v;
        }
        Mesh transform(const Mesh& mesh, const Options& o, Vec3 pivot) {
            Mesh out = mesh;
            for (auto& p : out.positions) {
                p = axis(p, o.up);
                p = {p.x * o.scale - pivot.x, p.y * o.scale - pivot.y, p.z * o.scale - pivot.z};
            }
            for (auto& n : out.normals) {
                n = axis(n, o.up);
            }
            return out;
        }
        Mesh slice(const Mesh& mesh, size_t first, size_t count) {
            Mesh out;
            std::map< unsigned, unsigned > remap;
            for (size_t i = first * 3; i < (first + count) * 3; ++i) {
                const auto old = mesh.indices[i];
                auto [it, added] = remap.emplace(old, out.positions.size());
                if (added) {
                    out.positions.push_back(mesh.positions[old]);
                    if (!mesh.normals.empty()) {
                        out.normals.push_back(mesh.normals[old]);
                    }
                    if (!mesh.uvs.empty()) {
                        out.uvs.push_back(mesh.uvs[old * 2]);
                        out.uvs.push_back(mesh.uvs[old * 2 + 1]);
                    }
                }
                out.indices.push_back(it->second);
            }
            return out;
        }
        Mesh simplify(const Mesh& mesh, float ratio, bool collision) {
            if (mesh.indices.empty()) {
                return mesh;
            }
            Mesh out = mesh;
            if (collision) {
                std::vector< unsigned > remap(mesh.positions.size());
                const size_t n = meshopt_generateVertexRemap(remap.data(), mesh.indices.data(), mesh.indices.size(), mesh.positions.data(),
                                                             mesh.positions.size(), sizeof(Vec3));
                out.positions.resize(n);
                meshopt_remapVertexBuffer(out.positions.data(), mesh.positions.data(), mesh.positions.size(), sizeof(Vec3), remap.data());
                meshopt_remapIndexBuffer(out.indices.data(), mesh.indices.data(), mesh.indices.size(), remap.data());
                out.normals.clear();
                out.uvs.clear();
            }
            const size_t target = std::max< size_t >(3, size_t(out.indices.size() * ratio) / 3 * 3);
            std::vector< unsigned > reduced(out.indices.size());
            const size_t n =
                meshopt_simplify(reduced.data(), out.indices.data(), out.indices.size(), reinterpret_cast< const float* >(out.positions.data()),
                                 out.positions.size(), sizeof(Vec3), target, .02f, meshopt_SimplifyLockBorder, nullptr);
            reduced.resize(n);
            out.indices = std::move(reduced);
            return out;
        }
        Image reducedImage(const Image& image) {
            if (image.width <= 1024 && image.height <= 1024) {
                return image;
            }
            const float s = 1024.f / std::max(image.width, image.height);
            Image out{std::max(1, int(image.width * s)), std::max(1, int(image.height * s)), {}};
            out.rgba.resize(size_t(out.width) * out.height * 4);
            for (int y = 0; y < out.height; ++y) {
                for (int x = 0; x < out.width; ++x) {
                    const int sx = std::min(image.width - 1, int((x + .5f) / s)), sy = std::min(image.height - 1, int((y + .5f) / s));
                    std::copy_n(&image.rgba[(size_t(sy) * image.width + sx) * 4], 4, &out.rgba[(size_t(y) * out.width + x) * 4]);
                }
            }
            return out;
        }
    }  // namespace
    std::optional< Asset > prepare(const Scene& scene, const Options& options, std::string* error, const Scene* external) {
        try {
            if (!std::isfinite(options.scale) || options.scale <= 0 || options.scale > 100000) {
                throw std::runtime_error("Scale must be positive and at most 100,000.");
            }
            if (!std::isfinite(options.collisionRatio)) {
                throw std::runtime_error("Collision ratio must be finite.");
            }
            if (scene.primitives.empty() || scene.materials.empty()) {
                throw std::runtime_error("The model has no mesh or materials.");
            }
            Asset out;
            out.name = scene.name;
            out.gravity = options.gravity;
            out.warnings = scene.warnings;
            Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
            for (const auto& p : scene.primitives) {
                for (auto v : p.mesh.positions) {
                    v = axis(v, options.up);
                    v = {v.x * options.scale, v.y * options.scale, v.z * options.scale};
                    lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)};
                    hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)};
                }
            }
            if (lo.x > hi.x || !std::isfinite(lo.x) || !std::isfinite(hi.x) || std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z}) > 1e7f) {
                throw std::runtime_error("The scaled model has invalid or excessively large bounds.");
            }
            Vec3 pivot{};
            if (options.origin != Origin::Keep) {
                pivot = {(lo.x + hi.x) / 2, options.origin == Origin::BottomCenter ? lo.y : (lo.y + hi.y) / 2, (lo.z + hi.z) / 2};
            }
            for (size_t i = 0; i < scene.materials.size(); ++i) {
                const auto& src = scene.materials[i];
                const auto opt = i < options.materials.size() ? options.materials[i] : MaterialOptions{};
                Material m;
                m.name = src.name;
                m.color = src.color;
                m.donor = opt.donor;
                m.doubleSided = src.doubleSided;
                if (m.donor < -1 || m.donor >= int(Terrain::materialPalette().size())) {
                    throw std::runtime_error("Unknown disc material.");
                }
                for (auto& c : m.color) {
                    if (!std::isfinite(c)) {
                        throw std::runtime_error("Material contains a non-finite colour.");
                    }
                    c = std::clamp(c, 0.f, 1.f);
                }
                if (m.donor < 0) {
                    Image pixels{1, 1, Bytes{255, 255, 255, 255}};
                    if (src.image >= 0 && src.image < int(scene.images.size()) && !scene.images[src.image].rgba.empty()) {
                        const auto& im = scene.images[src.image];
                        pixels = reducedImage(im);
                        if (im.width > 1024 || im.height > 1024) {
                            out.warnings.push_back(src.name + ": texture resized to fit GX's 1024-pixel limit.");
                        }
                    }
                    std::string e;
                    m.texture = encodeTexture(pixels, opt.format, &e);
                    if (!e.empty()) {
                        throw std::runtime_error(e);
                    }
                    if (opt.format == TextureFormat::I8) {
                        out.warnings.push_back(src.name + ": I8 keeps luminance only; texture alpha is discarded.");
                    }
                }
                out.materials.push_back(std::move(m));
            }
            size_t renderTriangles = 0, textureBytes = 0;
            for (const auto& material : out.materials) {
                textureBytes += material.texture.data.size();
            }
            for (const auto& primitive : scene.primitives) {
                if (primitive.material < 0 || primitive.material >= int(out.materials.size())) {
                    throw std::runtime_error("Mesh has an invalid material.");
                }
                const auto mesh = transform(primitive.mesh, options, pivot);
                renderTriangles += mesh.triangleCount();
                const auto opt = primitive.material < int(options.materials.size()) ? options.materials[primitive.material] : MaterialOptions{};
                for (size_t first = 0; first < mesh.triangleCount(); first += 16000) {
                    Part part;
                    part.material = primitive.material;
                    part.mesh = slice(mesh, first, std::min< size_t >(16000, mesh.triangleCount() - first));
                    if (options.collision == Collision::Mesh) {
                        part.collision = part.mesh;
                    } else if (options.collision == Collision::Simplified) {
                        part.collision = simplify(part.mesh, std::clamp(options.collisionRatio, .01f, 1.f), true);
                    }
                    part.collision.surfaces = {opt.surface};
                    if (options.generateLods) {
                        part.middle = simplify(part.mesh, .5f, false);
                        part.low = simplify(part.mesh, .2f, false);
                    }
                    out.parts.push_back(std::move(part));
                }
            }
            if (out.parts.empty()) {
                throw std::runtime_error("Model has no triangles.");
            }
            if (options.collision == Collision::Bounds) {
                out.parts.front().collision =
                    Terrain::makeBox({(lo.x + hi.x) / 2 - pivot.x, (lo.y + hi.y) / 2 - pivot.y, (lo.z + hi.z) / 2 - pivot.z},
                                     {std::max(.5f, (hi.x - lo.x) / 2), std::max(.5f, (hi.y - lo.y) / 2), std::max(.5f, (hi.z - lo.z) / 2)},
                                     options.materials.empty() ? Terrain::Surface{} : options.materials[0].surface);
            }
            if (options.collision == Collision::External) {
                if (!external) {
                    throw std::runtime_error("Choose the separate collision model.");
                }
                size_t partIndex = 0;
                for (const auto& primitive : external->primitives) {
                    const auto mesh = transform(primitive.mesh, options, pivot);
                    for (size_t first = 0; first < mesh.triangleCount(); first += 16000) {
                        auto collision = slice(mesh, first, std::min< size_t >(16000, mesh.triangleCount() - first));
                        if (partIndex == out.parts.size()) {
                            out.parts.push_back({});
                        }
                        const auto opt =
                            primitive.material < int(options.materials.size()) ? options.materials[primitive.material] : MaterialOptions{};
                        collision.surfaces = {opt.surface};
                        out.parts[partIndex++].collision = std::move(collision);
                    }
                }
                if (!partIndex) {
                    throw std::runtime_error("Separate collision model has no triangles.");
                }
            }
            if (out.parts.size() > 64) {
                throw std::runtime_error("Import needs more than 64 parts. Reduce the model or collision first.");
            }
            if (renderTriangles > 50000) {
                out.warnings.push_back("Over 50,000 render triangles: this part may be expensive to draw.");
            }
            if (textureBytes > 8 * 1024 * 1024) {
                out.warnings.push_back("GX textures exceed 8 MiB; reduce image sizes or use CMPR.");
            }
            if (out.parts.size() > 1) {
                out.warnings.push_back("Split into " + std::to_string(out.parts.size()) +
                                       " parts for materials and the 16,000-triangle collision limit.");
            }
            std::sort(out.warnings.begin(), out.warnings.end());
            out.warnings.erase(std::unique(out.warnings.begin(), out.warnings.end()), out.warnings.end());
            return out;
        } catch (const std::exception& e) {
            *error = e.what();
            return {};
        }
    }
}  // namespace PetariEditor::Imports
