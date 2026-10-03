// Live check of New Galaxy (native/tools/editor_new_galaxy_smoke.py), across the restart it makes
// (=create-box: the same with the Brushes template, "Smoke Box"):
// with PETARI_EDITOR=1 and PETARI_EDITOR_NEWGALAXY_TEST=create, once the Good Egg stage smoke's checks
// are done it asks for "Smoke Hop" (FlowerPlanet with its gravity, two missions) and flips the test to
// =verify, which the restarted process inherits (the same process image, execv). There, once the
// stage smoke's checks are done in the new galaxy, it checks the stage, the registry's names, the
// PETARI_OPEN_* hand-over and that the mod is on. Logs "[editor-newgalaxy] PASS" or "... FAIL: <why>".
#include "runtime_core.hpp"

#include <petari/editor/runtime.hpp>

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

    // After the restart's checks: the galaxy's Checks (galaxy_checks.cpp). None for a galaxy made
    // whole; "start in mid-air" once the start is moved 50,000 units up.
    int gChecksFrame = 0;

    void checksTick() {
        PetariNative::HostAllocationScope host;
        const int frame = ++gChecksFrame;
        if (frame == 1) {
            if (!ensureModel()) {
                return finish("FAIL: no model for the new galaxy");
            }
            return;
        }
        auto galaxyChecks = [] {
            std::string keys;
            for (const Check& c : checks()) {
                if (c.key.rfind("galaxy-", 0) == 0) {
                    keys += (keys.empty() ? "" : ", ") + c.key + " (" + c.message + ")";
                }
            }
            return keys;
        };
        if (frame == 40) {
            if (const std::string keys = galaxyChecks(); !keys.empty()) {
                return finish("FAIL: a new galaxy made whole has galaxy checks: " + keys);
            }
            const ModelView* m = model();
            RowId start;
            for (int zone = 0; m != nullptr && zone < m->zoneCount(); zone++) {
                for (int table = 0; table < m->tableCount(zone); table++) {
                    const std::string path = m->tablePath(zone, table);
                    if (path.size() > 10 && path.compare(path.size() - 10, 10, "/startinfo") == 0) {
                        m->forEachRow(zone, table, [&](RowId row) {
                            if (!start.valid()) start = row;
                        });
                    }
                }
            }
            if (!start.valid()) {
                return finish("FAIL: no start row in the model");
            }
            Command move{Command::Kind::Apply};
            move.op.label = "Start up in the air";
            move.op.sets.push_back(SetCell{start, columnHash("pos_y"), Cell{CellType::Float, 0, m->transform(start).pos.y + 50000.0f, {}}});
            enqueue(std::move(move));
        }
        if (frame == 80) {
            const std::string keys = galaxyChecks();
            if (keys.find("galaxy-start-midair") == std::string::npos) {
                return finish("FAIL: a start 50,000 units up is not reported in mid-air (" + keys + ")");
            }
            finish("PASS (checks: none when whole; then " + keys + ")");
        }
    }
}  // namespace

void newGalaxyTestTick() {
    static const std::string sMode = [] {
        const char* p = std::getenv("PETARI_EDITOR_NEWGALAXY_TEST");
        return std::string(p != nullptr ? p : "");
    }();
    static bool sDone = false;
    if (gChecksFrame > 0) {
        return checksTick();
    }
    if (sMode == "edit-check" && !sDone) {
        // A player's restart (no fixture, no stage checks): once in the galaxy, the editor must be
        // in Edit by itself (PlayerLaunch::openEdit, the UI's entry) within ten seconds.
        static int sFrames = 0;
        const char* pStage = EditorScene::stageName();
        if (pStage == nullptr || std::strcmp(pStage, kHop.internal) != 0) {
            return;
        }
        if (mode() == Mode::Editing) {
            sDone = true;
            std::fprintf(stderr, "[editor-newgalaxy] PASS: in Edit in %s after %d frames, by itself\n", pStage, sFrames);
            return;
        }
        if (++sFrames >= 600) {
            sDone = true;
            std::fprintf(stderr, "[editor-newgalaxy] FAIL: not in Edit 600 frames into %s after the restart\n", pStage);
        }
        return;
    }
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
    if (PetariNative::PlayerLaunch::openTitle != kName) {  // (openEdit is the player's run: edit-check)
        return finish("FAIL: PETARI_OPEN_TITLE did not reach the restarted game");
    }
    if (std::getenv("PETARI_OPEN_STAGE") != nullptr) {
        return finish("FAIL: PETARI_OPEN_STAGE was not cleared (a later restart would repeat it)");
    }
    std::fprintf(stderr, "[editor-newgalaxy] restart checks pass; now the galaxy's Checks\n");
    gChecksFrame = 0;
    checksTick();  // frame 1: the model
}

}  // namespace PetariEditor::Runtime
