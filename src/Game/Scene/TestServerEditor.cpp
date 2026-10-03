#ifdef PETARI_NATIVE
// The test server's live-editing commands (docs/dev/level-editor/AGENT_API.md "Live mode"): the agent
// API's low-level ops applied to the running game through the editor's own command queue, plus the
// verification commands (exact ground and gravity, Mario, screenshots, waits). Test-only, like the server.
#include "Game/Scene/TestServerEditor.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include <revolution/mtx.h>
#include <petari/editor/edit_api.hpp>
#include <petari/editor/level_api.hpp>
#include <petari/editor/runtime.hpp>
#include <petari/host_allocation.hpp>
#include <petari/screenshot.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;
namespace E = PetariEditor;
using E::Json::Value;

namespace {
    // Scene-outliving state is POD (see TestServer.cpp).
    enum class Wait : s32 { None, Model, Drain, Reload, Play, Shot, Frames };
    struct Pending {
        Wait wait = Wait::None;
        char name[64] = {};
        char path[1024] = {};
        u64 revision = 0;
        u64 errorSerial = 0;
        u64 noticeSerial = 0;
        s32 frames = 0;
        s32 count = 0;
        double since = 0;
    };
    Pending gPending;
    struct BaseIdentity {
        const E::ModelView* model = nullptr;
        char galaxy[64] = {};
        char fingerprint[17] = {};
        int mission = 0;
        int history = 0;
        u64 revision = 0;
    };
    BaseIdentity gBase;

    void observeBase() {
        const E::ModelView* model = E::model();
        if (model == nullptr) {
            gBase = BaseIdentity{};
            return;
        }
        if (gBase.model != model || std::strcmp(gBase.galaxy, model->galaxy()) != 0 || gBase.mission != model->scenario() ||
            model->revision() < gBase.revision || (model->historySize() == 0 && gBase.history != 0)) {
            // Retain the initial content through edits/reloads; capture a new model before its first API op.
            PetariNative::HostAllocationScope host;
            const std::string fingerprint = E::LevelApi::modelFingerprint(*model);
            gBase.model = model;
            gBase.mission = model->scenario();
            std::snprintf(gBase.galaxy, sizeof(gBase.galaxy), "%s", model->galaxy());
            std::snprintf(gBase.fingerprint, sizeof(gBase.fingerprint), "%s", fingerprint.c_str());
        }
        gBase.history = model->historySize();
        gBase.revision = model->revision();
    }

    double nowMs() {
        return std::chrono::duration< double, std::milli >(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    std::string vec(const TVec3f& v) {
        char text[96];
        std::snprintf(text, sizeof(text), "[%.1f,%.1f,%.1f]", v.x, v.y, v.z);
        return text;
    }

    std::string marioJson() {
        if (!MR::isExistSceneObj(SceneObj_MarioHolder)) return "{\"present\":false}";
        const TVec3f* p = MR::getPlayerPos();
        const TVec3f* g = MR::getPlayerGravity();
        std::string out = "{\"present\":true,\"pos\":" + vec(*p);
        if (g != nullptr) out += ",\"gravity\":" + vec(*g);
        out += std::string(",\"on_ground\":") + (MR::isOnGroundPlayer() ? "true" : "false");
        out += std::string(",\"dead\":") + (MR::isPlayerDead() ? "true" : "false") + "}";
        return out;
    }

    std::string modelJson() {
        observeBase();
        const E::ModelView* m = E::model();
        if (m == nullptr) return "{\"model\":false}";
        static const char* const kModes[] = {"off", "editing", "reloading", "playing"};
        char text[512];
        std::snprintf(text, sizeof(text), "{\"model\":true,\"galaxy\":\"%s\",\"mission\":%d,\"revision\":%llu,\"history\":%d,\"cursor\":%d,\"mode\":\"%s\",\"pending\":%d,\"base_fingerprint\":\"%s\"}",
                      m->galaxy(), m->scenario(), static_cast< unsigned long long >(m->revision()), m->historySize(), m->historyCursor(),
                      kModes[static_cast< int >(E::mode())], E::status().pendingCount, gBase.fingerprint);
        return text;
    }

    // "ok <json>" / "error <text>" (the server's protocol; the JSON is for petari_level_api's live client).
    void begin(Wait wait, const std::string& name) {
        gPending = Pending{};
        gPending.wait = wait;
        std::snprintf(gPending.name, sizeof(gPending.name), "%s", name.c_str());
        gPending.since = nowMs();
        if (const E::ModelView* m = E::model()) gPending.revision = m->revision();
        gPending.errorSerial = E::status().errorSerial;
        gPending.noticeSerial = E::status().noticeSerial;
    }

    // "error <json>": the API's Error ({code, message, hint, candidates, where}), as every live command replies.
    std::string errorText(const std::string& code, const std::string& message, const std::string& hint = "", const std::string& where = "") {
        PetariNative::HostAllocationScope host;
        return "error " + E::Json::write(E::LevelApi::Error{code, message, hint, {}, where}.toJson());
    }

    bool parseFloats(const std::vector< std::string >& w, std::size_t from, float* out, int n) {
        if (w.size() < from + n) return false;
        for (int i = 0; i < n; i++) out[i] = std::strtof(w[from + i].c_str(), nullptr);
        return true;
    }
}  // namespace

namespace TestServerEditor {

bool run(const std::string& name, const std::vector< std::string >& w, bool playable, const Reply& reply) {
    (void)playable;
    const std::string& c = w[0];
    if (c == "editor-begin") {
        // The edit model for the current stage without entering Edit (Runtime::ensureModel): ops then apply on the
        // next tick and SceneSync re-creates their actors in the running scene, in Play.
        if (E::model() == nullptr && !PetariEditor::Runtime::ensureModel()) {
            reply(name, errorText("no-model", "no edit model: " + E::status().lastError, "is a stage loaded? (status)", c));
            return true;
        }
        begin(Wait::Model, name);
        return true;
    }
    if (c == "editor-state") {
        reply(name, "ok {\"editor\":" + modelJson() + ",\"mario\":" + marioJson() + "}");
        return true;
    }
    if (c == "mario") {
        reply(name, "ok " + marioJson());
        return true;
    }
    if (c == "editor-apply" && w.size() >= 2) {
        // A file with one low-level op or {"ops": [op, ...]} (petari-level export).
        const E::ModelView* m = E::model();
        if (m == nullptr) {
            reply(name, errorText("no-model", "no edit model", "send editor-begin first", c));
            return true;
        }
        PetariNative::HostAllocationScope host;
        std::ifstream in(w[1], std::ios::binary);
        const std::string text((std::istreambuf_iterator< char >(in)), {});
        std::string error;
        std::optional< Value > json = E::Json::parse(text, &error);
        if (!json) {
            reply(name, errorText("bad-json", w[1] + ": " + error, "", c));
            return true;
        }
        std::vector< Value > ops;
        if (json->has("ops")) {
            for (const Value& op : (*json)["ops"].items()) ops.push_back(op);
        } else {
            ops.push_back(*json);
        }
        std::vector< E::Op > parsed;
        for (const Value& op : ops) {
            E::Op one;
            if (!E::LevelApi::opFromJson(*m, op, &one, &error)) {
                reply(name, errorText("bad-ref", error, "the game's model must match the workspace (same disc and mods; editor-state)", "ops[" + std::to_string(parsed.size()) + "]"));
                return true;
            }
            parsed.push_back(std::move(one));
        }
        for (E::Op& op : parsed) {
            E::Command command{E::Command::Kind::Apply};
            command.op = std::move(op);
            E::enqueue(std::move(command));
        }
        begin(Wait::Drain, name);
        gPending.count = static_cast< s32 >(parsed.size());
        return true;
    }
    if (c == "editor-undo" || c == "editor-redo") {
        const int n = w.size() >= 2 ? std::max(1, std::atoi(w[1].c_str())) : 1;
        for (int i = 0; i < n; i++) E::enqueue(E::Command{c == "editor-undo" ? E::Command::Kind::Undo : E::Command::Kind::Redo});
        begin(Wait::Drain, name);
        gPending.count = n;
        return true;
    }
    if (c == "editor-reload") {
        E::enqueue(E::Command{E::Command::Kind::Reload});
        begin(Wait::Reload, name);
        return true;
    }
    if (c == "editor-play") {
        // editor-play [x y z [ux uy uz [fx fy fz]]]: Play from a marker there (Mario teleported, upright for up),
        // reloading first when edits are pending; without a point, from the existing marker.
        float p[9] = {0, 0, 0, 0, 1, 0, 0, 0, 1};
        const bool at = parseFloats(w, 1, p, 3);
        if (at) {
            parseFloats(w, 4, p + 3, 3);
            parseFloats(w, 7, p + 6, 3);
            E::Command marker{E::Command::Kind::SetPlayMarker};
            marker.markerPos = {p[0], p[1], p[2]};
            marker.markerUp = {p[3], p[4], p[5]};
            marker.markerFront = {p[6], p[7], p[8]};
            E::enqueue(std::move(marker));
        }
        E::Command play{E::Command::Kind::Play};
        play.from = E::PlayFrom::Marker;
        E::enqueue(std::move(play));
        begin(Wait::Play, name);
        return true;
    }
    if (c == "teleport") {
        // teleport x y z: Mario there now (MR::setPlayerPosAndWait, as the stage smoke's warp), without the editor.
        float p[3];
        if (!parseFloats(w, 1, p, 3) || !MR::isExistSceneObj(SceneObj_MarioHolder)) {
            reply(name, errorText("bad-args", "usage: teleport x y z (in a stage)", "", c));
            return true;
        }
        MR::setPlayerPosAndWait(TVec3f(p[0], p[1], p[2]));
        begin(Wait::Frames, name);
        gPending.frames = 2;
        return true;
    }
    if (c == "editor-save" && w.size() >= 2) {
        E::Command save{E::Command::Kind::SaveMod};
        for (std::size_t i = 1; i < w.size(); i++) {
            if (w[i] == "--full") {
                save.fullArchives = true;
            } else {
                save.modName += (save.modName.empty() ? "" : " ") + w[i];
            }
        }
        E::enqueue(std::move(save));
        begin(Wait::Drain, name);
        gPending.count = -1;  // done on a notice or an error
        return true;
    }
    if (c == "editor-checks") {
        PetariNative::HostAllocationScope host;
        const E::ModelView* m = E::model();
        if (m == nullptr) {
            reply(name, errorText("no-model", "no edit model", "send editor-begin first", c));
            return true;
        }
        Value list = Value::array();
        for (const E::Check& check : E::checks()) {
            Value j = Value::object();
            j.set("key", check.key);
            j.set("severity", check.severity == E::Severity::Error ? "error" : "warning");
            j.set("message", check.message);
            Value rows = Value::array();
            for (E::RowId row : check.rows) rows.push(E::LevelApi::rowRef(*m, row));
            j.set("rows", std::move(rows));
            list.push(std::move(j));
        }
        reply(name, "ok " + E::Json::write(list));
        return true;
    }
    if (c == "ground" || c == "gravity") {
        // ground x y z [dx dy dz]: the engine's own gravity at the point and the first map polygon along it.
        float p[6] = {0, 0, 0, 0, 0, 0};
        if (!parseFloats(w, 1, p, 3)) {
            reply(name, errorText("bad-args", "usage: " + c + " x y z [dx dy dz]", "", c));
            return true;
        }
        const TVec3f at(p[0], p[1], p[2]);
        TVec3f gravity(0.0f, 0.0f, 0.0f);
        const bool covered = MR::calcGravityVectorOrZero(nullptr, at, &gravity, nullptr, 0) && PSVECMag(&gravity) > 1e-4f;
        TVec3f down = gravity;
        if (parseFloats(w, 4, p + 3, 3)) down = TVec3f(p[3], p[4], p[5]);
        std::string out = "{\"covered\":" + std::string(covered ? "true" : "false") + ",\"gravity\":" + vec(gravity);
        if (c == "ground") {
            if (PSVECMag(&down) < 1e-4f) down = TVec3f(0.0f, -1.0f, 0.0f);
            PSVECNormalize(&down, &down);
            TVec3f hit;
            Triangle triangle;
            const TVec3f ray(down.x * 50000.0f, down.y * 50000.0f, down.z * 50000.0f);
            if (MR::getFirstPolyOnLineToMap(&hit, &triangle, at, ray)) {
                out += ",\"hit\":true,\"point\":" + vec(hit);
                if (const TVec3f* n = triangle.getFaceNormal()) out += ",\"normal\":" + vec(*n);
            } else {
                out += ",\"hit\":false";
            }
        }
        reply(name, "ok " + out + "}");
        return true;
    }
    if (c == "screenshot" && w.size() >= 2) {
        // The next presented frame (no ImGui overlay) as PNG.
        std::error_code ec;
        fs::remove(w[1], ec);
        if (!PetariNative::Screenshot::request(w[1].c_str())) {
            reply(name, errorText("busy", "a screenshot is already pending", "retry after it is written", c));
            return true;
        }
        begin(Wait::Shot, name);
        std::snprintf(gPending.path, sizeof(gPending.path), "%s", w[1].c_str());
        return true;
    }
    if (c == "wait" && w.size() >= 2) {
        begin(Wait::Frames, name);
        gPending.frames = std::max(1, std::atoi(w[1].c_str()));
        return true;
    }
    return false;
}

bool tick(bool playable, const Reply& reply) {
    observeBase();
    if (gPending.wait == Wait::None) return false;
    const double elapsed = nowMs() - gPending.since;
    const E::Status& s = E::status();
    auto done = [&](const std::string& text) {
        reply(gPending.name, text);
        gPending.wait = Wait::None;
    };
    if (elapsed > 60000) {
        done(errorText("timeout", "no result within 60 s", "see the server log"));
        return true;
    }
    switch (gPending.wait) {
    case Wait::Model:
        if (E::model() != nullptr) done("ok " + modelJson());
        else if (s.errorSerial != gPending.errorSerial) done(errorText("editor-error", s.lastError));
        break;
    case Wait::Drain:
        // The queue drains at the next seam; an error bumps errorSerial, a save sets a notice.
        if (s.errorSerial != gPending.errorSerial) {
            done(errorText("editor-error", s.lastError));
        } else if (gPending.count < 0 ? s.noticeSerial != gPending.noticeSerial : (E::model() != nullptr && E::model()->revision() != gPending.revision)) {
            done("ok {\"notice\":\"" + (gPending.count < 0 ? s.notice : std::string()) + "\",\"editor\":" + modelJson() + "}");
        } else if (++gPending.frames > 30 && gPending.count >= 0) {
            done("ok {\"note\":\"no revision change\",\"editor\":" + modelJson() + "}");
        }
        break;
    case Wait::Reload:
        if (!PetariEditor::Runtime::reloading() && E::mode() != E::Mode::Reloading && ++gPending.frames > 2) {
            char text[64];
            std::snprintf(text, sizeof(text), ",\"ms\":%.0f}", elapsed);
            done("ok {\"editor\":" + modelJson() + text);
        }
        break;
    case Wait::Play:
        gPending.frames = (playable && E::mode() == E::Mode::Playing) ? gPending.frames + 1 : 0;
        if (gPending.frames >= 30) done("ok {\"mario\":" + marioJson() + ",\"editor\":" + modelJson() + "}");
        else if (s.errorSerial != gPending.errorSerial) done(errorText("editor-error", s.lastError));
        break;
    case Wait::Shot: {
        std::error_code ec;
        if (!PetariNative::Screenshot::pending.load() && fs::exists(gPending.path, ec) && fs::file_size(gPending.path, ec) > 0) {
            done(std::string("ok {\"path\":\"") + gPending.path + "\"}");
        }
        break;
    }
    case Wait::Frames:
        if (--gPending.frames <= 0) done("ok {\"mario\":" + marioJson() + "}");
        break;
    default:
        break;
    }
    return true;
}

}  // namespace TestServerEditor
#endif
