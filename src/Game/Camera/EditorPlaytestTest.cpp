#ifdef PETARI_NATIVE
#include "Game/Camera/EditorPlaytest.hpp"
#include "Game/Editor/EditorScene.hpp"
#include "Game/MapObj/PowerStar.hpp"
#include "Game/MapObj/PowerStarHolder.hpp"
#include "Game/NPC/EventDirector.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Util.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <petari/editor/playtest.hpp>
#include <petari/editor/runtime.hpp>
#include <petari/editor/runtime_hooks.hpp>
#include <petari/editor_pick.hpp>
#include <petari/editor_settings.hpp>
#include <petari/efb_dump_mark.hpp>
#include <petari/host_allocation.hpp>
#include <petari/progress.hpp>
#include <petari/progress_hook.hpp>

namespace EditorPlaytest {
    void testTick(bool playable) {
        using namespace PetariEditor;
        static const char* trigger = std::getenv("PETARI_EDITOR_PLAYTEST_TEST");
        static int step = -1, frame = 0, total = 0;
        static RowId kept;
        static std::map< RowId, Vec3 > initialPositions;
        static RowTransform original, expected;
        static int history;
        static std::uint64_t pausedFrames;
        static std::uint32_t runScene;
        static PowerStar* star = nullptr;
        static bool hadStar = false;
        static double personalTime;
        static int personalDeaths, livesAtMiss;
        static std::string personalRecord;
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
            if (!Runtime::progressIsolated())
                return fail("Edit did not isolate progress");
            personalTime = PetariNative::Progress::currentRunTimeS();
            personalDeaths = PetariNative::Progress::currentRunDeaths();
            personalRecord = PetariNative::Progress::serialize();
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
            runScene = Hooks::sceneSerial();
            forEachBoundRow([&](RowId row, LiveActor* actor) {
                if (actor && !MR::isDead(actor) && Playtest::keepReason(*m, row).empty())
                    initialPositions[row] = {actor->mPosition.x, actor->mPosition.y, actor->mPosition.z};
            });
            const auto g = *MR::getPlayerGravity();
            TVec3f side;
            MR::getPlayerSideVec(&side);
            MR::forceJumpPlayer(TVec3f(-g.x * 35 + side.x * 4, -g.y * 35 + side.y * 4, -g.z * 35 + side.z * 4));
            advance(2);
        } else if (step == 2 && frame > 180 && playable) {
            if (!view.active || view.frames < 120)
                return fail("play did not record time");
            Runtime::requestEdit("Trail test returned to Edit");
            advance(3);
        } else if (step == 3 && mode() == Mode::Editing && frame > 15) {
            if (view.samples < 120 || view.peaks == 0 || view.running)
                return fail("trail/peak capture or pause");
            if (PetariNative::Progress::currentRunTimeS() != personalTime || PetariNative::Progress::currentRunDeaths() != personalDeaths)
                return fail("play-test leaked personal time or deaths");
            petari_progress_star_get(1, 0);
            if (PetariNative::Progress::serialize() != personalRecord)
                return fail("isolated star event wrote personal progress");
            pausedFrames = view.frames;
            std::fprintf(stderr, "[editor-playtest] trail samples=%zu peaks=%zu frames=%llu\n", view.samples, view.peaks,
                         static_cast< unsigned long long >(view.frames));
            forEachBoundRow([&](RowId row, LiveActor* actor) {
                if (kept.valid() || !actor || MR::isDead(actor) || !Playtest::keepReason(*m, row).empty())
                    return;
                const auto initial = initialPositions.find(row);
                if (initial == initialPositions.end())
                    return;
                const auto p = initial->second;
                const float mx = actor->mPosition.x - p.x, my = actor->mPosition.y - p.y, mz = actor->mPosition.z - p.z;
                if (mx * mx + my * my + mz * mz <= 400)
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
            if (view.frames <= pausedFrames || Hooks::sceneSerial() != runScene)
                return fail("Resume reset the scene or stopped timer");
            const auto* info = EventFunction::getPowerStarHolder()->findPowerStarRequestInfo(m->scenario());
            star = info ? info->mStar : nullptr;
            if (!star)
                return fail("no Power Star fixture");
            hadStar = MR::hasPowerStarInCurrentStage(star->mPowerStarId);
            const auto position = *MR::getPlayerPos(), gravity = *MR::getPlayerGravity();
            star->mPosition.set(position.x - gravity.x * 50, position.y - gravity.y * 50, position.z - gravity.z * 50);
            star->offAppearDemo();
            star->appear();
            MR::calcAnimDirect(star);
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
            if (view.frames >= pausedFrames || view.cleared || Hooks::sceneSerial() == runScene)
                return fail("new Play did not reset scene and run");
            runScene = Hooks::sceneSerial();
            const auto gravity = *MR::getPlayerGravity();
            TVec3f side;
            MR::getPlayerSideVec(&side);
            MR::forceJumpPlayer(TVec3f(-gravity.x * 24 + side.x * 8, -gravity.y * 24 + side.y * 8, -gravity.z * 24 + side.z * 8));
            advance(11);
        } else if (step == 11 && frame > 45 && playable) {
            livesAtMiss = MR::getPlayerLeft();
            const auto noticeSerial = status().noticeSerial;
            MR::forceKillPlayerByAbyss();
            if (status().noticeSerial == noticeSerial || status().notice.find("Missed at ") != 0 || MR::isPlayerDead())
                return fail("fall did not intercept immediately before the miss demo");
            advance(12);
        } else if (step == 12 && frame >= 2) {
            if (mode() != Mode::Editing)
                return fail("accepted miss did not open Edit within two frames");
            if (!view.missed || view.running || view.samples < 30 || Hooks::sceneSerial() != runScene)
                return fail("miss lost the trail or reloaded the scene");
            if (MR::getPlayerLeft() != livesAtMiss || status().notice.find("Missed at ") != 0)
                return fail("miss consumed a life or omitted the result toast");
            pausedFrames = view.frames;
            PetariNative::EfbDump::mark("playtest-missed");
            std::fprintf(stderr, "[editor-playtest] immediate fall interception PASS: %s; %zu samples\n", status().notice.c_str(), view.samples);
            advance(13);
        } else if (step == 13 && frame > 30) {
            if (view.frames != pausedFrames || Hooks::sceneSerial() != runScene)
                return fail("missed run continued while in Edit");
            play(PlayFrom::Resume);
            advance(14);
        } else if (step == 14 && mode() == Mode::Playing && playable && frame > 15) {
            if (view.missed || view.frames >= pausedFrames || Hooks::sceneSerial() == runScene)
                return fail("Resume after death did not start a fresh playable world");
            runScene = Hooks::sceneSerial();
            const auto noticeSerial = status().noticeSerial;
            MR::forceKillPlayerByGroundRace();
            if (status().noticeSerial == noticeSerial || status().notice.find("Missed at ") != 0 || MR::isPlayerDead())
                return fail("forced death did not intercept immediately");
            advance(15);
        } else if (step == 15 && frame >= 2) {
            if (mode() != Mode::Editing)
                return fail("accepted miss did not open Edit within two frames");
            if (!view.missed || view.running || Hooks::sceneSerial() != runScene || MR::getPlayerLeft() != livesAtMiss)
                return fail("forced death lost the scene, trail or life");
            std::fprintf(stderr, "[editor-playtest] immediate forced-death interception PASS\n");
            play(PlayFrom::Resume);
            advance(16);
        } else if (step == 16 && mode() == Mode::Playing && playable && frame > 15) {
            runScene = Hooks::sceneSerial();
            const auto noticeSerial = status().noticeSerial;
            MarioAccess::getPlayerActor()->forceGameOverBlackHole();
            if (status().noticeSerial == noticeSerial || status().notice.find("Missed at ") != 0 || MR::isPlayerDead())
                return fail("black-hole death did not intercept before its animation");
            advance(17);
        } else if (step == 17 && frame >= 2) {
            if (mode() != Mode::Editing)
                return fail("accepted miss did not open Edit within two frames");
            if (!view.missed || view.running || Hooks::sceneSerial() != runScene || MR::getPlayerLeft() != livesAtMiss)
                return fail("black-hole miss lost scene, trail or life");
            std::fprintf(stderr, "[editor-playtest] immediate black-hole interception PASS\n");
            play(PlayFrom::Resume);
            advance(18);
        } else if (step == 18 && mode() == Mode::Playing && playable && frame > 15) {
            runScene = Hooks::sceneSerial();
            MarioAccess::getPlayerActor()->setNerve(GET_NERVE(MarioActor, MarioActorNrvGameOverSink));
            if (!MR::isPlayerDead())
                return fail("direct death-nerve fixture was not dying");
            advance(19);
        } else if (step == 19 && frame >= 2) {
            if (mode() != Mode::Editing || !view.missed || view.running || Hooks::sceneSerial() != runScene)
                return fail("first-step death fallback could not freeze dying Mario");
            if (MR::getPlayerLeft() != livesAtMiss)
                return fail("direct death nerve consumed a life");
            std::fprintf(stderr,
                         "[editor-playtest] PASS: trail, peaks, Keep/undo, star, fresh world, fall/death/black-hole, dying-player freeze, retry\n");
            step = 999;
        }
    }
}  // namespace EditorPlaytest
#endif
