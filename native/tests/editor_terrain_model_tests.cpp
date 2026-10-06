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
#include <petari/editor/terrain/bake.hpp>
#include <petari/editor/terrain/brush.hpp>
#include <petari/editor/terrain/model_writer.hpp>
#include <petari/kcl_collision.hpp>
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
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <optional>
#include <fstream>
#include <unistd.h>
#include <filesystem>
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
static bool loadAndCheck(const Bytes& model, const T::Mesh& mesh, const std::string& material, std::uint16_t textures, const std::string& where,
                         float tint = 1.0f) {
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
        // TEX1 holds only the textures the material uses (at most the donor's).
        expect(pData->getTexture() != nullptr && pData->getTexture()->getNum() <= textures, "the material's textures");
        expect(pData->getVertexData().getVtxNum() == mesh.positions.size(), "vertex count");
        const f32* pPositions = static_cast< const f32* >(pData->getVertexData().getVtxPosArray());
        expect(pPositions != nullptr && pPositions[0] == mesh.positions[0].x && pPositions[2] == mesh.positions[0].z, "positions (host order)");
        // The baked light (CLR0 per vertex), scaled by the donor's own vertex colour: a face
        // towards the sun is lit more than one away, which gets the ambient.
        const GXColor* pColors = pData->getVertexData().getVtxColorArray(0);
        u8 brightest = 0, darkest = 255;
        for (std::size_t i = 0; pColors != nullptr && i < mesh.positions.size(); i++) {
            brightest = std::max(brightest, pColors[i].r);
            darkest = std::min(darkest, pColors[i].r);
        }
        const int ambient = static_cast< int >(0.55f * tint * 255.0f + 0.5f);
        expect(pColors != nullptr && std::abs(darkest - ambient) <= 2 && (brightest > darkest || tint < 0.05f), "baked vertex light (ambient + sun, tinted)");
        // ... and is the only light: a vertex-coloured COLOR0 is not lit by the scene again.
        J3DColorChan* pChan = pMaterial != nullptr && pMaterial->getColorBlock() != nullptr ? pMaterial->getColorBlock()->getColorChan(0) : nullptr;
        expect(pChan == nullptr || pChan->getMatSrc() != GX_SRC_VTX || pChan->getEnable() == 0, "a vertex-coloured COLOR0 is unlit");
        J3DColorChan* pAlpha = pMaterial != nullptr && pMaterial->getColorBlock() != nullptr ? pMaterial->getColorBlock()->getColorChan(1) : nullptr;
        expect(pAlpha == nullptr || pAlpha->getEnable() == 0 || pMaterial->mMaterialMode != 1, "an opaque material's ALPHA0 is unlit (the bake is the only light)");
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
    std::size_t modelBytes = 0;
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
                sFailures += loadAndCheck(r.model, *pMesh, name, textures, where + (pMesh == &box ? " box" : " sphere"), clone->vertexTint.x) ? 0 : 1;
                models++;
                modelBytes += r.model.size();
            }
        }
        std::printf("donor %s: %zu materials cloned onto a box and a sphere\n", donor.archive, names.size());
    }
    check(models > 0, "no donor model on the disc");
    // The donor's lit vertex colour on the material (the 90th percentile, measured on the disc:
    // FlowerPlanet's grass about 229/255; Bathtub's checker 255).
    auto tintOf = [&](const char* pArchive, const char* pModel, const char* pMaterial) {
        std::ifstream file(std::string(pDisc) + "/ObjectData/" + pArchive + ".arc", std::ios::binary);
        const Bytes raw((std::istreambuf_iterator< char >(file)), std::istreambuf_iterator< char >());
        const auto archive = PetariEditor::Formats::Archive::parse(raw);
        const Bytes* pBytes = archive ? archive->file(pModel) : nullptr;
        std::string error;
        const auto clone = pBytes != nullptr ? T::cloneMaterial(*pBytes, pMaterial, &error) : std::nullopt;
        return clone ? clone->vertexTint : T::Vec3{-1, -1, -1};
    };
    const T::Vec3 grass = tintOf("FlowerPlanet", "flowerplanet.bdl", "gras_v"), checker = tintOf("BathtubPlanet", "bathtubplanet.bdl", "CheckMat_v");
    check(grass.x > 0.86f && grass.x < 0.94f, "the grass donor's vertex tint (about 0.9)");
    check(checker.x > 0.99f && checker.y > 0.99f, "the checker donor's vertex tint (1)");
    std::printf("vertex tints: grass %.3f %.3f %.3f, checker %.3f\n", grass.x, grass.y, grass.z, checker.x);
    std::printf("%d generated models loaded through J3D (%zu KB on average)\n", models, models ? modelBytes / models / 1024 : 0);
}

// Bakes brush rows into planets (bake.hpp): Good Egg's own archive with four brushes on the
// Common layer and one on Layer A.
static void testBake(const char* pDisc) {
    namespace F = PetariEditor::Formats;
    auto read = [pDisc](const std::string& path) -> std::optional< Bytes > {
        std::ifstream file(std::string(pDisc) + path, std::ios::binary);
        if (!file) {
            return std::nullopt;
        }
        return Bytes((std::istreambuf_iterator< char >(file)), std::istreambuf_iterator< char >());
    };
    const auto raw = read("/StageData/EggStarGalaxy.arc");
    if (!raw) {
        std::printf("bake: Good Egg not on the disc, skipped\n");
        return;
    }
    auto archive = F::Archive::parse(*raw);
    auto countRows = [](const F::Archive& a, const char* pTable, const char* pPrefix) {
        auto table = F::Table::parse(*a.file(pTable));
        const int column = table->column(F::jmapHash("name"));
        int n = 0;
        for (int r = 0; r < table->rowCount(); r++) {
            n += table->get(r, column).s.rfind(pPrefix, 0) == 0;
        }
        return n;
    };
    const int commonGravity = F::Table::parse(*archive->file("jmp/placement/common/planetobjinfo"))->rowCount();
    const int layerGravity = F::Table::parse(*archive->file("jmp/placement/layera/planetobjinfo"))->rowCount();
    auto addBrush = [](F::Table& table, int shape, float x) {
        const int row = table.addRow(0);
        auto set = [&](const char* pName, F::CellValue v) {
            const int c = table.column(F::jmapHash(pName));
            if (c >= 0) table.set(row, c, v);
        };
        F::CellValue name;
        name.kind = F::CellValue::Kind::String;
        name.s = T::kBrushClass;
        set("name", name);
        auto num = [](float f) { F::CellValue v; v.kind = F::CellValue::Kind::Float; v.f = f; return v; };
        auto arg = [](int i) { F::CellValue v; v.i = i; return v; };
        set("pos_x", num(x));
        set("dir_y", num(30.0f));
        set("scale_x", num(2));
        set("scale_y", num(0.5f));
        set("scale_z", num(1.5f));
        set("Obj_arg0", arg(shape));
        set("Obj_arg1", arg(shape + 1));
        set("Obj_arg2", arg(0));
        set("Obj_arg3", arg(1));
    };
    auto common = F::Table::parse(*archive->file("jmp/placement/common/objinfo"));
    for (int shape = 0; shape < 4; shape++) {
        addBrush(*common, shape, 3000.0f * shape);
    }
    archive->replace("jmp/placement/common/objinfo", common->build());
    auto layerA = F::Table::parse(*archive->file("jmp/placement/layera/objinfo"));
    addBrush(*layerA, 0, -5000.0f);
    archive->replace("jmp/placement/layera/objinfo", layerA->build());

    T::BakeInput input;
    input.stageArchives["/StageData/EggStarGalaxy.arc"] = archive->build();
    input.readDisc = read;
    const T::BakeOutput out = T::bakeBrushes(input);
    check(out.error.empty(), out.error.c_str());
    check(out.brushes == 5, "five brushes baked");
    int planets = 0, loaded = 0;
    for (const auto& [path, bytes] : out.files) {
        if (path.rfind("/ObjectData/EditorBrush", 0) != 0) {
            continue;
        }
        planets++;
        auto planet = F::Archive::parse(bytes);
        const std::string base = path.substr(12, path.size() - 12 - 4);
        std::string lowerBase;
        for (char c : base) lowerBase += static_cast< char >(std::tolower(static_cast< unsigned char >(c)));
        const Bytes* pModel = planet ? planet->file(lowerBase + ".bmd") : nullptr;
        const Bytes* pKcl = planet ? planet->file(lowerBase + ".kcl") : nullptr;
        if (pModel == nullptr || pKcl == nullptr || planet->file(lowerBase + ".pa") == nullptr) {
            check(false, "a planet archive holds <name>.bmd, .kcl and .pa");
            continue;
        }
        Bytes kcl = *pKcl;
        check(PetariNative::KCL::convertInPlace(kcl.data(), static_cast< std::uint32_t >(kcl.size())) == nullptr, "the planet's KCL converts");
        Bytes image(pModel->size());
        loaded += PetariNative::J3D::makeHostModelImage(pModel->data(), static_cast< std::uint32_t >(pModel->size()), image.data()) == nullptr;
    }
    check(planets == 5 && loaded == 5, "five planet archives whose models convert");
    const auto stageIt = out.files.find("/StageData/EggStarGalaxy.arc");
    auto baked = stageIt != out.files.end() ? F::Archive::parse(stageIt->second) : std::nullopt;
    if (!baked) {
        check(false, "the stage archive is rewritten");
        return;
    }
    check(countRows(*baked, "jmp/placement/common/objinfo", "EditorBrush") == 4 && countRows(*baked, "jmp/placement/layera/objinfo", "EditorBrush") == 1,
          "brush rows become planets, layer by layer");
    auto bakedCommon = F::Table::parse(*baked->file("jmp/placement/common/objinfo"));
    const int nameColumn = bakedCommon->column(F::jmapHash("name"));
    for (int r = 0; r < bakedCommon->rowCount(); r++) {
        check(bakedCommon->get(r, nameColumn).s != T::kBrushClass, "no row keeps the brush class");
    }
    const int commonAfter = F::Table::parse(*baked->file("jmp/placement/common/planetobjinfo"))->rowCount();
    const int layerAfter = F::Table::parse(*baked->file("jmp/placement/layera/planetobjinfo"))->rowCount();
    check(commonAfter == commonGravity + 4 && layerAfter == layerGravity + 1, "a gravity row per brush, in its layer");
    check(countRows(*baked, "jmp/placement/common/planetobjinfo", "GlobalSegmentGravity") >= 1 &&
              countRows(*baked, "jmp/placement/common/planetobjinfo", "GlobalPointGravity") >= 1,
          "cylinder -> segment, sphere -> point gravity");

    auto tableRows = [](const Bytes& arc) {
        auto a = F::Archive::parse(arc);
        return F::Table::parse(*a->file("planetmapdatatable.bcsv"))->rowCount();
    };
    const int discPlanets = tableRows(*read("/ObjectData/PlanetMapDataTable.arc"));
    const Bytes& firstTable = out.files.at("/ObjectData/PlanetMapDataTable.arc");
    check(tableRows(firstTable) == discPlanets + 5, "PlanetMapDataTable gains the five planets");
    // Baking again over the mod's own table (as a re-save reads it) replaces, not adds.
    T::BakeInput again = input;
    again.readDisc = [&](const std::string& path) -> std::optional< Bytes > {
        return path == "/ObjectData/PlanetMapDataTable.arc" ? std::optional< Bytes >(firstTable) : read(path);
    };
    check(tableRows(T::bakeBrushes(again).files.at("/ObjectData/PlanetMapDataTable.arc")) == discPlanets + 5, "a re-bake does not pile up planets");

    // The patch-mod loader's builder: over the archives the row edits built, once per mod.
    std::vector< PetariEditor::Patch::Built > built = {{"StageData/EggStarGalaxy.arc", input.stageArchives.at("/StageData/EggStarGalaxy.arc"), {"Low"}}};
    const PetariEditor::Patch::DiscReader discReader = [&](const std::string& path) { return read("/" + path); };
    std::vector< std::string > log;
    std::string error;
    check(T::bakePatchAssets("mods/Low", discReader, &built, &log, &error) && error.empty(), "the patch builder bakes");
    const std::size_t builtFiles = built.size();
    check(builtFiles == 1 + 5 + 1 && built[0].bytes == out.files.at("/StageData/EggStarGalaxy.arc"),
          "the patch builder replaces the stage archive and adds five planets and the table, as a save would");
    check(T::bakePatchAssets("mods/High", discReader, &built, &log, &error) && built.size() == builtFiles,
          "a later mod's call over baked rows adds nothing");
    auto tableIt = std::find_if(built.begin(), built.end(), [](const PetariEditor::Patch::Built& b) { return b.path == "ObjectData/PlanetMapDataTable.arc"; });
    check(tableIt != built.end() && tableRows(tableIt->bytes) == discPlanets + 5 && tableIt->mods == std::vector< std::string >{"Low"},
          "the built table holds the five planets once");
    // A new galaxy's own stage archive (no row edits): the builder reads it from the mod's files/.
    {
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / ("petari-bake-own-" + std::to_string(::getpid()));
        fs::create_directories(root / "files" / "StageData");
        {
            const Bytes& own = input.stageArchives.at("/StageData/EggStarGalaxy.arc");
            std::ofstream file(root / "files" / "StageData" / "BoxTestGalaxy.arc", std::ios::binary);
            file.write(reinterpret_cast< const char* >(own.data()), static_cast< std::streamsize >(own.size()));
        }
        std::vector< PetariEditor::Patch::Built > own;
        std::vector< std::string > ownLog;
        std::string ownError;
        check(T::bakePatchAssets(root, discReader, &own, &ownLog, &ownError) && ownError.empty(), "the builder bakes a mod's own stage archive");
        auto ownStage = std::find_if(own.begin(), own.end(), [](const PetariEditor::Patch::Built& b) { return b.path == "StageData/BoxTestGalaxy.arc"; });
        check(own.size() == 1 + 5 + 1 && ownStage != own.end(), "its baked stage, five planets and the table replace the mod's own file");
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    std::printf("bake: 5 brushes -> %d planets, %zu files\n", planets, out.files.size());
}

int main() {
    OSInit();
    JKRExpHeap::createRoot(1, false);
    testInputs();
#ifdef PETARI_TEST_DISC_FILES
    testDisc(PETARI_TEST_DISC_FILES);
    testBake(PETARI_TEST_DISC_FILES);
#endif
    if (sFailures != 0) {
        std::fprintf(stderr, "%d terrain model check(s) failed\n", sFailures);
        return 1;
    }
    std::puts("terrain model writer tests passed");
    return 0;
}
