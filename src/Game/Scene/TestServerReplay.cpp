#ifdef PETARI_NATIVE
#include "Game/Scene/TestServerReplay.hpp"
#include "Game/Camera/EditorCamera.hpp"
#include "Game/Editor/EditorScene.hpp"
#include "Game/Scene/TestServerInput.hpp"
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>
#include <petari/agent_replay.hpp>
#include <petari/editor/level_api.hpp>
#include <petari/host_allocation.hpp>

namespace E = PetariEditor;
using E::Json::Value;
namespace {
    Value routeStatus() {
        namespace R = PetariNative::App::AgentReplay;
        Value out = Value::object();
        out.set("active", R::active());
        out.set("finished", R::finished());
        out.set("passed", R::passed());
        out.set("reason", R::reason());
        out.set("frame", static_cast< double >(R::frame()));
        return out;
    }
    Value inputStatus() {
        const auto s = TestServerInput::status();
        Value out = Value::object();
        out.set("active", s.active);
        out.set("finished", s.finished);
        out.set("completed", s.completed);
        out.set("frame", static_cast< double >(s.frame));
        out.set("total", static_cast< double >(s.total));
        out.set("reason", s.reason);
        if (s.hasPlayer) {
            Value player = Value::object(), pos = Value::array(), vel = Value::array();
            for (int i = 0; i < 3; i++) {
                pos.push(s.pos[i]);
                vel.push(s.vel[i]);
            }
            player.set("pos", std::move(pos));
            player.set("velocity", std::move(vel));
            player.set("on_ground", s.ground);
            player.set("dead", s.dead);
            out.set("player", std::move(player));
        }
        return out;
    }
    bool integer(const Value& v, double lo, double hi) {
        const double n = v.asNumber();
        return v.type() == Value::Type::Number && std::isfinite(n) && n >= lo && n <= hi && std::floor(n) == n;
    }
}  // namespace
namespace TestServerReplay {
    bool controlsActive() {
        return PetariNative::App::AgentReplay::active() || TestServerInput::active();
    }

    bool run(const std::string& name, const std::vector< std::string >& w, bool playable, const TestServerEditor::Reply& reply) {
        if (w.empty())
            return false;
        const std::string& c = w[0];
        if (c != "verify-route" && c != "verify-route-status" && c != "verify-route-cancel" && c != "inputs-start" && c != "inputs-status" &&
            c != "inputs-cancel")
            return false;
        PetariNative::HostAllocationScope host;
        auto error = [&](const std::string& message, const std::string& where = "", const std::string& code = "bad-value",
                         const std::vector< std::string >& candidates = std::vector< std::string >{}) {
            E::LevelApi::Error e{code, message, "See `help --live verify-route` or `help --live inputs-start`.", candidates, where};
            reply(name, "error " + E::Json::write(e.toJson()));
            return true;
        };
        auto answer = [&](Value v) {
            v.set("ok", true);
            reply(name, "ok " + E::Json::write(v));
            return true;
        };
        if (c == "verify-route-status")
            return answer(routeStatus());
        if (c == "inputs-status")
            return answer(inputStatus());
        if (c == "verify-route-cancel") {
            PetariNative::App::AgentReplay::cancel();
            return answer(routeStatus());
        }
        if (c == "inputs-cancel") {
            TestServerInput::cancel();
            return answer(inputStatus());
        }
        if (!playable || EditorCamera::isActive())
            return error("Playback needs normal Play with a controllable Mario.", c, "not-playable");
        if (controlsActive())
            return error("Another route or input plan is running; cancel it first.", c, "busy");
        std::string path;
        for (std::size_t i = 1; i < w.size(); i++)
            path += (i == 1 ? "" : " ") + w[i];
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return error("Cannot read playback request: " + path, c, "bad-file");
        std::string parseError;
        auto json = E::Json::parse(std::string(std::istreambuf_iterator< char >(in), {}), &parseError);
        if (!json)
            return error(parseError, c, "bad-json");
        if (c == "verify-route") {
            if (!json->isObject())
                return error("Route must be {file, frames?, require_star?}.", c);
            for (const auto& [key, v] : json->members())
                if (key != "file" && key != "frames" && key != "require_star")
                    return error("Unknown route key: " + key, key, "bad-args", {"file", "frames", "require_star"});
            if ((*json)["file"].type() != Value::Type::String)
                return error("file must be a route CSV path.", "file");
            if (json->has("frames") && !integer((*json)["frames"], 60, 1080000))
                return error("frames must be an integer in 60..1080000.", "frames");
            if (json->has("require_star") && (*json)["require_star"].type() != Value::Type::Bool)
                return error("require_star must be boolean.", "require_star");
            std::string e;
            if (!PetariNative::App::AgentReplay::start((*json)["file"].asString(), EditorScene::stageName(), EditorScene::scenario(),
                                                       static_cast< unsigned long >((*json)["frames"].asNumber(18000)),
                                                       (*json)["require_star"].asBool(true), &e))
                return error(e, "file", "route");
            return answer(routeStatus());
        }
        if (!json->isArray() || json->items().empty() || json->items().size() > 10000)
            return error("Inputs must be 1..10000 {frames, buttons?, stick?} steps.", "inputs");
        const std::map< std::string, unsigned > buttons = {{"jump", 1},         {"action", 2},        {"crouch", 4}, {"spin", 8},   {"cappy", 16},
                                                           {"camera_left", 32}, {"camera_right", 64}, {"plus", 128}, {"minus", 256}};
        std::vector< TestServerInput::Step > steps;
        long total = 0;
        std::size_t index = 0;
        for (const Value& segment : json->items()) {
            const std::string at = "inputs[" + std::to_string(index++) + "]";
            if (!segment.isObject())
                return error("Step must be an object.", at);
            for (const auto& [key, v] : segment.members())
                if (key != "frames" && key != "buttons" && key != "stick")
                    return error("Unknown step key: " + key, at + "." + key, "bad-args", {"frames", "buttons", "stick"});
            if (!integer(segment["frames"], 1, 1080000))
                return error("frames must be an integer in 1..1080000.", at + ".frames");
            TestServerInput::Step step;
            step.frames = segment["frames"].asInt();
            total += step.frames;
            if (total > 1080000)
                return error("Input plan exceeds 1080000 frames.", at);
            if (segment.has("stick")) {
                const Value& stick = segment["stick"];
                if (!stick.isArray() || stick.items().size() != 2)
                    return error("stick must be [right, forward].", at + ".stick");
                for (const Value& axis : stick.items())
                    if (axis.type() != Value::Type::Number || !std::isfinite(axis.asNumber()) || std::fabs(axis.asNumber()) > 1)
                        return error("Stick axes must be finite numbers in -1..1.", at + ".stick");
                step.stickX = stick.items()[0].asNumber();
                step.stickY = stick.items()[1].asNumber();
            }
            if (segment.has("buttons")) {
                if (!segment["buttons"].isArray())
                    return error("buttons must be an array of control names.", at + ".buttons");
                std::map< std::string, bool > seen;
                for (const Value& b : segment["buttons"].items()) {
                    if (b.type() != Value::Type::String)
                        return error("Button names must be strings.", at + ".buttons");
                    const std::string key = b.asString();
                    if (seen[key])
                        return error("Repeated control: " + key, at + ".buttons");
                    seen[key] = true;
                    if (key == "forward" || key == "back" || key == "left" || key == "right") {
                        if (segment.has("stick"))
                            return error("Use stick or directional button names, not both.", at + ".buttons");
                        const std::string opposite = key == "forward" ? "back" : key == "back" ? "forward" : key == "left" ? "right" : "left";
                        if (seen[opposite])
                            return error("Opposing directions: " + key + " and " + opposite, at + ".buttons");
                        if (key == "forward")
                            step.stickY = 1;
                        if (key == "back")
                            step.stickY = -1;
                        if (key == "right")
                            step.stickX = 1;
                        if (key == "left")
                            step.stickX = -1;
                    } else {
                        auto bit = buttons.find(key);
                        if (bit == buttons.end()) {
                            std::vector< std::string > names = {"forward", "back", "left", "right"};
                            for (const auto& [button, mask] : buttons)
                                names.push_back(button);
                            return error("Unknown control: " + key, at + ".buttons", "bad-value", names);
                        }
                        step.held |= bit->second;
                    }
                }
            }
            steps.push_back(step);
        }
        std::string e;
        if (!TestServerInput::startSteps(steps, &e))
            return error(e, "inputs", "inputs");
        return answer(inputStatus());
    }
}  // namespace TestServerReplay
#endif
