#include "binary.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <petari/editor/terrain/brush.hpp>

namespace PetariEditor::Imports {
    namespace {
        using namespace Binary;
        void text(Bytes& out, const std::string& s) {
            u32(out, s.size());
            out.insert(out.end(), s.begin(), s.end());
        }
        void blob(Bytes& out, const Bytes& bytes) {
            u32(out, bytes.size());
            out.insert(out.end(), bytes.begin(), bytes.end());
        }
        void vectors(Bytes& out, const std::vector< Vec3 >& vs) {
            u32(out, vs.size());
            for (auto v : vs) {
                f32(out, v.x);
                f32(out, v.y);
                f32(out, v.z);
            }
        }
        void mesh(Bytes& out, const Mesh& m) {
            vectors(out, m.positions);
            vectors(out, m.normals);
            u32(out, m.uvs.size());
            for (float f : m.uvs) {
                f32(out, f);
            }
            u32(out, m.indices.size());
            for (auto i : m.indices) {
                u32(out, i);
            }
            u32(out, m.surfaces.size());
            for (auto s : m.surfaces) {
                u32(out, s.floor);
                u32(out, s.wall);
                u32(out, s.sound);
                u32(out, s.cameraThrough);
                u32(out, s.cameraId);
            }
            u32(out, m.triangleSurface.size());
            for (auto s : m.triangleSurface) {
                u32(out, s);
            }
        }
        struct Reader {
            const Bytes& bytes;
            size_t at = 8;
            void need(size_t n) {
                if (n > bytes.size() - at) {
                    throw std::runtime_error("Truncated imported asset.");
                }
            }
            unsigned integer() {
                need(4);
                unsigned n = (unsigned(bytes[at]) << 24) | (unsigned(bytes[at + 1]) << 16) | (unsigned(bytes[at + 2]) << 8) | bytes[at + 3];
                at += 4;
                return n;
            }
            float real() {
                unsigned n = integer();
                float f;
                std::memcpy(&f, &n, 4);
                if (!std::isfinite(f)) {
                    throw std::runtime_error("Imported asset has a non-finite number.");
                }
                return f;
            }
            size_t count(size_t max, size_t stride) {
                auto n = integer();
                if (n > max || n > (bytes.size() - at) / stride) {
                    throw std::runtime_error("Invalid imported asset count.");
                }
                return n;
            }
            std::string string() {
                const auto n = count(4096, 1);
                std::string s(bytes.begin() + at, bytes.begin() + at + n);
                at += n;
                return s;
            }
            Bytes data() {
                const auto n = count(16 * 1024 * 1024, 1);
                Bytes b(bytes.begin() + at, bytes.begin() + at + n);
                at += n;
                return b;
            }
            std::vector< Vec3 > vectors() {
                const auto n = count(65535, 12);
                std::vector< Vec3 > out;
                out.reserve(n);
                for (size_t i = 0; i < n; ++i) {
                    out.push_back({real(), real(), real()});
                }
                return out;
            }
            Mesh mesh() {
                Mesh m;
                m.positions = vectors();
                m.normals = vectors();
                auto n = count(131070, 4);
                m.uvs.reserve(n);
                for (size_t i = 0; i < n; ++i) {
                    m.uvs.push_back(real());
                }
                n = count(48000, 4);
                m.indices.reserve(n);
                for (size_t i = 0; i < n; ++i) {
                    auto index = integer();
                    if (index >= m.positions.size()) {
                        throw std::runtime_error("Imported asset index is out of range.");
                    }
                    m.indices.push_back(index);
                }
                if (n % 3 || (!m.normals.empty() && m.normals.size() != m.positions.size()) ||
                    (!m.uvs.empty() && m.uvs.size() != m.positions.size() * 2)) {
                    throw std::runtime_error("Imported mesh attribute lengths differ.");
                }
                n = count(256, 20);
                for (size_t i = 0; i < n; ++i) {
                    Terrain::Surface s;
                    s.floor = integer();
                    s.wall = integer();
                    s.sound = integer();
                    s.cameraThrough = integer();
                    s.cameraId = integer();
                    if (s.floor < 0 || s.floor >= int(Terrain::floorCodeNames().size()) || s.wall < 0 ||
                        s.wall >= int(Terrain::wallCodeNames().size()) || s.sound < 0 || s.sound >= int(Terrain::soundCodeNames().size()) ||
                        s.cameraThrough < 0 || s.cameraThrough > 1 || s.cameraId < 0 || s.cameraId > 255) {
                        throw std::runtime_error("Invalid imported collision surface.");
                    }
                    m.surfaces.push_back(s);
                }
                n = count(16000, 4);
                for (size_t i = 0; i < n; ++i) {
                    auto s = integer();
                    if (s >= m.surfaces.size()) {
                        throw std::runtime_error("Imported surface index is out of range.");
                    }
                    m.triangleSurface.push_back(s);
                }
                if (n && n != m.triangleCount()) {
                    throw std::runtime_error("Imported triangle surfaces are incomplete.");
                }
                return m;
            }
        };
    }  // namespace
    Bytes serialize(const Asset& asset) {
        Bytes out{'P', 'M', 'D', 'L', 0, 0, 0, 1};
        text(out, asset.name);
        u32(out, asset.gravity ? 1 : 0);
        u32(out, asset.materials.size());
        for (const auto& m : asset.materials) {
            text(out, m.name);
            for (float c : m.color) {
                f32(out, c);
            }
            u32(out, unsigned(m.donor));
            u32(out, m.doubleSided);
            u32(out, unsigned(m.texture.format));
            u32(out, m.texture.width);
            u32(out, m.texture.height);
            blob(out, m.texture.data);
        }
        u32(out, asset.parts.size());
        for (const auto& p : asset.parts) {
            u32(out, p.material);
            mesh(out, p.mesh);
            mesh(out, p.collision);
            mesh(out, p.middle);
            mesh(out, p.low);
        }
        u32(out, asset.warnings.size());
        for (const auto& s : asset.warnings) {
            text(out, s);
        }
        return out;
    }
    std::optional< Asset > deserialize(const Bytes& bytes, std::string* error) {
        try {
            if (bytes.size() < 8 || bytes.size() > 256 * 1024 * 1024 || std::memcmp(bytes.data(), "PMDL\0\0\0\1", 8)) {
                throw std::runtime_error("Unsupported imported asset version.");
            }
            Reader r{bytes};
            Asset out;
            out.name = r.string();
            out.gravity = r.integer() != 0;
            auto n = r.count(1024, 4);
            for (size_t i = 0; i < n; ++i) {
                Material m;
                m.name = r.string();
                for (float& c : m.color) {
                    c = r.real();
                    if (c < 0 || c > 1) {
                        throw std::runtime_error("Invalid imported material colour.");
                    }
                }
                const auto donor = r.integer();
                m.donor = donor == 0xffffffff ? -1 : int(donor);
                if (m.donor < -1 || m.donor >= int(Terrain::materialPalette().size())) {
                    throw std::runtime_error("Invalid imported donor index.");
                }
                m.doubleSided = r.integer() != 0;
                m.texture.format = TextureFormat(r.integer());
                m.texture.width = r.integer();
                m.texture.height = r.integer();
                m.texture.data = r.data();
                if (m.donor < 0) {
                    const int w = m.texture.width, h = m.texture.height;
                    size_t expected = 0;
                    if (w < 1 || h < 1 || w > 1024 || h > 1024) {
                        throw std::runtime_error("Imported texture dimensions are invalid.");
                    }
                    if (m.texture.format == TextureFormat::CMPR) {
                        expected = size_t((w + 7) / 8) * ((h + 7) / 8) * 32;
                    } else if (m.texture.format == TextureFormat::RGB5A3) {
                        expected = size_t((w + 3) / 4) * ((h + 3) / 4) * 32;
                    } else if (m.texture.format == TextureFormat::I8) {
                        expected = size_t((w + 7) / 8) * ((h + 3) / 4) * 32;
                    }
                    if (!expected || m.texture.data.size() != expected) {
                        throw std::runtime_error("Imported texture data size is invalid.");
                    }
                }
                out.materials.push_back(std::move(m));
            }
            n = r.count(64, 4);
            if (!n || out.materials.empty()) {
                throw std::runtime_error("Imported asset is empty.");
            }
            for (size_t i = 0; i < n; ++i) {
                Part p;
                p.material = r.integer();
                if (p.material < 0 || p.material >= int(out.materials.size())) {
                    throw std::runtime_error("Imported part has an invalid material.");
                }
                p.mesh = r.mesh();
                p.collision = r.mesh();
                p.middle = r.mesh();
                p.low = r.mesh();
                if (p.mesh.indices.empty() && p.collision.indices.empty()) {
                    throw std::runtime_error("Imported part has no geometry.");
                }
                out.parts.push_back(std::move(p));
            }
            n = r.count(4096, 4);
            for (size_t i = 0; i < n; ++i) {
                out.warnings.push_back(r.string());
            }
            if (r.at != bytes.size()) {
                throw std::runtime_error("Imported asset has trailing data.");
            }
            return out;
        } catch (const std::exception& e) {
            *error = e.what();
            return {};
        }
    }
    std::string contentId(const Bytes& bytes) {
        std::uint64_t a = 14695981039346656037ull, b = 7809847782465536322ull;
        for (auto c : bytes) {
            a = (a ^ c) * 1099511628211ull;
            b = (b ^ (c + 1u)) * 14029467366897019727ull;
        }
        char id[33];
        std::snprintf(id, sizeof(id), "%016llx%016llx", static_cast< unsigned long long >(a), static_cast< unsigned long long >(b));
        return id;
    }
    std::string className(const std::string& id, int part) {
        return "EditorModel_" + id + "_" + std::to_string(part);
    }
    bool parseClassName(const char* raw, std::string* id, int* part) {
        if (!raw) {
            return false;
        }
        const std::string name(raw), prefix = "EditorModel_";
        if (name.rfind(prefix, 0) != 0 || name.size() < prefix.size() + 34) {
            return false;
        }
        const auto key = name.substr(prefix.size(), 32);
        if (key.find_first_not_of("0123456789abcdef") != std::string::npos || name[prefix.size() + 32] != '_') {
            return false;
        }
        const auto number = name.substr(prefix.size() + 33);
        if (number.empty() || number.size() > 2 || number.find_first_not_of("0123456789") != std::string::npos) {
            return false;
        }
        const int n = std::stoi(number);
        if (n >= 64 || std::to_string(n) != number) {
            return false;
        }
        if (id) {
            *id = key;
        }
        if (part) {
            *part = n;
        }
        return true;
    }
}  // namespace PetariEditor::Imports
