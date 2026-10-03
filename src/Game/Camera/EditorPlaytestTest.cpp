#ifdef PETARI_NATIVE
#include "Game/Camera/EditorPlaytest.hpp"
#include "Game/Editor/EditorScene.hpp"
#include "Game/LiveActor/Spine.hpp"
#include "Game/MapObj/PowerStar.hpp"
#include "Game/Util.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <petari/editor/playtest.hpp>
#include <petari/editor/runtime.hpp>
#include <petari/editor_pick.hpp>
#include <petari/editor_settings.hpp>
#include <petari/efb_dump_mark.hpp>
#include <petari/host_allocation.hpp>

namespace EditorPlaytest {
    void testTick(bool playable) {
        using namespace PetariEditor;
        static const char* trigger = std::getenv("PETARI_EDITOR_PLAYTEST_TEST");
        static int step = -1, frame = 0, total = 0;
        static RowId kept;
        static RowTransform original, expected;
        static int history;
        static std::uint64_t pausedFrames;
        static PowerStar* star = nullptr;
        static bool hadStar = false;
        if (!trigger || step == 999)
            return;
        PetariNative::HostAllocationScope host;
        auto fail = [&](const char* why) {
            std::fprintf(stderr, "[editor-playtest] FAIL: %s (step %d)\n", why, step);
            step = 999;
        };
        auto advance = [&](int next) {
            step = next;
            frame = 0;
        };
        auto play = [](PlayFrom from) {
            Command c{Command::Kind::Play};
            c.from = from;
            enqueue(std::move(c));
        };
        if (step < 0) {
            if (!std::filesystem::exists(trigger) || !playable)
                return;
            if (Playtest::view().active)
                return fail("normal play recorded a trail");
            PetariNative::EditorSettings::requestEnter();
            advance(0);
            return;
        }
        if (++total > 3000)
            return fail("timeout");
        ++frame;
        const auto* m = model();
        if (!m)
            return;
        const auto view = Playtest::view();
        if (step == 0 && mode() == Mode::Editing && frame > 10) {
            const auto p = *MR::getPlayerPos();
            const auto g = *MR::getPlayerGravity();
            Command marker{Command::Kind::SetPlayMarker};
            marker.markerPos = {p.x, p.y, p.z};
            marker.markerUp = {-g.x, -g.y, -g.z};
            marker.markerFront = {0, 0, 1};
            enqueue(std::move(marker));
            play(PlayFrom::Marker);
            advance(1);
        } else if (step == 1 && mode() == Mode::Playing && playable && frame > 15) {
            const auto g = *MR::getPlayerGravity();
            MR::forceJumpPlayer(TVec3f(-g.x * 35, -g.y * 35, -g.z * 35));
            advance(2);
        } else if (step == 2 && frame > 180 && playable) {
            if (!view.active || view.frames < 120)
                return fail("play did not record time");
            Runtime::requestEdit("Trail test returned to Edit");
            advance(3);
        } else if (step == 3 && mode() == Mode::Editing && frame > 15) {
            if (view.samples < 120 || view.peaks == 0 || view.running)
                return fail("trail/peak capture or pause");
            pausedFrames = view.frames;
            std::fprintf(stderr, "[editor-playtest] trail samples=%zu peaks=%zu frames=%llu\n", view.samples, view.peaks,
                         static_cast< unsigned long long >(view.frames));
            forEachBoundRow([&](RowId row, LiveActor* actor) {
                if (kept.valid() || !actor || MR::isDead(actor) || !Playtest::keepReason(*m, row).empty())
                    return;
                int zone;
                m->locate(row, &zone, nullptr);
                RowTransform world{{actor->mPosition.x, actor->mPosition.y, actor->mPosition.z},
                                   {actor->mRotation.x, actor->mRotation.y, actor->mRotation.z},
                                   {actor->mScale.x, actor->mScale.y, actor->mScale.z}};
                const auto local = Playtest::inZone(m->zoneWorldMatrix(zone), world), layout = m->transform(row);
                const float dx = local.pos.x - layout.pos.x, dy = local.pos.y - layout.pos.y, dz = local.pos.z - layout.pos.z;
                if (dx * dx + dy * dy + dz * dz > 400) {
                    kept = row;
                    original = layout;
                    expected = local;
                }
            });
            if (!kept.valid())
                return fail("no keepable actor moved during the run");
            PetariNative::EditorPick::selectRow(kept.v, PetariNative::EditorPick::Click::Replace);
            PetariNative::EditorGizmo::setShowGhosts(false);
            advance(4);
        } else if (step == 4 && frame == 20) {
            if (view.frames != pausedFrames)
                return fail("Edit advanced timer");
            history = m->historyCursor();
            Playtest::requestKeep({kept});
            advance(5);
        } else if (step == 5 && frame > 15) {
            const auto t = m->transform(kept);
            if (m->historyCursor() != history + 1 || std::fabs(t.pos.x - expected.pos.x) > 0.01f || std::fabs(t.pos.y - expected.pos.y) > 0.01f ||
                std::fabs(t.pos.z - expected.pos.z) > 0.01f)
                return fail("Keep did not persist live transform as one op");
            PetariNative::EfbDump::mark("playtest-trail");
            enqueue(Command{Command::Kind::Undo});
            advance(6);
        } else if (step == 6 && frame > 20) {
            const auto t = m->transform(kept);
            if (m->historyCursor() != history || std::fabs(t.pos.x - original.pos.x) > 0.01f || std::fabs(t.pos.y - original.pos.y) > 0.01f)
                return fail("Keep undo");
            std::fprintf(stderr, "[editor-playtest] Keep + undo PASS: %s\n", m->objectName(kept));
            play(PlayFrom::Resume);
            advance(7);
        } else if (step == 7 && mode() == Mode::Playing && playable && frame > 45) {
            if (view.frames <= pausedFrames)
                return fail("Resume reset or stopped timer");
            forEachBoundRow([&](RowId row, LiveActor* actor) {
                if (!star && actor && std::string(m->objectName(row)) == "PowerStar")
                    star = static_cast< PowerStar* >(actor);
            });
            if (!star)
                return fail("no Power Star fixture");
            hadStar = MR::hasPowerStarInCurrentStage(star->mPowerStarId);
            star->offAppearDemo();
            star->appear();
            if (!star->receiveOtherMsg(ACTMES_AUTORUSH_BEGIN, nullptr, nullptr))
                return fail("star did not enter get nerve");
            // Exercise the production first-step hook before the fixture's far-away star can clip.
            star->mSpine->update();
            advance(8);
        } else if (step == 8 && mode() == Mode::Editing && frame > 30) {
            if (!view.cleared || view.running || !MR::isDead(star))
                return fail("star did not return to Edit");
            if (MR::hasPowerStarInCurrentStage(star->mPowerStarId) != hadStar)
                return fail("star progress escaped interception");
            if (status().notice.find("Cleared in ") != 0)
                return fail("missing clear toast");
            PetariNative::EfbDump::mark("playtest-cleared");
            std::fprintf(stderr, "[editor-playtest] star interception PASS: %s\n", status().notice.c_str());
            advance(9);
        } else if (step == 9 && frame > 30) {
            play(PlayFrom::Marker);
            advance(10);
        } else if (step == 10 && mode() == Mode::Playing && playable && frame > 15) {
            if (view.frames >= pausedFrames || view.cleared)
                return fail("new Play did not reset run");
            Runtime::requestEdit("Play-test checks passed");
            std::fprintf(stderr, "[editor-playtest] PASS: trail, gravity-relative peak, pause/resume, Keep/undo, star interception, new-run reset\n");
            step = 999;
        }
    }
}  // namespace EditorPlaytest
#endif
