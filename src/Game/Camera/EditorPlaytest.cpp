#ifdef PETARI_NATIVE
#include "Game/Camera/EditorPlaytest.hpp"
#include "Game/Editor/EditorScene.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util.hpp"
#include <petari/editor/playtest.hpp>
#include <petari/editor/runtime.hpp>
#include <petari/editor_pick.hpp>
#include <petari/host_allocation.hpp>
#include <petari/mods.hpp>
#include <petari/odyssey_move.hpp>

namespace {
    using namespace PetariEditor;
    namespace P = Playtest;
    struct State {
        P::Trail trail;
        DebugDraw::Mesh mesh;
        bool cleared = false, missed = false, meshDirty = true;
        std::string keepResult;
        int resultFrames = 0;
    };
    State& state() {
        static State s;
        return s;
    }
    Vec3 vec(const TVec3f& v) {
        return {v.x, v.y, v.z};
    }
    P::Motion motion() {
        namespace O = PetariNative::Odyssey;
        P::Motion motion = P::Motion::Air;
        const bool odyssey = PetariNative::Mods::enabled(PetariNative::Mods::Mod::OdysseyMovement);
        if (MR::isOnGroundPlayer())
            motion = P::Motion::Ground;
        else if (odyssey && O::Live::airKind == O::Air::Dive)
            motion = P::Motion::Dive;
        else if (odyssey && (O::Live::airKind == O::Air::CapBounce || O::Live::airKind == O::Air::DiveCapBounce ||
                             O::Live::airKind == O::Air::GroundPoundCapBounce))
            motion = P::Motion::CapBounce;
        return motion;
    }
    void event(Runtime::PlayEvent event, const char*, int) {
        PetariNative::HostAllocationScope host;
        auto& s = state();
        switch (event) {
        case Runtime::PlayEvent::Begin:
            s.trail.begin();
            s.cleared = s.missed = false;
            s.keepResult.clear();
            break;
        case Runtime::PlayEvent::Resume:
            s.trail.resume();
            s.cleared = s.missed = false;
            s.keepResult.clear();
            break;
        case Runtime::PlayEvent::ToEdit:
            s.trail.pause();
            break;
        case Runtime::PlayEvent::End:
            s.trail.end();
            s.mesh = {};
            s.keepResult.clear();
            s.cleared = s.missed = false;
            P::takeKeep();
            break;
        }
        s.meshDirty = true;
    }
    std::string collect(const std::vector< RowId >& rows, std::vector< P::LiveTransform >* live) {
        if (!state().trail.active())
            return "Play-test first, then return to Edit to Keep a moved object.";
        const auto* m = model();
        if (!m || mode() != Mode::Editing)
            return "Return to Edit to Keep a moved object.";
        if (rows.empty())
            return "Select a moved object to Keep.";
        for (const auto row : rows) {
            const auto why = P::keepReason(*m, row);
            if (!why.empty())
                return why;
            const auto* actor = actorForRow(row);
            if (!actor || MR::isDead(actor))
                return "This object was collected or defeated; it has no live transform to Keep.";
            if (live)
                live->push_back({row, {vec(actor->mPosition), vec(actor->mRotation), vec(actor->mScale)}});
        }
        return {};
    }
}  // namespace
namespace EditorPlaytest {
    void init() {
        static bool registered = false;
        if (!registered) {
            PetariNative::HostAllocationScope host;
            Runtime::registerPlayListener(event);
            Runtime::registerMissHandler([](const char*) { return miss(); });
            registered = true;
        }
    }
    void update(bool playable) {
        PetariNative::HostAllocationScope host;
        auto& s = state();
        if (s.trail.running() && mode() == Mode::Playing && playable) {
            s.trail.record(vec(*MR::getPlayerPos()), vec(*MR::getPlayerVelocity()), vec(*MR::getPlayerGravity()), motion());
            s.meshDirty = true;
        }
        if (s.resultFrames > 0 && --s.resultFrames == 0)
            s.keepResult.clear();
        auto rows = P::takeKeep();
        if (!rows.empty()) {
            s.resultFrames = 240;
            std::vector< P::LiveTransform > live;
            s.keepResult = collect(rows, &live);
            if (s.keepResult.empty()) {
                auto op = P::keepOp(*model(), live, &s.keepResult);
                if (s.keepResult.empty()) {
                    if (op.sets.empty())
                        s.keepResult = "The live transform already matches the layout.";
                    else {
                        Command command{Command::Kind::Apply};
                        command.op = std::move(op);
                        enqueue(std::move(command));
                        s.keepResult = "Kept played transform. Cmd+Z undoes it.";
                    }
                }
            }
        }
        P::View view;
        view.active = s.trail.active();
        view.running = s.trail.running();
        view.cleared = s.cleared;
        view.missed = s.missed;
        view.frames = s.trail.frames();
        if (mode() == Mode::Editing) {
            for (const auto& item : PetariNative::EditorPick::selection())
                if (item.row)
                    view.keepRows.push_back({item.row});
            view.keepReason = collect(view.keepRows, nullptr);
            const auto samples = s.trail.samples();
            view.samples = samples.size();
            for (const auto& sample : samples)
                view.peaks += sample.peak;
            if (s.meshDirty) {
                s.mesh = P::geometry(samples);
                s.meshDirty = false;
            }
        }
        view.keepResult = s.keepResult;
        P::publish(std::move(view));
    }
    void draw() {
        if (mode() == Mode::Editing && P::visible())
            DebugDraw::draw(state().mesh, 0.08f);
    }
    bool missPending() {
        return state().missed && mode() == Mode::Playing;
    }
    bool miss() {
        auto& s = state();
        if (!s.trail.running() || mode() != Mode::Playing || !Runtime::progressIsolated())
            return false;
        PetariNative::HostAllocationScope host;
        s.trail.miss(vec(*MR::getPlayerPos()), vec(*MR::getPlayerVelocity()), vec(*MR::getPlayerGravity()), motion());
        s.missed = true;
        s.cleared = false;
        s.meshDirty = true;
        P::setVisible(true);
        const std::string result = "Missed at " + P::elapsed(s.trail.frames());
        return Runtime::requestEditAfterMiss(result.c_str());
    }
    bool collectStar(LiveActor* star) {
        auto& s = state();
        if (!s.trail.active() || mode() != Mode::Playing)
            return false;
        PetariNative::HostAllocationScope host;
        const std::string result = "Cleared in " + P::elapsed(s.trail.frames());
        if (!Runtime::requestEdit(result.c_str()))
            return false;
        MR::endBindAndPlayerWait(star);
        s.trail.pause();
        s.cleared = true;
        MR::deleteEffectAll(star);
        EditorScene::retire(star);
        return true;
    }
}  // namespace EditorPlaytest
#endif
