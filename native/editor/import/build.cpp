#include <algorithm>
#include <cmath>
#include <petari/editor/import_assets.hpp>
#include <petari/editor/terrain/brush.hpp>
#include <petari/editor/terrain/kcl_writer.hpp>
#include <stdexcept>

namespace PetariEditor::Imports {
    namespace {
        Mesh scaled(Mesh mesh, Vec3 s) {
            for (auto& p : mesh.positions) {
                p = {p.x * s.x, p.y * s.y, p.z * s.z};
            }
            for (auto& n : mesh.normals) {
                n = {n.x / s.x, n.y / s.y, n.z / s.z};
                const float length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
                if (length > 0) {
                    n = {n.x / length, n.y / length, n.z / length};
                }
            }
            if (s.x * s.y * s.z < 0) {
                for (size_t i = 0; i < mesh.indices.size(); i += 3) {
                    std::swap(mesh.indices[i + 1], mesh.indices[i + 2]);
                }
            }
            return mesh;
        }
    }  // namespace
    BuiltPart buildPart(const Asset& asset, int partIndex, Vec3 scale, const Reader& readDisc) {
        BuiltPart out;
        try {
            if (partIndex < 0 || partIndex >= int(asset.parts.size())) {
                throw std::runtime_error("Imported part is missing.");
            }
            for (float s : {scale.x, scale.y, scale.z}) {
                if (!std::isfinite(s) || std::abs(s) < .0001f || std::abs(s) > 10000) {
                    throw std::runtime_error("Imported part scale must be finite and nonzero.");
                }
            }
            const auto& part = asset.parts[partIndex];
            if (part.material < 0 || part.material >= int(asset.materials.size())) {
                throw std::runtime_error("Imported material is missing.");
            }
            const auto& material = asset.materials[part.material];
            Terrain::MaterialClone clone;
            if (material.donor >= 0) {
                const auto& palette = Terrain::materialPalette();
                if (material.donor >= int(palette.size())) {
                    throw std::runtime_error("Imported donor material is invalid.");
                }
                const auto& donor = palette[material.donor];
                const auto bytes = readDisc ? readDisc("/ObjectData/" + donor.donorArchive + ".arc") : std::nullopt;
                const auto archive = bytes ? Formats::Archive::parse(*bytes) : std::nullopt;
                const auto* model = archive ? archive->file(donor.donorModel) : nullptr;
                if (!model) {
                    throw std::runtime_error("Material needs " + donor.donorArchive + " from the disc.");
                }
                std::string error;
                auto result = Terrain::cloneMaterial(*model, donor.material, &error);
                if (!result) {
                    throw std::runtime_error(error);
                }
                clone = std::move(*result);
            } else {
                clone = nativeMaterial(material);
            }
            Mesh render = scaled(part.mesh, scale), collision = scaled(part.collision, scale);
            if (render.indices.empty()) {
                // Collision-only split: a transparent primitive gives PlanetMap a valid model.
                render = Terrain::makeBox({}, {.01f, .01f, .01f});
                Material invisible;
                invisible.color = {0, 0, 0, 0};
                clone = nativeMaterial(invisible);
            }
            auto make = [&](const Mesh& mesh) {
                const auto result = Terrain::buildModel(mesh, clone);
                if (!result.error.empty()) {
                    throw std::runtime_error(result.error);
                }
                return result.model;
            };
            out.model = make(render);
            if (!part.middle.indices.empty()) {
                out.middle = make(scaled(part.middle, scale));
            }
            if (!part.low.indices.empty()) {
                out.low = make(scaled(part.low, scale));
            }
            if (!collision.indices.empty()) {
                const auto kcl = Terrain::buildKcl(collision);
                if (!kcl.error.empty()) {
                    throw std::runtime_error(kcl.error);
                }
                out.kcl = kcl.kcl;
                out.pa = kcl.pa;
            }
            Mesh bounds = render;
            bounds.positions.insert(bounds.positions.end(), collision.positions.begin(), collision.positions.end());
            bounds.bounds(&out.minimum, &out.maximum);
        } catch (const std::exception& e) {
            out.error = e.what();
        }
        return out;
    }
}  // namespace PetariEditor::Imports
