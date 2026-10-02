// The level editor's terrain model writer (native/editor/terrain/model_writer.cpp) loaded by the
// game's own J3D code: the generated J3D2bmd3 file goes through the native big-endian converter
// (PetariNative::J3D::makeHostModelImage) and J3DModelLoaderDataBase::load with ResourceHolder's
// .bmd flags, then the model data is checked (one joint, material, shape; the cloned material's
// name, TEV stages and textures; the vertex arrays and bounds).
//
// Needs the disc (PETARI_TEST_DISC_FILES) for donor materials; without it only the writer's input
// checks run. With the disc, every material of a few planets is cloned onto a box and a sphere.
// Links: petari_j3d (J3DGraphLoader, J3DGraphBase, native/resource/j3d_model.cpp, a GX
// implementation), petari_editor_terrain.
#include <petari/editor/formats/formats.hpp>
#include <petari/editor/terrain/model_writer.hpp>
#include <petari/boot.hpp>
#include <petari/j3d_model.hpp>
#include <JSystem/J3DGraphAnimator/J3DJoint.hpp>
#include <JSystem/J3DGraphAnimator/J3DModelData.hpp>
#include <JSystem/J3DGraphBase/J3DMaterial.hpp>
#include <JSystem/J3DGraphBase/J3DShape.hpp>
#include <JSystem/J3DGraphLoader/J3DModelLoader.hpp>
#include <JSystem/JKernel/JKRExpHeap.hpp>
#include <JSystem/JUtility/JUTNameTab.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

namespace T = PetariEditor::Terrain;
using PetariEditor::Formats::Bytes;

static int sFailures = 0;
static void check(bool condition, const char* text) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", text);
        ++sFailures;
    }
}

static std::uint16_t be16(const Bytes& b, std::size_t at) { return static_cast< std::uint16_t >(b[at] << 8 | b[at + 1]); }

static std::uint16_t donorTextureCount(const Bytes& donor) {
    std::size_t at = 0x20;
    const std::uint32_t blocks = static_cast< std::uint32_t >(donor[12] << 24 | donor[13] << 16 | donor[14] << 8 | donor[15]);
    for (std::uint32_t i = 0; i < blocks; i++) {
        const std::uint32_t size = static_cast< std::uint32_t >(donor[at + 4] << 24 | donor[at + 5] << 16 | donor[at + 6] << 8 | donor[at + 7]);
        if (std::memcmp(&donor[at], "TEX1", 4) == 0) {
            return be16(donor, at + 8);
        }
        at += size;
    }
    return 0;
}

// Loads `model` as ResourceHolder loads a .bmd, on its own heap, and checks it.
static bool loadAndCheck(const Bytes& model, const T::Mesh& mesh, const std::string& material, std::uint16_t textures, const std::string& where) {
    Bytes image(model.size());
    if (const char* pError = PetariNative::J3D::makeHostModelImage(model.data(), static_cast< std::uint32_t >(model.size()), image.data())) {
        std::fprintf(stderr, "FAIL: %s: the native converter refused the model: %s\n", where.c_str(), pError);
        return false;
    }
    JKRExpHeap* pHeap = JKRExpHeap::create(16 * 1024 * 1024, JKRHeap::sRootHeap, false);
    JKRHeap* pPrevious = pHeap->becomeCurrentHeap();
    bool ok = true;
    auto expect = [&](bool condition, const char* pWhat) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s: %s\n", where.c_str(), pWhat);
            ok = false;
        }
    };
    J3DModelData* pData =
        J3DModelLoaderDataBase::load(model.data(), J3DMLF_Material_PE_Full | J3DMLF_Material_UseIndirect | J3DMLF_UseUniqueMaterials | J3DMLF_21);
    expect(pData != nullptr, "the loader returned a model");
    if (pData != nullptr) {
        pData->newSharedDisplayList(0);
        expect(pData->getJointNum() == 1 && pData->getMaterialNum() == 1 && pData->getShapeNum() == 1, "one joint, material and shape");
        expect(pData->getJointTree().getRootNode() != nullptr, "the joint is the root");
        J3DMaterial* pMaterial = pData->getMaterialNodePointer(0);
        expect(pMaterial != nullptr && pMaterial->getShape() == pData->getShapeNodePointer(0), "the shape hangs off the material");
        expect(pMaterial != nullptr && pMaterial->getJoint() == pData->getJointNodePointer(0), "the material hangs off the joint");
        expect(pMaterial != nullptr && pMaterial->getTevBlock() != nullptr && pMaterial->getTevBlock()->getTevStageNum() >= 1, "TEV stages");
        const JUTNameTab* pNames = pData->getMaterialTable().getMaterialName();
        expect(pNames != nullptr && std::strcmp(pNames->getName(0), material.c_str()) == 0, "the material keeps its donor name");
        expect(pData->getTexture() != nullptr && pData->getTexture()->getNum() == textures, "the donor's textures");
        expect(pData->getVertexData().getVtxNum() == mesh.positions.size(), "vertex count");
        const f32* pPositions = static_cast< const f32* >(pData->getVertexData().getVtxPosArray());
        expect(pPositions != nullptr && pPositions[0] == mesh.positions[0].x && pPositions[2] == mesh.positions[0].z, "positions (host order)");
        const f32* pUvs = static_cast< const f32* >(pData->getVertexData().getVtxTexCoordArray(0));
        expect(pUvs != nullptr && pUvs[1] == mesh.uvs[1], "UVs (host order)");
        J3DShape* pShape = pData->getShapeNodePointer(0);
        T::Vec3 lo, hi;
        mesh.bounds(&lo, &hi);
        expect(pShape != nullptr && pShape->getMin()->x == lo.x && pShape->getMax()->y == hi.y, "shape bounds");
    }
    pPrevious->becomeCurrentHeap();
    JKRHeap::destroy(pHeap);
    return ok;
}

static void testInputs() {
    T::MaterialClone none;
    check(!T::buildModel(T::Mesh(), none).error.empty(), "an empty mesh is refused");
    T::Mesh noUv = T::makeBox({0, 0, 0}, {10, 10, 10});
    noUv.uvs.clear();
    check(!T::buildModel(noUv, none).error.empty(), "a mesh without UVs is refused");
    std::string error;
    check(!T::cloneMaterial(Bytes(64, 0), "x", &error).has_value() && !error.empty(), "a non-model donor is refused");
}

static void testDisc(const char* pDisc) {
    struct Donor {
        const char* archive;
        const char* model;
    };
    const Donor donors[] = {{"BathtubPlanet", "bathtubplanet.bdl"}, {"BeeWallClimbPlanetA", "beewallclimbplaneta.bdl"},
                            {"BroadBeanPlanet", "broadbeanplanet.bdl"}, {"AssemblyBlockPlanet", "assemblyblockplanet.bdl"}};
    const T::Mesh box = T::makeBox({0, 150, 0}, {1500, 150, 1500});
    const T::Mesh sphere = T::makeSphere({0, 0, 0}, 1000.0f, 24, 32);
    int models = 0;
    for (const Donor& donor : donors) {
        std::ifstream file(std::string(pDisc) + "/ObjectData/" + donor.archive + ".arc", std::ios::binary);
        if (!file) {
            std::printf("donor %s: not on the disc, skipped\n", donor.archive);
            continue;
        }
        const Bytes raw((std::istreambuf_iterator< char >(file)), std::istreambuf_iterator< char >());
        const auto archive = PetariEditor::Formats::Archive::parse(raw);
        const Bytes* pModel = archive ? archive->file(donor.model) : nullptr;
        if (pModel == nullptr) {
            check(false, "donor archive has its model");
            continue;
        }
        const std::vector< std::string > names = T::modelMaterialNames(*pModel);
        check(!names.empty(), "donor materials listed");
        const std::uint16_t textures = donorTextureCount(*pModel);
        for (const std::string& name : names) {
            std::string error;
            const auto clone = T::cloneMaterial(*pModel, name, &error);
            const std::string where = std::string(donor.archive) + ":" + name;
            if (!clone) {
                std::fprintf(stderr, "FAIL: %s: clone: %s\n", where.c_str(), error.c_str());
                ++sFailures;
                continue;
            }
            for (const T::Mesh* pMesh : {&box, &sphere}) {
                const T::ModelResult r = T::buildModel(*pMesh, *clone);
                if (!r.error.empty()) {
                    std::fprintf(stderr, "FAIL: %s: buildModel: %s\n", where.c_str(), r.error.c_str());
                    ++sFailures;
                    continue;
                }
                sFailures += loadAndCheck(r.model, *pMesh, name, textures, where + (pMesh == &box ? " box" : " sphere")) ? 0 : 1;
                models++;
            }
        }
        std::printf("donor %s: %zu materials cloned onto a box and a sphere\n", donor.archive, names.size());
    }
    check(models > 0, "no donor model on the disc");
    std::printf("%d generated models loaded through J3D\n", models);
}

int main() {
    OSInit();
    JKRExpHeap::createRoot(1, false);
    testInputs();
#ifdef PETARI_TEST_DISC_FILES
    testDisc(PETARI_TEST_DISC_FILES);
#endif
    if (sFailures != 0) {
        std::fprintf(stderr, "%d terrain model check(s) failed\n", sFailures);
        return 1;
    }
    std::puts("terrain model writer tests passed");
    return 0;
}
