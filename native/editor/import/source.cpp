#include <petari/editor/import.hpp>
#define CGLTF_IMPLEMENTATION
#include "vendor/cgltf/cgltf.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include "vendor/meshoptimizer/src/meshoptimizer.h"
#include "vendor/tinyobjloader/tiny_obj_loader.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <memory>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace PetariEditor::Imports {
    namespace {
        constexpr size_t kFileLimit = 64 * 1024 * 1024, kVertexLimit = 1000000;
        Vec3 sub(Vec3 a, Vec3 b) {
            return {a.x - b.x, a.y - b.y, a.z - b.z};
        }
        Vec3 cross(Vec3 a, Vec3 b) {
            return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
        }
        Vec3 normal(Vec3 a) {
            float n = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
            return n > 1e-12f ? Vec3{a.x / n, a.y / n, a.z / n} : Vec3{0, 1, 0};
        }
        Bytes read(const std::filesystem::path& path) {
            std::ifstream in(path, std::ios::binary | std::ios::ate);
            if (!in || in.tellg() < 0 || static_cast< size_t >(in.tellg()) > kFileLimit) {
                throw std::runtime_error("Missing file or file exceeds 64 MiB: " + path.filename().string());
            }
            Bytes bytes(static_cast< size_t >(in.tellg()));
            in.seekg(0);
            if (!bytes.empty() && !in.read(reinterpret_cast< char* >(bytes.data()), bytes.size())) {
                throw std::runtime_error("Could not read " + path.filename().string());
            }
            return bytes;
        }
        std::filesystem::path sibling(const std::filesystem::path& base, const std::string& uri) {
            if (uri.find(":") != std::string::npos) {
                throw std::runtime_error("Textures and buffers must be local files or embedded data.");
            }
            const auto root = std::filesystem::weakly_canonical(base);
            const auto path = std::filesystem::weakly_canonical(base / uri);
            const auto relative = path.lexically_relative(root);
            if (relative.empty() || relative.is_absolute() || *relative.begin() == "..") {
                throw std::runtime_error("Keep external textures and buffers beside the model, or use a packed GLB.");
            }
            return path;
        }
        Bytes uriBytes(const std::filesystem::path& base, const char* uri) {
            if (!uri) {
                return {};
            }
            std::string text(uri);
            if (text.rfind("data:", 0) == 0) {
                const auto comma = text.find(',');
                if (comma == std::string::npos || text.substr(0, comma).find(";base64") == std::string::npos) {
                    throw std::runtime_error("Only base64 data URIs are supported.");
                }
                Bytes out;
                unsigned value = 0, bits = 0;
                const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                for (size_t i = comma + 1; i < text.size(); ++i) {
                    if (text[i] == '=') {
                        break;
                    }
                    auto v = alphabet.find(text[i]);
                    if (v == std::string::npos) {
                        throw std::runtime_error("Invalid base64 image.");
                    }
                    value = (value << 6) | unsigned(v);
                    bits += 6;
                    if (bits >= 8) {
                        bits -= 8;
                        out.push_back((value >> bits) & 255);
                    }
                }
                return out;
            }
            cgltf_decode_uri(text.data());
            text.resize(std::strlen(text.c_str()));
            return read(sibling(base, text));
        }
        void validateMesh(Mesh& mesh) {
            if (mesh.positions.size() > kVertexLimit || mesh.indices.size() > 3 * kVertexLimit) {
                throw std::runtime_error("Model exceeds the one-million-vertex/triangle import limit.");
            }
            for (const auto& p : mesh.positions) {
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
                    throw std::runtime_error("Model contains non-finite positions.");
                }
            }
            for (float uv : mesh.uvs) {
                if (!std::isfinite(uv)) {
                    throw std::runtime_error("Model contains non-finite texture coordinates.");
                }
            }
            for (auto i : mesh.indices) {
                if (i >= mesh.positions.size()) {
                    throw std::runtime_error("Triangle references a missing vertex.");
                }
            }
            if (mesh.normals.size() != mesh.positions.size()) {
                Mesh flat;
                for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                    auto a = mesh.positions[mesh.indices[i]], b = mesh.positions[mesh.indices[i + 1]], c = mesh.positions[mesh.indices[i + 2]];
                    const auto n = normal(cross(sub(b, a), sub(c, a)));
                    for (int j = 0; j < 3; ++j) {
                        auto index = mesh.indices[i + j];
                        flat.indices.push_back(flat.positions.size());
                        flat.positions.push_back(mesh.positions[index]);
                        flat.normals.push_back(n);
                        flat.uvs.push_back(mesh.uvs.size() == mesh.positions.size() * 2 ? mesh.uvs[index * 2] : 0);
                        flat.uvs.push_back(mesh.uvs.size() == mesh.positions.size() * 2 ? mesh.uvs[index * 2 + 1] : 0);
                    }
                }
                mesh = std::move(flat);
            }
            if (mesh.uvs.size() != mesh.positions.size() * 2) {
                mesh.uvs.assign(mesh.positions.size() * 2, 0);
            }
            for (auto& n : mesh.normals) {
                if (!std::isfinite(n.x) || !std::isfinite(n.y) || !std::isfinite(n.z)) {
                    throw std::runtime_error("Model contains non-finite normals.");
                }
                n = normal(n);
            }
        }
        int image(Scene& out, Bytes bytes, const std::string& name) {
            std::string error;
            auto decoded = decodeImage(bytes, &error);
            if (!decoded) {
                out.warnings.push_back(name + ": " + error + " Choose a disc material to replace it.");
                return -1;
            }
            out.images.push_back(std::move(*decoded));
            return int(out.images.size() - 1);
        }
        Scene obj(const std::filesystem::path& path) {
            const auto raw = read(path);
            std::string text(raw.begin(), raw.end()), mtl;
            std::istringstream lines(text);
            std::string line;
            while (std::getline(lines, line)) {
                std::istringstream words(line);
                std::string key, file;
                words >> key;
                if (key == "mtllib") {
                    while (words >> file) {
                        auto b = read(sibling(path.parent_path(), file));
                        mtl.append(b.begin(), b.end());
                        mtl += '\n';
                    }
                }
            }
            tinyobj::ObjReader reader;
            if (!reader.ParseFromString(text, mtl)) {
                throw std::runtime_error(reader.Error());
            }
            Scene out;
            out.name = path.stem().string();
            if (!reader.Warning().empty()) {
                out.warnings.push_back(reader.Warning());
            }
            std::map< std::string, int > images;
            for (const auto& m : reader.GetMaterials()) {
                SourceMaterial mat;
                mat.name = m.name;
                mat.color = {m.diffuse[0], m.diffuse[1], m.diffuse[2], m.dissolve};
                if (!m.diffuse_texname.empty()) {
                    auto it = images.find(m.diffuse_texname);
                    if (it != images.end()) {
                        mat.image = it->second;
                    } else {
                        try {
                            mat.image = image(out, read(sibling(path.parent_path(), m.diffuse_texname)), m.diffuse_texname);
                        } catch (const std::exception& e) {
                            out.warnings.push_back(e.what());
                        }
                        images[m.diffuse_texname] = mat.image;
                    }
                }
                out.materials.push_back(mat);
            }
            const int fallback = out.materials.size();
            out.materials.push_back(SourceMaterial{"Default"});
            const auto& a = reader.GetAttrib();
            if (a.vertices.size() / 3 > kVertexLimit) {
                throw std::runtime_error("OBJ has too many vertices.");
            }
            std::map< int, Mesh > meshes;
            for (const auto& shape : reader.GetShapes()) {
                size_t start = 0;
                for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f) {
                    const auto count = shape.mesh.num_face_vertices[f];
                    if (count != 3) {
                        throw std::runtime_error("OBJ contains a face that could not be triangulated.");
                    }
                    int m = shape.mesh.material_ids[f];
                    if (m < 0 || m >= fallback) {
                        m = fallback;
                    }
                    auto& mesh = meshes[m];
                    for (size_t j = 0; j < count; ++j) {
                        const auto ix = shape.mesh.indices[start + j];
                        if (ix.vertex_index < 0 || size_t(ix.vertex_index) * 3 + 2 >= a.vertices.size()) {
                            throw std::runtime_error("OBJ has an invalid vertex index.");
                        }
                        mesh.indices.push_back(mesh.positions.size());
                        mesh.positions.push_back(
                            {a.vertices[ix.vertex_index * 3], a.vertices[ix.vertex_index * 3 + 1], a.vertices[ix.vertex_index * 3 + 2]});
                        if (ix.normal_index >= 0 && size_t(ix.normal_index) * 3 + 2 < a.normals.size()) {
                            mesh.normals.push_back(
                                {a.normals[ix.normal_index * 3], a.normals[ix.normal_index * 3 + 1], a.normals[ix.normal_index * 3 + 2]});
                        }
                        if (ix.texcoord_index >= 0 && size_t(ix.texcoord_index) * 2 + 1 < a.texcoords.size()) {
                            mesh.uvs.push_back(a.texcoords[ix.texcoord_index * 2]);
                            mesh.uvs.push_back(1 - a.texcoords[ix.texcoord_index * 2 + 1]);
                        } else {
                            mesh.uvs.push_back(0);
                            mesh.uvs.push_back(0);
                        }
                    }
                    start += count;
                }
            }
            for (auto& [m, mesh] : meshes) {
                validateMesh(mesh);
                out.primitives.push_back({std::move(mesh), m});
            }
            return out;
        }
        cgltf_result readBuffer(const cgltf_memory_options*, const cgltf_file_options* opts, const char* path, cgltf_size* size, void** data) {
            try {
                const auto& base = *static_cast< const std::filesystem::path* >(opts->user_data);
                const auto p = std::filesystem::path(path);
                auto bytes = read(sibling(base, p.lexically_relative(base).string()));
                if (*size && bytes.size() < *size) {
                    return cgltf_result_data_too_short;
                }
                *size = bytes.size();
                *data = std::malloc(bytes.size());
                if (!*data) {
                    return cgltf_result_out_of_memory;
                }
                std::memcpy(*data, bytes.data(), bytes.size());
                return cgltf_result_success;
            } catch (...) {
                return cgltf_result_io_error;
            }
        }
        void releaseBuffer(const cgltf_memory_options*, const cgltf_file_options*, void* data) {
            std::free(data);
        }
        Scene gltf(const std::filesystem::path& path) {
            const auto raw = read(path);
            cgltf_options opts{};
            auto base = path.parent_path();
            opts.file = {readBuffer, releaseBuffer, &base};
            cgltf_data* parsed = nullptr;
            if (cgltf_parse(&opts, raw.data(), raw.size(), &parsed) != cgltf_result_success) {
                throw std::runtime_error("Could not parse glTF 2.0/GLB.");
            }
            std::unique_ptr< cgltf_data, decltype(&cgltf_free) > data(parsed, &cgltf_free);
            size_t total = 0;
            for (size_t i = 0; i < data->buffers_count; ++i) {
                if (data->buffers[i].size > kFileLimit || total > 4 * kFileLimit - data->buffers[i].size) {
                    throw std::runtime_error("glTF buffers exceed 256 MiB.");
                }
                total += data->buffers[i].size;
            }
            if (data->nodes_count > 10000 || data->materials_count > 1024 || data->images_count > 256) {
                throw std::runtime_error("glTF has too many nodes, materials or textures.");
            }
            if (cgltf_load_buffers(&opts, data.get(), path.c_str()) != cgltf_result_success || cgltf_validate(data.get()) != cgltf_result_success) {
                throw std::runtime_error("glTF buffer/accessor data is invalid or missing.");
            }
            for (size_t i = 0; i < data->buffer_views_count; ++i) {
                auto& view = data->buffer_views[i];
                if (!view.has_meshopt_compression) {
                    continue;
                }
                const auto& m = view.meshopt_compression;
                if (!m.buffer || !m.buffer->data || m.offset > m.buffer->size || m.size > m.buffer->size - m.offset || !m.stride || m.stride > 256 ||
                    m.count > kVertexLimit || m.count * m.stride != view.size || view.size > kFileLimit) {
                    throw std::runtime_error("Invalid meshopt-compressed buffer.");
                }
                view.data = std::malloc(view.size);
                if (!view.data) {
                    throw std::bad_alloc();
                }
                const auto* src = static_cast< const unsigned char* >(m.buffer->data) + m.offset;
                int result = -1;
                if (m.mode == cgltf_meshopt_compression_mode_attributes && m.stride % 4 == 0) {
                    result = meshopt_decodeVertexBuffer(view.data, m.count, m.stride, src, m.size);
                } else if ((m.stride == 2 || m.stride == 4) && m.mode == cgltf_meshopt_compression_mode_triangles && m.count % 3 == 0) {
                    result = meshopt_decodeIndexBuffer(view.data, m.count, m.stride, src, m.size);
                } else if ((m.stride == 2 || m.stride == 4) && m.mode == cgltf_meshopt_compression_mode_indices) {
                    result = meshopt_decodeIndexSequence(view.data, m.count, m.stride, src, m.size);
                }
                if (result) {
                    throw std::runtime_error("Could not decode meshopt-compressed geometry.");
                }
                if (m.filter == cgltf_meshopt_compression_filter_octahedral && (m.stride == 4 || m.stride == 8)) {
                    meshopt_decodeFilterOct(view.data, m.count, m.stride);
                } else if (m.filter == cgltf_meshopt_compression_filter_quaternion && m.stride == 8) {
                    meshopt_decodeFilterQuat(view.data, m.count, m.stride);
                } else if (m.filter == cgltf_meshopt_compression_filter_exponential && m.stride % 4 == 0) {
                    meshopt_decodeFilterExp(view.data, m.count, m.stride);
                } else if (m.filter != cgltf_meshopt_compression_filter_none) {
                    throw std::runtime_error("Invalid meshopt vertex filter.");
                }
            }
            Scene out;
            out.name = path.stem().string();
            if (data->animations_count || data->skins_count) {
                out.warnings.push_back("Animations and skins are imported as static rest geometry.");
            }
            for (size_t i = 0; i < data->images_count; ++i) {
                const auto& im = data->images[i];
                Bytes bytes;
                try {
                    if (im.buffer_view) {
                        const auto* b = cgltf_buffer_view_data(im.buffer_view);
                        if (b) {
                            bytes.assign(b, b + im.buffer_view->size);
                        }
                    } else {
                        bytes = uriBytes(base, im.uri);
                    }
                    const int got = image(out, std::move(bytes), im.name ? im.name : "Texture");
                    if (got < 0) {
                        out.images.push_back({});
                    }
                } catch (const std::exception& e) {
                    out.warnings.push_back(e.what());
                    out.images.push_back({});
                }
            }
            for (size_t i = 0; i < data->materials_count; ++i) {
                const auto& m = data->materials[i];
                SourceMaterial mat;
                mat.name = m.name ? m.name : "Material " + std::to_string(i + 1);
                mat.doubleSided = m.double_sided;
                const cgltf_texture* tex = nullptr;
                if (m.has_pbr_metallic_roughness) {
                    std::copy_n(m.pbr_metallic_roughness.base_color_factor, 4, mat.color.begin());
                    tex = m.pbr_metallic_roughness.base_color_texture.texture;
                }
                if (tex && tex->image) {
                    mat.image = int(tex->image - data->images);
                }
                if (m.alpha_mode == cgltf_alpha_mode_opaque) {
                    mat.color[3] = 1;
                }
                out.materials.push_back(mat);
            }
            const int fallback = out.materials.size();
            out.materials.push_back(SourceMaterial{"Default"});
            for (size_t ni = 0; ni < data->nodes_count; ++ni) {
                const auto& node = data->nodes[ni];
                if (!node.mesh) {
                    continue;
                }
                if (data->scene) {
                    const cgltf_node* root = &node;
                    size_t depth = 0;
                    while (root->parent && ++depth <= data->nodes_count) {
                        root = root->parent;
                    }
                    if (depth > data->nodes_count) {
                        throw std::runtime_error("glTF has a cyclic node hierarchy.");
                    }
                    if (std::find(data->scene->nodes, data->scene->nodes + data->scene->nodes_count, root) ==
                        data->scene->nodes + data->scene->nodes_count) {
                        continue;
                    }
                }
                float t[16];
                cgltf_node_transform_world(&node, t);
                const Vec3 x{t[0], t[1], t[2]}, y{t[4], t[5], t[6]}, z{t[8], t[9], t[10]};
                auto nx = cross(y, z), ny = cross(z, x), nz = cross(x, y);
                const float det = x.x * nx.x + x.y * nx.y + x.z * nx.z;
                if (!std::isfinite(det) || std::fabs(det) < 1e-12f) {
                    throw std::runtime_error("glTF node has a singular transform.");
                }
                for (size_t pi = 0; pi < node.mesh->primitives_count; ++pi) {
                    const auto& p = node.mesh->primitives[pi];
                    if (p.has_draco_mesh_compression) {
                        throw std::runtime_error("Draco-compressed geometry is unsupported; export an uncompressed GLB.");
                    }
                    if (p.type != cgltf_primitive_type_triangles && p.type != cgltf_primitive_type_triangle_strip &&
                        p.type != cgltf_primitive_type_triangle_fan) {
                        out.warnings.push_back("Point/line primitives were skipped.");
                        continue;
                    }
                    const cgltf_texture_view* texture =
                        p.material && p.material->has_pbr_metallic_roughness ? &p.material->pbr_metallic_roughness.base_color_texture : nullptr;
                    const int uvSet =
                        texture ? (texture->has_transform && texture->transform.has_texcoord ? texture->transform.texcoord : texture->texcoord) : 0;
                    const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr;
                    for (size_t a = 0; a < p.attributes_count; ++a) {
                        const auto& attr = p.attributes[a];
                        if (attr.type == cgltf_attribute_type_position) {
                            pos = attr.data;
                        }
                        if (attr.type == cgltf_attribute_type_normal) {
                            nrm = attr.data;
                        }
                        if (attr.type == cgltf_attribute_type_texcoord && attr.index == uvSet) {
                            uv = attr.data;
                        }
                    }
                    if (!pos || pos->type != cgltf_type_vec3 || pos->count > kVertexLimit) {
                        throw std::runtime_error("glTF primitive needs valid positions (at most one million vertices).");
                    }
                    Primitive prim;
                    prim.material = p.material ? int(p.material - data->materials) : fallback;
                    auto& mesh = prim.mesh;
                    auto unpack = [&](const cgltf_accessor* acc, size_t n) {
                        std::vector< float > v;
                        if (acc) {
                            if (acc->count != pos->count || cgltf_num_components(acc->type) != n) {
                                throw std::runtime_error("glTF attribute lengths do not match.");
                            }
                            v.resize(acc->count * n);
                            if (cgltf_accessor_unpack_floats(acc, v.data(), v.size()) != v.size()) {
                                throw std::runtime_error("Could not unpack glTF attribute.");
                            }
                        }
                        return v;
                    };
                    const auto positions = unpack(pos, 3), normals = unpack(nrm, 3), uvs = unpack(uv, 2);
                    for (size_t v = 0; v < pos->count; ++v) {
                        const float a = positions[v * 3], b = positions[v * 3 + 1], c = positions[v * 3 + 2];
                        mesh.positions.push_back({t[0] * a + t[4] * b + t[8] * c + t[12], t[1] * a + t[5] * b + t[9] * c + t[13],
                                                  t[2] * a + t[6] * b + t[10] * c + t[14]});
                        if (nrm) {
                            const float a = normals[v * 3] / det, b = normals[v * 3 + 1] / det, c = normals[v * 3 + 2] / det;
                            mesh.normals.push_back(
                                normal({nx.x * a + ny.x * b + nz.x * c, nx.y * a + ny.y * b + nz.y * c, nx.z * a + ny.z * b + nz.z * c}));
                        }
                        float u = uv ? uvs[v * 2] : 0, vv = uv ? uvs[v * 2 + 1] : 0;
                        if (texture && texture->has_transform) {
                            const auto& tr = texture->transform;
                            const float x = u * tr.scale[0], y = vv * tr.scale[1];
                            u = tr.offset[0] + std::cos(tr.rotation) * x - std::sin(tr.rotation) * y;
                            vv = tr.offset[1] + std::sin(tr.rotation) * x + std::cos(tr.rotation) * y;
                        }
                        mesh.uvs.push_back(u);
                        mesh.uvs.push_back(vv);
                    }
                    std::vector< unsigned > idx;
                    if (p.indices) {
                        if (p.indices->count > 3 * kVertexLimit) {
                            throw std::runtime_error("Too many glTF indices.");
                        }
                        idx.resize(p.indices->count);
                        if (cgltf_accessor_unpack_indices(p.indices, idx.data(), sizeof(unsigned), idx.size()) != idx.size()) {
                            throw std::runtime_error("Invalid glTF triangle indices.");
                        }
                    } else {
                        idx.resize(pos->count);
                        std::iota(idx.begin(), idx.end(), 0);
                    }
                    if (p.type == cgltf_primitive_type_triangles) {
                        if (idx.size() % 3) {
                            throw std::runtime_error("Incomplete glTF triangle.");
                        }
                        mesh.indices = std::move(idx);
                    } else {
                        for (size_t i = 2; i < idx.size(); ++i) {
                            unsigned a = p.type == cgltf_primitive_type_triangle_fan ? idx[0] : idx[i - 2], b = idx[i - 1];
                            if (p.type == cgltf_primitive_type_triangle_strip && (i & 1)) {
                                std::swap(a, b);
                            }
                            mesh.indices.insert(mesh.indices.end(), {a, b, idx[i]});
                        }
                    }
                    if (det < 0) {
                        for (size_t i = 0; i < mesh.indices.size(); i += 3) {
                            std::swap(mesh.indices[i + 1], mesh.indices[i + 2]);
                        }
                    }
                    if (p.targets_count) {
                        out.warnings.push_back("Morph targets are imported in their base shape.");
                    }
                    validateMesh(mesh);
                    out.primitives.push_back(std::move(prim));
                }
            }
            return out;
        }
    }  // namespace
    std::optional< Scene > load(const std::filesystem::path& input, std::string* error) {
        try {
            const auto path = std::filesystem::absolute(input).lexically_normal();
            auto ext = path.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
            Scene scene;
            if (ext == ".obj") {
                scene = obj(path);
            } else if (ext == ".gltf" || ext == ".glb") {
                scene = gltf(path);
            } else {
                throw std::runtime_error("Choose a .obj, .gltf or .glb model.");
            }
            size_t count = 0;
            for (const auto& p : scene.primitives) {
                count += p.mesh.triangleCount();
            }
            if (!count) {
                throw std::runtime_error("Model has no triangles.");
            }
            if (count > kVertexLimit) {
                throw std::runtime_error("Model exceeds one million triangles.");
            }
            std::vector< SourceMaterial > used;
            std::map< int, int > materialMap;
            for (auto& p : scene.primitives) {
                auto [it, added] = materialMap.emplace(p.material, used.size());
                if (added) {
                    used.push_back(scene.materials[p.material]);
                }
                p.material = it->second;
            }
            scene.materials = std::move(used);
            return scene;
        } catch (const std::exception& e) {
            *error = e.what();
            return {};
        }
    }
}  // namespace PetariEditor::Imports
