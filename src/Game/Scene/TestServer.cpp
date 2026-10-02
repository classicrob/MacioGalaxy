#ifdef PETARI_NATIVE
#include "Game/Scene/TestServer.hpp"
#include "Game/Editor/EditorScene.hpp"
#include "Game/LiveActor/AllLiveActorGroup.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include <petari/editor/runtime.hpp>
#include <petari/editor/runtime_hooks.hpp>
#include <petari/player_launch.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {
    // State that outlives a scene must not live in std containers: the game's allocator can put their buffers on the
    // scene heap, which a scene change reclaims (the server answered at the title and never again in the stage). POD only.
    bool gChecked = false;
    char gDirText[1024] = {};
    bool gEnabled = false;
    fs::path dir() {
        return fs::path(gDirText);
    }

    // A command in flight: warp/reload reply when the new scene is playable again.
    struct Pending {
        char name[64] = {};
        char what[160] = {};  // "warp Stage n" / "reload"
        char stage[64] = {};
        s32 scenario = 0;
        double since = 0;
        s32 serialBefore = 0;
        bool leftScene = false;  // the old scene went away (not in normal play) at least once
        bool isReload = false;   // done when PetariEditor::Runtime::reloading() clears
    };
    bool gHavePending = false;
    Pending gPending;
    s32 gFramesPlayable = 0;

    double nowMs() {
        return std::chrono::duration< double, std::milli >(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void reply(const std::string& name, const std::string& text) {
        const fs::path tmp = dir() / "reply" / (name + ".tmp");
        {
            std::ofstream out(tmp);
            out << text << "\n";
        }
        fs::rename(tmp, dir() / "reply" / (name + ".txt"));
        std::fprintf(stderr, "[test-server] reply %s: %s\n", name.c_str(), text.c_str());
    }

    std::string statusText() {
        const char* stage = EditorScene::stageName();
        std::ostringstream out;
        out << "stage=" << (stage != nullptr ? stage : "-") << " scenario=" << EditorScene::scenario()
            << " actors=" << EditorScene::liveActorCount() << "/" << EditorScene::liveActorLimit();
        if (MR::isExistSceneObj(SceneObj_MarioHolder)) {
            const TVec3f* p = MR::getPlayerPos();
            char buf[96];
            std::snprintf(buf, sizeof(buf), " mario=(%.0f,%.0f,%.0f)", p->x, p->y, p->z);
            out << buf;
        }
        return out.str();
    }

    std::vector< std::string > split(const std::string& line) {
        std::istringstream in(line);
        std::vector< std::string > words;
        for (std::string w; in >> w;) {
            words.push_back(w);
        }
        return words;
    }

    // Runs one command. Returns true if it finished (reply written), false if it is now pending.
    bool run(const std::string& name, const std::string& line, bool playable) {
        const std::vector< std::string > w = split(line);
        if (w.empty()) {
            reply(name, "error empty command");
            return true;
        }
        if (w[0] == "status") {
            reply(name, "ok " + statusText() + (playable ? " playable" : " not-playable"));
            return true;
        }
        if (w[0] == "quit") {
            reply(name, "ok bye");
            std::fflush(stderr);
            _exit(0);
        }
        if (!playable) {
            reply(name, "error not in normal play (retry)");
            return true;
        }
        if (w[0] == "warp" && w.size() >= 2) {
            gPending = Pending{};
            std::snprintf(gPending.name, sizeof(gPending.name), "%s", name.c_str());
            std::snprintf(gPending.what, sizeof(gPending.what), "%s", line.c_str());
            std::snprintf(gPending.stage, sizeof(gPending.stage), "%s", w[1].c_str());
            gPending.scenario = w.size() >= 3 ? std::atoi(w[2].c_str()) : 1;
            gPending.since = nowMs();
            gPending.serialBefore = static_cast< s32 >(PetariEditor::Hooks::sceneSerial());
            std::snprintf(PetariNative::PlayerLaunch::warpStage, sizeof(PetariNative::PlayerLaunch::warpStage), "%s", w[1].c_str());
            PetariNative::PlayerLaunch::warpScenario = gPending.scenario;
            PetariNative::PlayerLaunch::warpPending.store(true);  // GameScene::update does the galaxy move when pause is permitted
            gHavePending = true;
            gFramesPlayable = 0;
            return false;
        }
        if (w[0] == "reload") {
            const bool fromStart = w.size() >= 2 && w[1] == "start";
            if (!PetariEditor::Runtime::reloadScene(fromStart)) {
                reply(name, "error a reload is in flight or no stage is loaded");
                return true;
            }
            gPending = Pending{};
            std::snprintf(gPending.name, sizeof(gPending.name), "%s", name.c_str());
            std::snprintf(gPending.what, sizeof(gPending.what), "%s", line.c_str());
            std::snprintf(gPending.stage, sizeof(gPending.stage), "%s", EditorScene::stageName() != nullptr ? EditorScene::stageName() : "");
            gPending.since = nowMs();
            gPending.isReload = true;
            gHavePending = true;
            gFramesPlayable = 0;
            return false;
        }
        reply(name, "error unknown command: " + line);
        return true;
    }
}  // namespace

namespace TestServer {
    bool active() {
        return gEnabled;
    }

    void update(bool actionNerve) {
        if (!gChecked) {
            gChecked = true;
            const char* pDir = std::getenv("PETARI_TEST_SERVER");
            gEnabled = pDir != nullptr && pDir[0] != '\0';
            if (gEnabled) {
                std::snprintf(gDirText, sizeof(gDirText), "%s", pDir);
                fs::create_directories(dir() / "cmd");
                fs::create_directories(dir() / "reply");
                std::fprintf(stderr, "[test-server] listening on %s\n", pDir);
            }
        }
        if (!gEnabled) {
            return;
        }
        static int sBeat = 0;
        if (++sBeat % 300 == 1) {
            std::fprintf(stderr, "[test-server] alive: beat %d action %d pending %d stage %s\n", sBeat, actionNerve, gHavePending,
                         EditorScene::stageName() != nullptr ? EditorScene::stageName() : "-");
        }
        const bool playable = actionNerve && MR::isExistSceneObj(SceneObj_MarioHolder) && !MR::isDemoActive() && !MR::isPlayerDead();
        if (gHavePending && gPending.isReload) {
            if (!PetariEditor::Runtime::reloading()) {
                char text[160];
                std::snprintf(text, sizeof(text), "ok %s in %.0f ms", statusText().c_str(), nowMs() - gPending.since);
                reply(gPending.name, text);
                gHavePending = false;
            } else if (nowMs() - gPending.since > 60000) {
                reply(gPending.name, "error timeout waiting for the reload");
                gHavePending = false;
            }
            return;
        }
        if (gHavePending) {
            const s32 serial = static_cast< s32 >(PetariEditor::Hooks::sceneSerial());
            gPending.leftScene = gPending.leftScene || serial != gPending.serialBefore || !actionNerve;
            gFramesPlayable = (gPending.leftScene && playable) ? gFramesPlayable + 1 : 0;
            const char* stage = EditorScene::stageName();
            if (gFramesPlayable >= 30 && stage != nullptr && std::strcmp(gPending.stage, stage) == 0) {
                char text[160];
                std::snprintf(text, sizeof(text), "ok %s in %.0f ms", statusText().c_str(), nowMs() - gPending.since);
                reply(gPending.name, text);
                gHavePending = false;
            } else if (nowMs() - gPending.since > 120000) {
                reply(gPending.name, std::string("error timeout waiting for ") + gPending.what);
                gHavePending = false;
            }
            return;
        }
        // One command per frame, in name order.
        static int sPollCounter = 0;
        if (++sPollCounter % 6 != 0) {
            return;
        }
        std::vector< fs::path > files;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(dir() / "cmd", ec)) {
            if (entry.path().extension() == ".txt") {
                files.push_back(entry.path());
            }
        }
        if (files.empty()) {
            return;
        }
        std::sort(files.begin(), files.end());
        const fs::path file = files.front();
        std::string line;
        {
            std::ifstream in(file);
            std::getline(in, line);
        }
        fs::remove(file, ec);
        const std::string name = file.stem().string();
        std::fprintf(stderr, "[test-server] command %s: %s\n", name.c_str(), line.c_str());
        run(name, line, playable);
    }
}  // namespace TestServer
#endif
