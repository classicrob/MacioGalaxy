// J3D model writer for terrain. See petari/editor/terrain/model_writer.hpp.
#include <petari/editor/terrain/model_writer.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace PetariEditor::Terrain {

using Formats::Bytes;

namespace {
    std::uint32_t get32(const Bytes& b, std::size_t at) {
        return static_cast< std::uint32_t >(b[at]) << 24 | static_cast< std::uint32_t >(b[at + 1]) << 16 |
               static_cast< std::uint32_t >(b[at + 2]) << 8 | b[at + 3];
    }
    std::uint16_t get16(const Bytes& b, std::size_t at) { return static_cast< std::uint16_t >(b[at] << 8 | b[at + 1]); }
    void put32(Bytes& b, std::size_t at, std::uint32_t v) {
        b[at] = static_cast< std::uint8_t >(v >> 24);
        b[at + 1] = static_cast< std::uint8_t >(v >> 16);
        b[at + 2] = static_cast< std::uint8_t >(v >> 8);
        b[at + 3] = static_cast< std::uint8_t >(v);
    }
    void put16(Bytes& b, std::size_t at, std::uint16_t v) {
        b[at] = static_cast< std::uint8_t >(v >> 8);
        b[at + 1] = static_cast< std::uint8_t >(v);
    }
    void putF(Bytes& b, std::size_t at, float f) {
        std::uint32_t bits;
        std::memcpy(&bits, &f, 4);
        put32(b, at, bits);
    }
    void append32(Bytes& b, std::uint32_t v) {
        b.resize(b.size() + 4);
        put32(b, b.size() - 4, v);
    }
    void append16(Bytes& b, std::uint16_t v) {
        b.resize(b.size() + 2);
        put16(b, b.size() - 2, v);
    }
    void appendF(Bytes& b, float f) {
        b.resize(b.size() + 4);
        putF(b, b.size() - 4, f);
    }
    void align(Bytes& b, std::size_t to, std::uint8_t fill = 0) {
        while (b.size() % to != 0) {
            b.push_back(fill);
        }
    }

    // A block: header of `headerSize` (type, size, count...), then tables placed with their
    // offsets written into header fields.
    struct Block {
        Bytes data;
        Block(const char* pType, std::size_t headerSize) : data(headerSize, 0) { std::memcpy(data.data(), pType, 4); }
        std::size_t place(const Bytes& table, std::size_t field, std::size_t alignment = 4) {
            align(data, alignment);
            const std::size_t offset = data.size();
            data.insert(data.end(), table.begin(), table.end());
            put32(data, field, static_cast< std::uint32_t >(offset));
            return offset;
        }
        Bytes finish() {
            align(data, 0x20);
            put32(data, 4, static_cast< std::uint32_t >(data.size()));
            return data;
        }
    };

    // ResNTAB: count, 0xFFFF, then (hash, offset) per name and the strings.
    Bytes nameTable(const std::vector< std::string >& names) {
        Bytes data(4 + names.size() * 4);
        put16(data, 0, static_cast< std::uint16_t >(names.size()));
        put16(data, 2, 0xFFFF);
        for (std::size_t i = 0; i < names.size(); i++) {
            std::uint16_t hash = 0;
            for (char c : names[i]) {
                hash = static_cast< std::uint16_t >(hash * 3 + static_cast< std::uint8_t >(c));
            }
            put16(data, 4 + i * 4, hash);
            put16(data, 6 + i * 4, static_cast< std::uint16_t >(data.size()));
            data.insert(data.end(), names[i].begin(), names[i].end());
            data.push_back(0);
        }
        return data;
    }

    std::vector< std::string > readNames(const Bytes& b, std::size_t table, std::size_t limit) {
        std::vector< std::string > names;
        if (table + 4 > limit) {
            return names;
        }
        const std::uint16_t count = get16(b, table);
        for (std::uint16_t i = 0; i < count && table + 8 + i * 4 <= limit; i++) {
            std::size_t at = table + get16(b, table + 6 + i * 4);
            std::string name;
            while (at < limit && b[at] != 0) {
                name.push_back(static_cast< char >(b[at++]));
            }
            names.push_back(name);
        }
        return names;
    }

    // Finds a block in a big-endian J3D2 file: its start and size, or false.
    bool findBlock(const Bytes& file, const char* pType, std::size_t* pStart, std::size_t* pSize) {
        if (file.size() < 0x20 || std::memcmp(file.data(), "J3D2", 4) != 0) {
            return false;
        }
        const std::uint32_t count = get32(file, 0x0C);
        std::size_t at = 0x20;
        for (std::uint32_t i = 0; i < count && at + 8 <= file.size(); i++) {
            const std::uint32_t size = get32(file, at + 4);
            if (size < 8 || at + size > file.size()) {
                return false;
            }
            if (std::memcmp(&file[at], pType, 4) == 0) {
                *pStart = at;
                *pSize = size;
                return true;
            }
            at += size;
        }
        return false;
    }

    const std::size_t kMat3Header = 0x84;
    const std::size_t kMaterialInitSize = 0x14C;
    const std::size_t kIndInitSize = 0x138;
}  // namespace

std::vector< std::string > modelMaterialNames(const Bytes& model) {
    std::size_t start, size;
    if (!findBlock(model, "MAT3", &start, &size) || size < kMat3Header) {
        return {};
    }
    return readNames(model, start + get32(model, start + 0x14), start + size);
}

std::optional< MaterialClone > cloneMaterial(const Bytes& donor, const std::string& materialName, std::string* pError) {
    std::size_t start, size;
    if (!findBlock(donor, "MAT3", &start, &size) || size < kMat3Header) {
        *pError = "the donor is not a J3D2 model with materials";
        return std::nullopt;
    }
    const Bytes block(donor.begin() + static_cast< std::ptrdiff_t >(start), donor.begin() + static_cast< std::ptrdiff_t >(start + size));
    const std::vector< std::string > names = readNames(block, get32(block, 0x14), block.size());
    const auto it = std::find(names.begin(), names.end(), materialName);
    if (it == names.end()) {
        *pError = "the donor has no material \"" + materialName + "\"";
        return std::nullopt;
    }
    const std::size_t material = static_cast< std::size_t >(it - names.begin());
    const std::uint16_t initIndex = get16(block, get32(block, 0x10) + material * 2);

    // The tables (offset fields 0x0C..0x80) as regions up to the next table, as the native
    // converter (native/resource/j3d_model.cpp) and the loader see them.
    std::vector< std::uint32_t > offsets;
    for (std::size_t field = 0x0C; field < kMat3Header; field += 4) {
        const std::uint32_t offset = get32(block, field);
        if (offset != 0) {
            if (offset >= block.size()) {
                *pError = "a MAT3 table is outside its block";
                return std::nullopt;
            }
            offsets.push_back(offset);
        }
    }
    std::sort(offsets.begin(), offsets.end());
    offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
    auto regionEnd = [&](std::uint32_t offset) {
        const auto next = std::upper_bound(offsets.begin(), offsets.end(), offset);
        return next != offsets.end() ? *next : static_cast< std::uint32_t >(block.size());
    };
    const std::uint32_t initOffset = get32(block, 0x0C), nameOffset = get32(block, 0x14), indOffset = get32(block, 0x18);
    // J3DMaterialFactory's test: the indirect table exists when it is more than 4 bytes past the
    // name table.
    const bool hasIndirect = indOffset != 0 && indOffset - nameOffset > 4;
    if (initOffset + (initIndex + 1) * kMaterialInitSize > regionEnd(initOffset) ||
        (hasIndirect && indOffset + (material + 1) * kIndInitSize > regionEnd(indOffset))) {
        *pError = "the material's entries are outside their tables";
        return std::nullopt;
    }

    Block out("MAT3", kMat3Header);
    put16(out.data, 0x08, 1);
    put16(out.data, 0x0A, 0xFFFF);
    // Fields in the donor's table order, so the regions keep their order and extents.
    std::vector< std::size_t > fields;
    for (std::size_t field = 0x0C; field < kMat3Header; field += 4) {
        if (get32(block, field) != 0) {
            fields.push_back(field);
        }
    }
    std::stable_sort(fields.begin(), fields.end(), [&](std::size_t a, std::size_t b) { return get32(block, a) < get32(block, b); });
    std::map< std::uint32_t, std::uint32_t > placed;  // donor offset -> new offset (tables sharing a region)
    for (std::size_t field : fields) {
        const std::uint32_t offset = get32(block, field);
        if (field == 0x18 && !hasIndirect) {
            continue;  // left 0: no indirect data
        }
        if (field != 0x0C && field != 0x10 && field != 0x14 && field != 0x18) {
            auto shared = placed.find(offset);
            if (shared != placed.end()) {
                put32(out.data, field, shared->second);
                continue;
            }
        }
        Bytes table;
        switch (field) {
        case 0x0C:  // the material's init entry only
            table.assign(block.begin() + initOffset + initIndex * kMaterialInitSize, block.begin() + initOffset + (initIndex + 1) * kMaterialInitSize);
            break;
        case 0x10:  // material 0 -> init entry 0
            table = {0, 0, 0, 0};
            break;
        case 0x14:
            table = nameTable({materialName});
            break;
        case 0x18:  // indirect data is per material, not per init entry
            table.assign(block.begin() + indOffset + material * kIndInitSize, block.begin() + indOffset + (material + 1) * kIndInitSize);
            break;
        default:  // a shared pool, whole, so the init entry's indices stay valid
            table.assign(block.begin() + offset, block.begin() + regionEnd(offset));
            break;
        }
        placed[offset] = static_cast< std::uint32_t >(out.place(table, field));
    }

    MaterialClone clone;
    clone.name = materialName;
    clone.mat3 = out.finish();
    std::size_t texStart, texSize;
    if (findBlock(donor, "TEX1", &texStart, &texSize)) {
        clone.tex1.assign(donor.begin() + static_cast< std::ptrdiff_t >(texStart), donor.begin() + static_cast< std::ptrdiff_t >(texStart + texSize));
    } else {
        Block empty("TEX1", 0x14);
        clone.tex1 = empty.finish();
    }
    return clone;
}

ModelResult buildModel(const Mesh& mesh, const MaterialClone& material, const std::string& jointName) {
    ModelResult result;
    const std::size_t vertexCount = mesh.positions.size();
    if (mesh.triangleCount() == 0 || mesh.indices.size() % 3 != 0) {
        result.error = "the mesh has no triangles";
        return result;
    }
    if (vertexCount > 0xFFFF) {
        result.error = "the mesh has more than 65535 vertices: split it into several models";
        return result;
    }
    if (mesh.normals.size() != vertexCount || mesh.uvs.size() != vertexCount * 2) {
        result.error = "the mesh needs a normal and a UV per vertex";
        return result;
    }
    for (std::uint32_t index : mesh.indices) {
        if (index >= vertexCount) {
            result.error = "a triangle index is past the positions";
            return result;
        }
    }
    Vec3 lo, hi;
    mesh.bounds(&lo, &hi);
    float radius = 0.0f;
    for (const Vec3& p : mesh.positions) {
        radius = std::max(radius, std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z));
    }

    std::vector< Bytes > blocks;
    {  // INF1: the hierarchy joint 0 { material 0, shape 0 }.
        Block inf("INF1", 0x18);
        put16(inf.data, 0x08, 0);  // matrix calc: basic
        put32(inf.data, 0x0C, 1);  // packets
        put32(inf.data, 0x10, static_cast< std::uint32_t >(vertexCount));
        Bytes hierarchy;
        for (std::uint16_t v : {0x10, 0, 0x01, 0, 0x11, 0, 0x12, 0, 0x02, 0, 0x00, 0}) {
            append16(hierarchy, v);
        }
        inf.place(hierarchy, 0x14);
        blocks.push_back(inf.finish());
    }
    {  // VTX1
        Block vtx("VTX1", 0x40);
        Bytes formats;
        // {attribute, component count, component type, fraction}: POS xyz f32, NRM xyz f32,
        // CLR0 rgba8, TEX0 st f32, end.
        const std::uint32_t list[][4] = {{9, 1, 4, 0}, {10, 0, 4, 0}, {11, 1, 5, 0}, {13, 1, 4, 0}, {0xFF, 1, 0, 0}};
        for (const auto& f : list) {
            append32(formats, f[0]);
            append32(formats, f[1]);
            append32(formats, f[2]);
            formats.push_back(static_cast< std::uint8_t >(f[3]));
            formats.insert(formats.end(), {0xFF, 0xFF, 0xFF});
        }
        vtx.place(formats, 0x08);
        Bytes positions, normals, uvs;
        for (std::size_t i = 0; i < vertexCount; i++) {
            appendF(positions, mesh.positions[i].x);
            appendF(positions, mesh.positions[i].y);
            appendF(positions, mesh.positions[i].z);
            appendF(normals, mesh.normals[i].x);
            appendF(normals, mesh.normals[i].y);
            appendF(normals, mesh.normals[i].z);
            appendF(uvs, mesh.uvs[i * 2]);
            appendF(uvs, mesh.uvs[i * 2 + 1]);
        }
        vtx.place(positions, 0x0C, 0x20);
        vtx.place(normals, 0x10, 0x20);
        vtx.place(Bytes{0xFF, 0xFF, 0xFF, 0xFF}, 0x18, 0x20);
        vtx.place(uvs, 0x20, 0x20);
        blocks.push_back(vtx.finish());
    }
    {  // EVP1: no weighted matrices.
        Block evp("EVP1", 0x1C);
        put16(evp.data, 0x0A, 0xFFFF);
        blocks.push_back(evp.finish());
    }
    {  // DRW1: draw matrix 0 is joint 0.
        Block drw("DRW1", 0x14);
        put16(drw.data, 0x08, 1);
        put16(drw.data, 0x0A, 0xFFFF);
        drw.place(Bytes{0}, 0x0C);
        drw.place(Bytes{0, 0}, 0x10, 2);
        blocks.push_back(drw.finish());
    }
    {  // JNT1: one identity joint with the mesh's bounds.
        Block jnt("JNT1", 0x18);
        put16(jnt.data, 0x08, 1);
        put16(jnt.data, 0x0A, 0xFFFF);
        Bytes init(0x40, 0);
        put16(init, 0x00, 0);
        init[0x02] = 0;
        init[0x03] = 0xFF;
        putF(init, 0x04, 1.0f);
        putF(init, 0x08, 1.0f);
        putF(init, 0x0C, 1.0f);
        put16(init, 0x16, 0xFFFF);
        putF(init, 0x24, radius);
        putF(init, 0x28, lo.x);
        putF(init, 0x2C, lo.y);
        putF(init, 0x30, lo.z);
        putF(init, 0x34, hi.x);
        putF(init, 0x38, hi.y);
        putF(init, 0x3C, hi.z);
        jnt.place(init, 0x0C);
        jnt.place(Bytes{0, 0}, 0x10);
        jnt.place(nameTable({jointName}), 0x14);
        blocks.push_back(jnt.finish());
    }
    {  // SHP1: one shape, one matrix group, triangles in display lists of at most 65,535 vertices.
        Block shp("SHP1", 0x2C);
        put16(shp.data, 0x08, 1);
        put16(shp.data, 0x0A, 0xFFFF);
        Bytes init(0x28, 0);
        init[0x00] = 0;     // J3DShapeMtxType_Mtx
        init[0x01] = 0xFF;
        put16(init, 0x02, 1);  // matrix groups
        put16(init, 0x04, 0);  // vertex descriptors at byte 0 of the list
        put16(init, 0x06, 0);
        put16(init, 0x08, 0);
        put16(init, 0x0A, 0xFFFF);
        putF(init, 0x0C, radius);
        putF(init, 0x10, lo.x);
        putF(init, 0x14, lo.y);
        putF(init, 0x18, lo.z);
        putF(init, 0x1C, hi.x);
        putF(init, 0x20, hi.y);
        putF(init, 0x24, hi.z);
        shp.place(init, 0x0C);
        shp.place(Bytes{0, 0}, 0x10);
        shp.place(nameTable({"shape"}), 0x14);
        Bytes desc;
        for (std::uint32_t attr : {9u, 10u, 11u, 13u}) {
            append32(desc, attr);
            append32(desc, 3);  // GX_INDEX16
        }
        append32(desc, 0xFF);
        append32(desc, 0);
        shp.place(desc, 0x18);
        shp.place(Bytes{0, 0}, 0x1C);
        Bytes dl;
        const std::size_t perCommand = 0xFFFF / 3 * 3;
        for (std::size_t first = 0; first < mesh.indices.size(); first += perCommand) {
            const std::size_t count = std::min(perCommand, mesh.indices.size() - first);
            dl.push_back(0x90);  // GX_DRAW_TRIANGLES, vertex format 0
            append16(dl, static_cast< std::uint16_t >(count));
            for (std::size_t i = first; i < first + count; i++) {
                const std::uint16_t v = static_cast< std::uint16_t >(mesh.indices[i]);
                append16(dl, v);  // POS
                append16(dl, v);  // NRM
                append16(dl, 0);  // CLR0: the single white entry
                append16(dl, v);  // TEX0
            }
        }
        align(dl, 0x20);
        shp.place(dl, 0x20, 0x20);
        Bytes mtxInit(8, 0);
        put16(mtxInit, 0, 0);  // matrix 0
        put16(mtxInit, 2, 1);
        put32(mtxInit, 4, 0);
        shp.place(mtxInit, 0x24);
        Bytes drawInit(8, 0);
        put32(drawInit, 0, static_cast< std::uint32_t >(dl.size()));
        put32(drawInit, 4, 0);
        shp.place(drawInit, 0x28);
        blocks.push_back(shp.finish());
    }
    blocks.push_back(material.mat3);
    blocks.push_back(material.tex1);

    Bytes& file = result.model;
    file.assign(0x20, 0);
    std::memcpy(file.data(), "J3D2bmd3", 8);
    put32(file, 0x0C, static_cast< std::uint32_t >(blocks.size()));
    std::memcpy(&file[0x10], "SVR3", 4);
    for (std::size_t i = 0x14; i < 0x20; i++) {
        file[i] = 0xFF;
    }
    for (const Bytes& block : blocks) {
        file.insert(file.end(), block.begin(), block.end());
    }
    put32(file, 0x08, static_cast< std::uint32_t >(file.size()));
    result.vertices = static_cast< int >(vertexCount);
    result.triangles = static_cast< int >(mesh.triangleCount());
    return result;
}

}  // namespace PetariEditor::Terrain
