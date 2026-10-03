// Live check of New Galaxy (native/tools/editor_new_galaxy_smoke.py), across the restart it makes
// (=create-box: the same with the Brushes template, "Smoke Box"):
// with PETARI_EDITOR=1 and PETARI_EDITOR_NEWGALAXY_TEST=create, once the Good Egg stage smoke's checks
// are done it asks for "Smoke Hop" (FlowerPlanet with its gravity, two missions) and flips the test to
// =verify, which the restarted process inherits (the same process image, execv). There, once the
// stage smoke's checks are done in the new galaxy, it checks the stage, the registry's names, the
// PETARI_OPEN_* hand-over and that the mod is on. Logs "[editor-newgalaxy] PASS" or "... FAIL: <why>".
#include "runtime_core.hpp"

#include <petari/host_allocation.hpp>
#include <petari/platform/mod_galaxy.hpp>
#include <petari/player_launch.hpp>
#include <petari/smoke_phase.hpp>

#include "Game/Editor/EditorScene.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace PetariEditor::Runtime {
namespace {
    // create: "Smoke Hop" (FlowerPlanet, two missions); create-box: "Smoke Box" (the Brushes template).
    struct Case {
        const char* name;
        const char* internal;
        NewGalaxyRequest::From from;
        const char* music;
    };
    const Case kHop{"Smoke Hop", "SmokeHopGalaxy", NewGalaxyRequest::From::OnePlanet, "FlowerGalaxy"};
    const Case kBox{"Smoke Box", "SmokeBoxGalaxy", NewGalaxyRequest::From::Brushes, "EggStarGalaxy"};

    void finish(const std::string& result) {
        std::fprintf(stderr, "[editor-newgalaxy] %s\n", result.c_str());
        PetariNative::SmokePhase::followDone.store(true);
    }
}  // namespace

void newGalaxyTestTick() {
    static const std::string sMode = [] {
        const char* p = std::getenv("PETARI_EDITOR_NEWGALAXY_TEST");
        return std::string(p != nullptr ? p : "");
    }();
    static bool sDone = false;
    if (sMode.empty() || sDone || !PetariNative::SmokePhase::stageChecksDone.load()) {
        return;
    }
    const bool box = sMode.find("box") != std::string::npos;
    const Case& c = box ? kBox : kHop;
    const char* const kName = c.name;
    const char* const kInternal = c.internal;
    sDone = true;
    PetariNative::HostAllocationScope host;  // test bookkeeping
    const char* pStage = EditorScene::stageName();
    if (sMode.rfind("create", 0) == 0) {
        NewGalaxyRequest request;
        request.name = kName;
        request.from = c.from;
        request.planet = "FlowerPlanet";
        request.musicFrom = c.music;
        request.missions = {"First Bloom", "Second Bloom"};
        request.discardUnsaved = true;
        if (newGalaxyInternalName(kName) != kInternal || !checkNewGalaxyName(kInternal).empty()) {
            return finish("FAIL: the name check: " + newGalaxyInternalName(kName) + " / " + checkNewGalaxyName(kInternal));
        }
        if (newGalaxyChoices().planets.empty() || newGalaxyChoices().galaxies.empty()) {
            return finish("FAIL: no choices from the mounted disc");
        }
        setenv("PETARI_EDITOR_NEWGALAXY_TEST", box ? "verify-box" : "verify", 1);  // for the restarted process
        Command command{Command::Kind::NewGalaxy};
        command.newGalaxy = request;
        enqueue(std::move(command));
        std::fprintf(stderr, "[editor-newgalaxy] create requested in %s (%zu planets, %zu galaxies to choose from)\n", pStage != nullptr ? pStage : "?",
                     newGalaxyChoices().planets.size(), newGalaxyChoices().galaxies.size());
        return;
    }
    namespace ModGalaxy = PetariNative::Platform::ModGalaxy;
    const ModGalaxy::Galaxy* pGalaxy = ModGalaxy::find(kInternal);
    std::fprintf(stderr, "[editor-newgalaxy] restarted into %s; open edit %d, title \"%s\"; registry %d (music %s)\n", pStage != nullptr ? pStage : "?",
                 PetariNative::PlayerLaunch::openEdit ? 1 : 0, PetariNative::PlayerLaunch::openTitle.c_str(), pGalaxy != nullptr,
                 pGalaxy != nullptr ? pGalaxy->music.c_str() : "-");
    if (pStage == nullptr || std::strcmp(pStage, kInternal) != 0) {
        return finish("FAIL: not in the new galaxy after the restart");
    }
    if (pGalaxy == nullptr || pGalaxy->music != c.music) {
        return finish("FAIL: the registry has no entry (or the wrong music) for the new galaxy");
    }
    if (!PetariNative::PlayerLaunch::openEdit || PetariNative::PlayerLaunch::openTitle != kName) {
        return finish("FAIL: PETARI_OPEN_EDIT / PETARI_OPEN_TITLE did not reach the restarted game");
    }
    if (std::getenv("PETARI_OPEN_STAGE") != nullptr) {
        return finish("FAIL: PETARI_OPEN_STAGE was not cleared (a later restart would repeat it)");
    }
    finish("PASS");
}

}  // namespace PetariEditor::Runtime
