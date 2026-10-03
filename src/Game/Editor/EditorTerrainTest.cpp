#ifdef PETARI_NATIVE
// Live check of terrain built in memory (spikes S6 and S7, docs/dev/level-editor/TECH.md 6.3;
// native/tools/editor_terrain_smoke.py): PETARI_EDITOR=1 PETARI_EDITOR_TERRAIN_TEST=1 on the
// Good Egg stage smoke. On the first gameplay frame it builds a box brush and a sphere brush with
// the editor's KCL writer, creates a collision part for each from the in-memory KCL, and fits a
// cube and a point gravity to them, far from the galaxy's own planets. The same meshes become J3D
// models (model_writer.cpp, the material cloned from a disc planet: BathtubPlanet's checker for
// the box, BeeWallClimbPlanetA's Soil06_v for the sphere), served as in-memory ObjectData archives
// and drawn by ModelObjs that follow the parts: a visible, walkable blockout.
//   box    3000 x 300 x 3000, centred at (-20000, 0, 30000): its top face is y = 150
//   sphere radius 1000, centred at (-20000, 0, 40000): its north pole is y = 1000
// The smoke's PETARI_STAGE_WARP puts Mario on one of them, and the smoke's own walks, jump and
// spin then run there. Every frame it records whether Mario stands on a generated part, how far
// he moved while grounded on it, and the gravity and surface he gets. When the smoke's checks are
// done it logs "[editor-terrain] PASS ..." or "[editor-terrain] FAIL: <reason>".
// PETARI_EDITOR_TERRAIN_TEST=archive builds nothing: the same box and sphere come from a baked
// mod (native/editor/tools/terrain_bake.cpp: ObjectData planet archives, PlanetMapDataTable rows,
// placement and gravity rows), loaded by the game's own placement; the check is the same, on the
// planets the bake made of the two brush rows (EditorBrush<hash>).
// Test code only: the editor builds parts from brushes in the EditModel.
#include "Game/Editor/EditorScene.hpp"
#include "Game/Editor/EditorTerrain.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include <JSystem/JKernel/JKRSolidHeap.hpp>
#include <petari/editor/runtime_hooks.hpp>
#include <petari/editor/terrain/kcl_writer.hpp>
#include <petari/editor/terrain/model_writer.hpp>
#include <petari/host_allocation.hpp>
#include <petari/kcl_collision.hpp>
#include <petari/screenshot.hpp>
#include <petari/smoke_phase.hpp>

#include <cctype>
#include <chrono>
#include <optional>
#include <string>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
    namespace T = PetariEditor::Terrain;

    struct Built {
        u8* mKcl = nullptr;
        u8* mPa = nullptr;
        EditorTerrain::Part* mPart = nullptr;
        LiveActor* mModel = nullptr;
    };

    struct Look {
        const char* mDonorArchive;  // "/ObjectData/BathtubPlanet.arc"
        const char* mDonorModel;    // "bathtubplanet.bdl"
        const char* mMaterial;      // "CheckMat_v"
    };

    // The model: the mesh with a material cloned from a disc model, as an in-memory archive
    // "/ObjectData/<pName>.arc" holding "<name>.bmd", drawn by a ModelObj with the part's matrix.
    bool buildModel(const T::Mesh& mesh, const char* pName, const Look& rLook, Built* pOut, const char** ppError) {
        const auto start = std::chrono::steady_clock::now();
        // The donor archive through the game's own loader (decompressed), on the scene heap.
        u8* pDonor = static_cast< u8* >(MR::loadToMainRAM(rLook.mDonorArchive, nullptr, MR::getSceneHeapNapa(), JKRDvdRipper::ALLOC_DIRECTION_FORWARD));
        if (pDonor == nullptr) {
            *ppError = "the donor archive did not load";
            return false;
        }
        const u32 donorSize = static_cast< u32 >(pDonor[4] << 24 | pDonor[5] << 16 | pDonor[6] << 8 | pDonor[7]);
        char path[96];
        snprintf(path, sizeof(path), "/ObjectData/%s.arc", pName);
        std::string error;
        PetariEditor::Formats::Bytes archive;
        int triangles = 0;
        {
            PetariNative::HostAllocationScope host;
            const auto donor = PetariEditor::Formats::Archive::parse(PetariEditor::Formats::Bytes(pDonor, pDonor + donorSize));
            const PetariEditor::Formats::Bytes* pModel = donor ? donor->file(rLook.mDonorModel) : nullptr;
            const auto material = pModel != nullptr ? T::cloneMaterial(*pModel, rLook.mMaterial, &error) : std::nullopt;
            if (material) {
                const T::ModelResult model = T::buildModel(mesh, *material);
                error = model.error;
                triangles = model.triangles;
                std::string base(pName);
                for (char& c : base) {
                    c = static_cast< char >(std::tolower(static_cast< unsigned char >(c)));
                }
                if (error.empty()) {
                    archive = PetariEditor::Formats::Archive::create(base, {{base + ".bmd", model.model}}).build();
                }
            } else if (error.empty()) {
                error = "no donor model";
            }
        }
        if (!error.empty()) {
            std::fprintf(stderr, "[editor-terrain] %s model: %s\n", pName, error.c_str());
            *ppError = "the model did not build";
            return false;
        }
        const double buildMs = std::chrono::duration< double, std::milli >(std::chrono::steady_clock::now() - start).count();
        const auto created = std::chrono::steady_clock::now();
        EditorTerrain::injectArchive(path, archive.data(), static_cast< u32 >(archive.size()));
        pOut->mModel = EditorTerrain::createModel(pName, pName, pOut->mPart->mBaseMtx.toMtxPtr(), ppError);
        const double createMs = std::chrono::duration< double, std::milli >(std::chrono::steady_clock::now() - created).count();
        std::fprintf(stderr, "[editor-terrain] model %s: %d triangles, %zu-byte archive (%s from %s) in %.2f ms; ModelObj created in %.2f ms\n", pName,
                     triangles, archive.size(), rLook.mMaterial, rLook.mDonorArchive, buildMs, createMs);
        return pOut->mModel != nullptr;
    }

    const TVec3f kBoxCenter(-20000.0f, 0.0f, 30000.0f);
    const TVec3f kBoxHalf(1500.0f, 150.0f, 1500.0f);
    const TVec3f kSphereCenter(-20000.0f, 0.0f, 40000.0f);
    const f32 kSphereRadius = 1000.0f;

    // Builds a part from a mesh in the part's local space. The buffers are host memory and live
    // as long as the process (the scene keeps pointers into them).
    bool build(const T::Mesh& mesh, const char* pName, const TVec3f& rPos, Built* pOut, const char** ppError) {
        T::KclResult result;
        double ms = 0.0;
        {
            PetariNative::HostAllocationScope host;
            const auto start = std::chrono::steady_clock::now();
            result = T::buildKcl(mesh);
            if (result.error.empty()) {
                pOut->mKcl = static_cast< u8* >(std::malloc(result.kcl.size()));
                pOut->mPa = static_cast< u8* >(std::malloc(result.pa.size()));
                std::memcpy(pOut->mKcl, result.kcl.data(), result.kcl.size());
                std::memcpy(pOut->mPa, result.pa.data(), result.pa.size());
            }
            ms = std::chrono::duration< double, std::milli >(std::chrono::steady_clock::now() - start).count();
        }
        if (!result.error.empty()) {
            *ppError = "buildKcl failed";
            std::fprintf(stderr, "[editor-terrain] %s: %s\n", pName, result.error.c_str());
            return false;
        }
        if (const char* pError = PetariNative::KCL::convertInPlace(pOut->mKcl, static_cast< u32 >(result.kcl.size()))) {
            *ppError = pError;
            return false;
        }
        const auto start = std::chrono::steady_clock::now();
        pOut->mPart = EditorTerrain::createPart(pName, rPos, pOut->mKcl, pOut->mPa, ppError);
        const double createMs = std::chrono::duration< double, std::milli >(std::chrono::steady_clock::now() - start).count();
        std::fprintf(stderr, "[editor-terrain] built %s: %d triangles, %zu-byte KCL, %zu-byte .pa in %.2f ms; part created in %.2f ms\n", pName,
                     result.stats.triangles, result.kcl.size(), result.pa.size(), ms, createMs);
        return pOut->mPart != nullptr;
    }

    Built gBox, gSphere;
    bool gArchive = false;  // the parts come from the baked mod
    const char* gFailure = nullptr;
    bool gCreated = false;
    s32 gFrame = 0;

    struct OnPart {
        s32 frames = 0;      // grounded on the part
        s32 landings = 0;    // airborne, then grounded on the part
        f32 travelled = 0.0f;  // path length while grounded on it
        TVec3f last;
        bool wasOn = false;
        u32 sound = 0xFFFF, floor = 0xFFFF;
        TVec3f gravity;
    };
    OnPart gOnBox, gOnSphere;
    bool gAirborne = false;

    void fail(const char* pReason) {
        if (gFailure == nullptr) {
            gFailure = pReason;
            std::fprintf(stderr, "[editor-terrain] FAIL: %s\n", pReason);
        }
    }

    void track(OnPart* pOn, const TVec3f& rPos) {
        if (pOn->wasOn) {
            pOn->travelled += PSVECDistance(&pOn->last, &rPos);
        }
        if (gAirborne) {
            pOn->landings++;
        }
        pOn->frames++;
        pOn->last = rPos;
        pOn->wasOn = true;
        pOn->gravity = *MR::getPlayerGravity();
        Triangle* pTri = MR::getPlayerGroundingPolygon();
        JMapInfoIter attributes = pTri->getAttributes();
        attributes.getValue< u32 >("Sound_code", &pOn->sound);
        attributes.getValue< u32 >("Floor_code", &pOn->floor);
    }

    // PETARI_EDITOR_TERRAIN_SHOTS=<dir>: screenshots after 90 and 420 frames on a part.
    void maybeShoot(const char* pShape, const OnPart& rOn) {
        const char* pDir = std::getenv("PETARI_EDITOR_TERRAIN_SHOTS");
        if (pDir == nullptr || (rOn.frames != 90 && rOn.frames != 420)) {
            return;
        }
        char file[512];
        snprintf(file, sizeof(file), "%s/%s-%s-%d.png", pDir, gArchive ? "archive" : "live", pShape, rOn.frames);
        PetariNative::Screenshot::request(file);
    }

    void report(const char* pName, const OnPart& rOn) {
        std::fprintf(stderr,
                     "[editor-terrain] on the %s: %d frames grounded, %d landings, %.0f units walked; gravity (%.2f, %.2f, %.2f); "
                     "floor code %u, sound code %u\n",
                     pName, rOn.frames, rOn.landings, rOn.travelled, rOn.gravity.x, rOn.gravity.y, rOn.gravity.z, rOn.floor, rOn.sound);
    }
}  // namespace

namespace EditorTerrainTest {
    void update(bool actionNerve) {
        static const bool sOn = [] {
            const char* p = std::getenv("PETARI_EDITOR_TERRAIN_TEST");
            gArchive = p != nullptr && std::strcmp(p, "archive") == 0;
            // Archive mode only watches what placement made: it runs with the editor off too.
            return p != nullptr && ((p[0] == '1' && PetariEditor::Hooks::enabled()) || gArchive);
        }();
        if (!sOn || gFailure != nullptr || !actionNerve) {
            return;
        }
        const char* pError = "";
        if (!gCreated) {
            // Only on the smoke's stage (FileSelect is a GameScene too), a few frames in: before
            // the smoke's test warp, which waits for 60 frames of pausing permitted.
            static s32 sStageFrames = 0;
            const char* pStage = std::getenv("PETARI_STAGE");
            const char* pCurrent = EditorScene::stageName();
            if (pStage == nullptr || pCurrent == nullptr || std::strcmp(pStage, pCurrent) != 0 || ++sStageFrames < 10) {
                return;
            }
            gCreated = true;
            if (gArchive) {
                return;  // placement made them
            }
            // Box: the top face sounds like stone (Sound_code 3); sides and bottom are default.
            T::Mesh box = T::makeBox({0, 0, 0}, {kBoxHalf.x, kBoxHalf.y, kBoxHalf.z});
            T::Surface stone;
            stone.sound = T::soundCode("Stone");
            box.surfaces.push_back(stone);
            for (std::size_t t = 0; t < box.triangleCount(); t++) {
                if (box.normals[box.indices[t * 3]].y > 0.5f) {
                    box.triangleSurface[t] = 1;
                }
            }
            T::Surface grass;
            grass.sound = T::soundCode("Lawn");
            const T::Mesh sphere = T::makeSphere({0, 0, 0}, kSphereRadius, 24, 32, grass);
            if (!build(box, "EditorTerrainBox", kBoxCenter, &gBox, &pError) ||
                !build(sphere, "EditorTerrainSphere", kSphereCenter, &gSphere, &pError)) {
                fail(pError);
                return;
            }
            const Look checker = {"/ObjectData/BathtubPlanet.arc", "bathtubplanet.bdl", "CheckMat_v"};
            const Look soil = {"/ObjectData/BeeWallClimbPlanetA.arc", "beewallclimbplaneta.bdl", "Soil06_v"};
            if (!buildModel(box, "EditorTerrainBox", checker, &gBox, &pError) || !buildModel(sphere, "EditorTerrainSphere", soil, &gSphere, &pError)) {
                fail(pError);
                return;
            }
            // Gravity fitted to the brushes, above the galaxy's own priorities, reaching 2000
            // units past their surfaces.
            EditorTerrain::addCubeGravity(kBoxCenter, kBoxHalf, 2000.0f, 1000);
            EditorTerrain::addPointGravity(kSphereCenter, kSphereRadius, 2000.0f, 1000);
            std::fprintf(stderr, "[editor-terrain] gravity fields: %d registered\n", EditorScene::gravityCount());
            return;
        }

        gFrame++;
        const TVec3f pos = *MR::getPlayerPos();
        Triangle* pGround = MR::isOnGroundPlayer() ? MR::getPlayerGroundingPolygon() : nullptr;
        CollisionParts* pParts = pGround != nullptr ? pGround->mParts : nullptr;
        bool onBox, onSphere;
        if (gArchive) {
            // The baked planets (EditorBrush<hash>), by the actor owning the collision; the box is
            // the one near z = 30000, the sphere near z = 40000.
            const char* pHost = pParts != nullptr && pParts->mHitSensor != nullptr ? pParts->mHitSensor->mHost->mName : "";
            const bool brush = std::strncmp(pHost, "EditorBrush", 11) == 0;
            onBox = brush && pos.z < 35000.0f;
            onSphere = brush && pos.z >= 35000.0f;
        } else {
            onBox = pParts != nullptr && pParts == gBox.mPart->mCollisionParts;
            onSphere = pParts != nullptr && pParts == gSphere.mPart->mCollisionParts;
        }
        if (onBox) {
            track(&gOnBox, pos);
            maybeShoot("box", gOnBox);
            gOnSphere.wasOn = false;
        } else if (onSphere) {
            track(&gOnSphere, pos);
            maybeShoot("sphere", gOnSphere);
            gOnBox.wasOn = false;
        } else {
            gOnBox.wasOn = gOnSphere.wasOn = false;
        }
        gAirborne = !MR::isOnGroundPlayer();
        if (gFrame % 300 == 0) {
            std::fprintf(stderr, "[editor-terrain] frame %d: Mario at (%.0f, %.0f, %.0f), %s\n", gFrame, pos.x, pos.y, pos.z,
                         pParts == nullptr ? "not on ground" : onBox ? "on the box" : onSphere ? "on the sphere" : "on other ground");
        }

        if (PetariNative::SmokePhase::stageChecksDone.load()) {
            report("box", gOnBox);
            report("sphere", gOnSphere);
            const OnPart& rOn = gOnBox.frames >= gOnSphere.frames ? gOnBox : gOnSphere;
            const bool box = &rOn == &gOnBox;
            const TVec3f up = box ? TVec3f(0.0f, 1.0f, 0.0f) : TVec3f(rOn.last.x - kSphereCenter.x, rOn.last.y - kSphereCenter.y, rOn.last.z - kSphereCenter.z);
            const f32 upLength = PSVECMag(&up);
            const f32 alignment = upLength > 0.0f ? -(rOn.gravity.x * up.x + rOn.gravity.y * up.y + rOn.gravity.z * up.z) / upLength : 0.0f;
            if (rOn.frames < 120) {
                fail("Mario never stood on a generated part for 120 frames");
            } else if (rOn.travelled < 300.0f) {
                fail("Mario did not walk on the generated part");
            } else if (alignment < 0.95f) {
                fail("Mario's gravity on the part is not the fitted field");
            } else if (rOn.sound != static_cast< u32 >(box ? T::soundCode("Stone") : T::soundCode("Lawn"))) {
                fail("the part's surface (.pa) did not reach Mario's ground");
            } else {
                std::fprintf(stderr, "[editor-terrain] PASS: walked %.0f units on the %s over %d frames (%d landings)\n", rOn.travelled,
                             box ? "box" : "sphere", rOn.frames, rOn.landings);
                gFailure = "done";
            }
        }
    }
}  // namespace EditorTerrainTest
#endif
