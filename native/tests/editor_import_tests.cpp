// Generated fixtures in this test (the wedge, textured platform and image) are dedicated to CC0.
// No downloaded model assets or disc data are needed.
#include "../editor/import/vendor/meshoptimizer/src/meshoptimizer.h"
#include <JSystem/J3DGraphAnimator/J3DModelData.hpp>
#include <JSystem/J3DGraphBase/J3DMaterial.hpp>
#include <JSystem/J3DGraphLoader/J3DModelLoader.hpp>
#include <JSystem/JKernel/JKRExpHeap.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <petari/boot.hpp>
#include <petari/editor/catalog.hpp>
#include <petari/editor/import_assets.hpp>
#include <petari/editor/terrain/kcl_reader.hpp>
#include <petari/editor/terrain/kcl_writer.hpp>
#include <petari/host_allocation.hpp>
#include <petari/j3d_model.hpp>
#include <petari/png_write.hpp>
#include <unistd.h>
namespace I = PetariEditor::Imports;
namespace T = PetariEditor::Terrain;
using Bytes = PetariEditor::Formats::Bytes;
int failures = 0, checks = 0;
void check(bool ok, const char* name) {
    ++checks;
    if (!ok) {
        ++failures;
        std::cerr << "FAIL: " << name << '\n';
    }
}
void write(const std::filesystem::path& p, const Bytes& b) {
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast< const char* >(b.data()), b.size());
}
void write(const std::filesystem::path& p, const std::string& s) {
    write(p, Bytes(s.begin(), s.end()));
}
void little(Bytes& b, unsigned v) {
    for (int i = 0; i < 4; ++i) {
        b.push_back(v >> (i * 8));
    }
}
void real(Bytes& b, float f) {
    unsigned v;
    std::memcpy(&v, &f, 4);
    little(b, v);
}
Bytes read(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return Bytes(std::istreambuf_iterator< char >(f), {});
}
int main() {
    OSInit();
    JKRExpHeap::createRoot(1, false);
    PetariNative::HostAllocationScope host;
    const auto dir = std::filesystem::temp_directory_path() / ("petari-import-" + std::to_string(getpid()));
    std::filesystem::create_directories(dir);
    std::string error;
    write(dir / "wedge.obj",
          std::string("mtllib wedge.mtl\nv 0 0 0\nv 2 0 0\nv 0 0 2\nv 0 2 2\nusemtl Stone\nf 1 3 2\nf 1 4 3\nusemtl Ice\nf 1 2 4\nf 2 3 4\n"));
    write(dir / "wedge.mtl", std::string("newmtl Stone\nKd .5 .6 .7\nnewmtl Ice\nKd .8 .9 1\n"));
    auto wedge = I::load(dir / "wedge.obj", &error);
    check(bool(wedge), "OBJ fixture loads");
    if (wedge) {
        check(wedge->primitives.size() == 2, "OBJ splits material groups");
        check(wedge->primitives[0].mesh.normals.size() == 6, "missing normals become face normals");
    }
    const unsigned char rgb[] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};
    PetariNative::writePngRgb((dir / "tiles.png").string(), 2, 2, rgb);
    auto image = I::decodeImage(read(dir / "tiles.png"), &error);
    check(bool(image) && image->width == 2, "PNG decoded by host");
    if (image) {
        check(image->rgba[0] > 240 && image->rgba[1] < 10 && image->rgba[8] < 10 && image->rgba[10] > 240,
              "texture row orientation survives decoding");
    }
    Bytes buffer;
    for (float v : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f, 0.f, 1.f}) {
        real(buffer, v);
    }
    for (float v : {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f}) {
        real(buffer, v);
    }
    for (unsigned i : {0, 2, 1, 0, 3, 2}) {
        buffer.push_back(i);
        buffer.push_back(0);
    }
    write(dir / "platform.bin", buffer);
    const std::string prefix = R"({"asset":{"version":"2.0","copyright":"CC0 generated fixture"},"buffers":[{"byteLength":92)";
    const std::string rest =
        R"(}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":48},{"buffer":0,"byteOffset":48,"byteLength":32},{"buffer":0,"byteOffset":80,"byteLength":12}],"accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3"},{"bufferView":1,"componentType":5126,"count":4,"type":"VEC2"},{"bufferView":2,"componentType":5123,"count":6,"type":"SCALAR"}],"images":[{"uri":"tiles.png"}],"textures":[{"source":0}],"materials":[{"name":"Tiles","pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1},"indices":2,"material":0}]}],"nodes":[{"mesh":0,"translation":[2,3,4],"scale":[-1,2,1]}],"scenes":[{"nodes":[0]}],"scene":0})";
    write(dir / "platform.gltf", prefix + ",\"uri\":\"platform.bin\"" + rest);
    auto platform = I::load(dir / "platform.gltf", &error);
    if (!platform) {
        std::cerr << error << '\n';
    }
    check(bool(platform), "glTF buffers, materials and images load");
    if (platform) {
        check(platform->primitives[0].mesh.positions[0].x == 2 && platform->primitives[0].mesh.positions[0].y == 3, "glTF node transform applied");
        check(platform->primitives[0].mesh.normals[0].y > .99f, "mirrored node preserves outward winding");
    }
    {
        std::string transformed = prefix + ",\"uri\":\"platform.bin\"" + rest;
        const auto offset = transformed.find("\"baseColorTexture\":{\"index\":0}");
        transformed.replace(offset, std::strlen("\"baseColorTexture\":{\"index\":0}"),
                            "\"baseColorTexture\":{\"index\":0,\"extensions\":{\"KHR_texture_transform\":{\"offset\":[0.25,0.5]}}}");
        write(dir / "uv-transform.gltf", transformed);
        const auto uv = I::load(dir / "uv-transform.gltf", &error);
        check(uv && std::fabs(uv->primitives[0].mesh.uvs[0] - .25f) < .0001f && std::fabs(uv->primitives[0].mesh.uvs[1] - .5f) < .0001f,
              "glTF texture transform applied");
        Bytes encoded(meshopt_encodeVertexBufferBound(4, 12));
        encoded.resize(meshopt_encodeVertexBuffer(encoded.data(), encoded.size(), buffer.data(), 4, 12));
        Bytes compressed = buffer;
        std::fill(compressed.begin(), compressed.begin() + 48, 0);
        compressed.insert(compressed.end(), encoded.begin(), encoded.end());
        write(dir / "compressed.bin", compressed);
        std::string packed = prefix + ",\"uri\":\"compressed.bin\"" + rest;
        auto at = packed.find("\"byteLength\":92");
        packed.replace(at, 15, "\"byteLength\":" + std::to_string(compressed.size()));
        const std::string view = "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":48}";
        at = packed.find(view);
        packed.replace(at, view.size(),
                       "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":48,\"extensions\":{\"EXT_meshopt_compression\":{\"buffer\":0,\"byteOffset\":92,"
                       "\"byteLength\":" +
                           std::to_string(encoded.size()) + ",\"byteStride\":12,\"count\":4,\"mode\":\"ATTRIBUTES\"}}}");
        write(dir / "compressed.gltf", packed);
        const auto meshopt = I::load(dir / "compressed.gltf", &error);
        if (!meshopt)
            std::cerr << error << '\n';
        check(meshopt && std::fabs(meshopt->primitives[0].mesh.positions[1].x - 1) < .0001f, "meshoptimizer compressed vertex data decoded");
    }
    std::string json = prefix + rest;
    while (json.size() % 4) {
        json += ' ';
    }
    Bytes glb;
    little(glb, 0x46546c67);
    little(glb, 2);
    little(glb, 12 + 8 + json.size() + 8 + buffer.size());
    little(glb, json.size());
    little(glb, 0x4e4f534a);
    glb.insert(glb.end(), json.begin(), json.end());
    little(glb, buffer.size());
    little(glb, 0x004e4942);
    glb.insert(glb.end(), buffer.begin(), buffer.end());
    write(dir / "platform.glb", glb);
    check(bool(I::load(dir / "platform.glb", &error)), "GLB binary container loads");
    if (wedge) {
        I::Options options;
        options.scale = 100;
        options.up = I::UpAxis::Z;
        options.collision = I::Collision::Mesh;
        options.materials.resize(wedge->materials.size());
        options.materials[1].surface.floor = T::floorCode("Ice");
        auto asset = I::prepare(*wedge, options, &error);
        check(bool(asset), "prepare OBJ with scale/up/origin");
        if (asset) {
            check(asset->parts.size() == 2, "one custom part per material");
            T::Vec3 lo, hi;
            asset->parts[0].mesh.bounds(&lo, &hi);
            check(std::fabs(lo.y) < .01f && hi.y == 200, "Z-up and bottom-centre normalisation");
            const auto bytes = I::serialize(*asset);
            auto reopened = I::deserialize(bytes, &error);
            if (!reopened) {
                std::cerr << error << '\n';
            }
            check(bool(reopened) && I::serialize(*reopened) == bytes, "neutral asset byte-exact roundtrip");
            auto truncated = bytes;
            truncated.resize(bytes.size() / 2);
            check(!I::deserialize(truncated, &error), "truncated asset rejected");
            std::string id;
            int part = -1;
            const auto hash = I::contentId(bytes);
            check(I::parseClassName(I::className(hash, 1).c_str(), &id, &part) && id == hash && part == 1, "stable custom class identity");
            check(!I::parseClassName("EditorModel_../../secret_0"), "noncanonical asset identity rejected");
            for (const auto& p : asset->parts) {
                auto kcl = T::buildKcl(p.collision);
                check(kcl.error.empty(), "each material collision writes KCL/PA");
            }
        }
    }
    I::Scene large;
    large.name = "CC0 sphere";
    large.materials.push_back(I::SourceMaterial{"Stone"});
    large.primitives.push_back({T::makeSphere({}, 100, 128, 128), 0});
    I::Options opts;
    opts.scale = 1;
    opts.collision = I::Collision::Mesh;
    opts.generateLods = false;
    auto big = I::prepare(large, opts, &error);
    check(bool(big) && big->parts.size() > 1, "large model auto-splits");
    if (big) {
        for (const auto& p : big->parts) {
            check(p.collision.triangleCount() <= 16000 && p.mesh.positions.size() <= 65535, "parts stay within KCL and BMD index limits");
            check(T::buildKcl(p.collision).error.empty(), "large split actually fits KCL writer");
        }
    }
    opts.collision = I::Collision::Simplified;
    opts.collisionRatio = .2f;
    auto simple = I::prepare(large, opts, &error);
    if (simple) {
        size_t n = 0;
        for (const auto& p : simple->parts) {
            n += p.collision.triangleCount();
        }
        check(n < large.primitives[0].mesh.triangleCount() / 2, "meshoptimizer reduces collision");
    } else {
        check(false, "collision simplification prepares");
    }
    I::Image red{9, 5, Bytes(9 * 5 * 4)};
    for (size_t i = 0; i < red.rgba.size(); i += 4) {
        red.rgba[i] = 255;
        red.rgba[i + 3] = 255;
    }
    for (auto format : {I::TextureFormat::CMPR, I::TextureFormat::RGB5A3, I::TextureFormat::I8}) {
        error.clear();
        const auto encoded = I::encodeTexture(red, format, &error);
        check(error.empty() && !encoded.data.empty(), "GX texture encodes padded dimensions");
        check(encoded.data.size() == (format == I::TextureFormat::CMPR   ? 64u :
                                      format == I::TextureFormat::RGB5A3 ? 192u :
                                                                           128u),
              "GX block byte count");
        if (format == I::TextureFormat::I8) {
            check(encoded.data[0] >= 76 && encoded.data[0] <= 78, "I8 luminance");
        }
        I::Material m;
        m.name = "Imported";
        m.texture = encoded;
        auto native = I::nativeMaterial(m);
        auto model = T::buildModel(T::makeBox({}, {100, 100, 100}), native);
        check(model.error.empty(), "native imported material builds BMD");
        if (model.error.empty()) {
            Bytes converted(model.model.size());
            const char* e = PetariNative::J3D::makeHostModelImage(model.model.data(), model.model.size(), converted.data());
            if (e) {
                std::cerr << e << '\n';
            }
            check(!e, "native converter accepts imported BMD");
            if (!e) {
                auto* heap = JKRExpHeap::create(8 * 1024 * 1024, JKRHeap::sRootHeap, false);
                auto* old = heap->becomeCurrentHeap();
                auto* loaded = J3DModelLoaderDataBase::load(model.model.data(), J3DMLF_Material_PE_Full | J3DMLF_Material_UseIndirect |
                                                                                    J3DMLF_UseUniqueMaterials | J3DMLF_21);
                check(loaded && loaded->getMaterialNum() == 1 && loaded->getShapeNum() == 1, "real J3D loader accepts imported material");
                if (loaded) {
                    loaded->newSharedDisplayList(0);
                }
                old->becomeCurrentHeap();
                JKRHeap::destroy(heap);
            }
        }
    }
    red.rgba[3] = 100;
    auto alpha = I::encodeTexture(red, I::TextureFormat::Auto, &error);
    check(alpha.format == I::TextureFormat::RGB5A3, "Auto preserves texture alpha");
    write(dir / "bad.gltf", std::string("{broken"));
    check(!I::load(dir / "bad.gltf", &error), "malformed file fails without a scene");
    if (wedge) {
        namespace F = PetariEditor::Formats;
        I::Options options;
        options.scale = 100;
        options.collision = I::Collision::Mesh;
        auto asset = I::prepare(*wedge, options, &error);
        I::loadLibrary(dir.string(), {});
        const auto libraryId = I::storeAsset(*asset, &error);
        check(!libraryId.empty() && I::findAsset(libraryId), "import persists neutral reusable library asset");
        PetariEditor::AddRow first;
        first.id = PetariEditor::newRowId();
        first.zone = 2;
        first.table = "jmp/placement/common/objinfo";
        first.cells.push_back(
            {PetariEditor::columnHash("name"), PetariEditor::Cell{PetariEditor::CellType::StringOffset, 0, 0, I::className(libraryId, 0)}});
        PetariEditor::Op placement;
        placement.adds.push_back(first);
        I::appendPlacementParts(placement, first);
        check(placement.adds.size() == asset->parts.size() && placement.adds[1].zone == 2 && placement.adds[1].id != first.id,
              "Browser model placement includes all material splits in one op");
        const auto* info = PetariEditor::Catalog::find(I::className(libraryId, 0).c_str());
        check(info && info->bakesScale, "generated import catalog declares exact XYZ collision scale");
        const auto encoded = I::serialize(*asset);
        const auto id = I::contentId(encoded);
        F::CellValue name;
        name.kind = F::CellValue::Kind::String;
        name.s = I::className(id, 0);
        F::CellValue scalar;
        scalar.kind = F::CellValue::Kind::Float;
        scalar.f = 1;
        F::Table objects;
        objects.addColumn(F::jmapHash("name"), F::ColumnType::StringOffset, name);
        for (const char* c : {"scale_x", "scale_y", "scale_z"}) {
            objects.addColumn(F::jmapHash(c), F::ColumnType::Float, scalar);
        }
        const int row = objects.addRow(-1);
        objects.set(row, 0, name);
        for (int c = 1; c < 4; ++c) {
            objects.set(row, c, scalar);
        }
        F::Table planets;
        name.s = "OriginalPlanet";
        planets.addColumn(F::jmapHash("PlanetName"), F::ColumnType::StringOffset, name);
        F::CellValue flag;
        flag.kind = F::CellValue::Kind::Int;
        for (const char* c : {"MiddleFlag", "LowFlag"}) {
            planets.addColumn(F::jmapHash(c), F::ColumnType::Long, flag);
        }
        planets.addRow(-1);
        planets.set(0, 0, name);
        const auto table = F::Archive::create("planetmapdatatable", {{"planetmapdatatable.bcsv", planets.build()}}).build();
        T::BakeInput input;
        input.stageArchives["/StageData/Fixture.arc"] = F::Archive::create("stage", {{"jmp/placement/common/objinfo", objects.build()}}).build();
        input.readDisc = [&](const std::string&) -> std::optional< Bytes > { return table; };
        auto shared = std::make_shared< I::Asset >(*asset);
        const auto baked = I::bake(input, [&](const std::string& key) -> std::shared_ptr< const I::Asset > { return key == id ? shared : nullptr; });
        check(baked.error.empty() && baked.brushes == 1, "import bakes to native planet without disc materials");
        if (baked.error.empty()) {
            const auto stage = F::Archive::parse(baked.files.at("/StageData/Fixture.arc"));
            const auto placed = F::Table::parse(*stage->file("jmp/placement/common/objinfo"));
            check(placed->get(0, 0).s.rfind("EditorImport", 0) == 0, "baked placement uses registered planet class");
            const auto planet = F::Archive::parse(baked.files.at("/ObjectData/" + placed->get(0, 0).s + ".arc"));
            int count = 0;
            for (const auto& f : planet->files()) {
                count += f.find(".bmd") != std::string::npos || f.find(".kcl") != std::string::npos || f.find(".pa") != std::string::npos;
            }
            check(count == 3, "native planet contains model, collision and attributes");
        }
        const auto mod = dir / "mods" / "OwnModels";
        std::filesystem::create_directories(mod / "assets" / "models");
        write(mod / "assets" / "models" / (id + ".pmodel"), encoded);
        std::vector< PetariEditor::Patch::Built > files{{"StageData/Fixture.arc", input.stageArchives.begin()->second, {"OwnModels"}}};
        check(I::bakePatchAssets(
                  mod, [&](const std::string&) -> std::optional< Bytes > { return table; }, &files, nullptr, &error),
              "patch asset builder resolves portable neutral asset");
        auto before = files;
        check(I::bakePatchAssets(
                  mod, [&](const std::string&) -> std::optional< Bytes > { return table; }, &files, nullptr, &error) &&
                  files.size() == before.size(),
              "patch model bake is idempotent");
        bool equal = files.size() == before.size();
        for (size_t i = 0; i < files.size() && i < before.size(); ++i) {
            equal &= files[i].bytes == before[i].bytes;
        }
        check(equal, "second patch bake preserves all generated bytes");
        asset->parts[0].collision = {};
        auto visual = I::buildPart(*asset, 0, {1, 1, 1}, {});
        check(visual.error.empty() && visual.kcl.empty() && !visual.model.empty(), "visual-only import has no collision");
        asset->parts[0].collision = asset->parts[0].mesh;
        asset->parts[0].mesh = {};
        auto collisionOnly = I::buildPart(*asset, 0, {1, 1, 1}, {});
        check(collisionOnly.error.empty() && !collisionOnly.kcl.empty() && !collisionOnly.model.empty(),
              "collision-only split has valid invisible native model");
    }
    std::filesystem::remove_all(dir);
    std::cout << (failures ? "FAIL: " : "PASS: ") << checks << " model import checks\n";
    return failures ? 1 : 0;
}
