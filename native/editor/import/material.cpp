#include "binary.hpp"
#include <algorithm>
#include <cmath>
namespace PetariEditor::Imports {
    Terrain::MaterialClone nativeMaterial(const Material& material) {
        using namespace Binary;
        Terrain::MaterialClone out;
        out.name = material.name;
        out.vertexTint = {material.color[0], material.color[1], material.color[2]};
        Texture tex = material.texture;
        if (tex.data.empty()) {
            std::string error;
            tex = encodeTexture({1, 1, {255, 255, 255, 255}}, TextureFormat::RGB5A3, &error);
        }
        Block texture("TEX1", 0x20);
        put16(texture.data, 8, 1);
        Bytes header(0x20);
        header[0] = static_cast< unsigned >(tex.format);
        header[1] = 1;
        put16(header, 2, tex.width);
        put16(header, 4, tex.height);
        header[6] = header[7] = 1;        // repeat
        header[0x14] = header[0x15] = 1;  // linear, no mipmaps
        header[0x18] = 1;                 // one mip level
        const auto textureHeader = texture.table(header, 0x0c, 32);
        texture.table(names("imported"), 0x10);
        align(texture.data, 32);
        put32(texture.data, textureHeader + 0x1c, texture.data.size() - textureHeader);
        texture.data.insert(texture.data.end(), tex.data.begin(), tex.data.end());
        out.tex1 = texture.finish();

        Block mat("MAT3", 0x84);
        put16(mat.data, 8, 1);
        Bytes init(0x14c, 0xff);
        const bool alpha = material.color[3] < .999f || tex.format == TextureFormat::RGB5A3;
        init[0] = alpha ? 4 : 1;
        for (int i = 1; i < 8; ++i) {
            init[i] = 0;
        }
        put16(init, 0x08, 0);
        put16(init, 0x0c, 0);
        put16(init, 0x0e, 1);
        put16(init, 0x28, 0);
        put16(init, 0x84, 0);
        put16(init, 0xbc, 0);
        put16(init, 0xe4, 0);
        put16(init, 0x104, 0);
        for (int i = 0; i < 4; ++i) {
            put16(init, 0x124 + i * 2, 0);
        }
        put16(init, 0x146, 0);
        put16(init, 0x148, 0);
        mat.table(init, 0x0c);
        mat.table({0, 0}, 0x10);
        mat.table(names(material.name.empty() ? "Imported" : material.name), 0x14);
        mat.table(Bytes(0x138), 0x18);  // indirect texturing disabled
        mat.table({0, 0, 0, static_cast< unsigned char >(material.doubleSided ? 0 : 2)}, 0x1c);
        mat.table({255, 255, 255, static_cast< unsigned char >(std::clamp(material.color[3], 0.f, 1.f) * 255)}, 0x20);
        mat.table({1}, 0x24);
        // Unlit vertex colour (the writer bakes diffuse); constant material alpha.
        mat.table({0, 1, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0}, 0x28);
        mat.table({1}, 0x34);
        mat.table({1, 4, 60, 0}, 0x38);  // UV0, identity
        mat.table({0, 0}, 0x48);
        mat.table({0, 0, 4, 0}, 0x4c);
        mat.table({1}, 0x58);
        // (0 * (1-raster) + texture * raster) + 0, for both colour and alpha.
        mat.table({0, 15, 8, 10, 15, 0, 0, 0, 1, 0, 7, 4, 5, 7, 0, 0, 0, 1, 0, 0}, 0x5c);
        mat.table({0, 0, 0, 0}, 0x60);
        mat.table({0, 1, 2, 3}, 0x64);
        mat.table({4, 0, 0, 7, 0, 0, 0, 0}, 0x6c);  // alpha > 0, AND always
        mat.table({static_cast< unsigned char >(alpha ? 1 : 0), 4, 5, 15}, 0x70);
        mat.table({1, 3, static_cast< unsigned char >(alpha ? 0 : 1), 0}, 0x74);  // LEQUAL
        mat.table({0}, 0x78);
        mat.table({1}, 0x7c);
        out.mat3 = mat.finish();
        return out;
    }
}  // namespace PetariEditor::Imports
