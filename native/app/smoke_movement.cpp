#include "smoke_movement.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <petari/efb_dump_mark.hpp>
#include <petari/mods.hpp>

// Cappy mod (src/Game/Player/MarioCappy.cpp).
extern "C" int petari_cappy_bounces();
extern "C" int petari_cappy_state();
extern "C" int petari_cappy_hits();
extern "C" int petari_cappy_collected();

namespace PetariNative::App::Smoke {

namespace {

constexpr unsigned long kTapFrames = 6;
constexpr unsigned long kReadyFrames = 60;
constexpr unsigned long kStandStill = 30;      // grounded, not moving, before a standstill jump
constexpr unsigned long kTaskLimit = 900;      // frames per task
constexpr float kStillSpeed = 0.05f;

struct V {
    float x, y, z;
};
V sub(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float len(V a) { return std::sqrt(dot(a, a)); }
V scale(V a, float s) { return {a.x * s, a.y * s, a.z * s}; }
std::string num(float v) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.3f", v);
    return text;
}

}  // namespace

bool movementEnabledFromEnvironment() {
    const char* smoke = std::getenv("PETARI_SMOKE");
    return smoke != nullptr && std::strcmp(smoke, "movement") == 0;
}

MovementDriver::MovementDriver(unsigned long frameLimit) : mBoot(frameLimit + 1, Script::Reload), mFrameLimit(frameLimit) {
    mTasks = {
        {"standstill held jump", 15, false, 0, false},
        {"standstill tap jump", 1, false, 0, false},
        {"ground-pound jump", 15, false, 0, false, 0, true},
        {"running jump 1", 15, true, 50, false},
        {"chain jump 2", 15, true, 0, true},
        {"chain jump 3", 15, true, 0, true},
        {"long jump", 6, true, 40, false, 3},
        {"dive", 6, false, 0, false, 0, true, 0, true},
        {"backflip", 6, false, 0, false, 8},
        {"sideflip", 6, true, 45, false, 0, false, 2},
        {"roll", 0, false, 0, false, 3, false, 0, false, 120},
        {"wall jump", 15, false, 0, false, 0, false, 0, false, 0, true},
        {"long jump landing (stick held)", 6, true, 40, false, 3, false, 0, false, 0, false, 30},
        {"long jump landing (stick released)", 6, true, 40, false, 3, false, 0, false, 0, false, 30, true},
        {"dive landing (stick held)", 6, false, 0, false, 0, true, 0, true, 0, false, 30, false, true},
        {"dive landing (stick released)", 6, false, 0, false, 0, true, 0, true, 0, false, 30},
        {"roll to the end (stick held)", 0, false, 0, false, 3, false, 0, false, 140, false, 0, false, true, true},
        {"roll to the end (stick released)", 0, false, 0, false, 3, false, 0, false, 140, false, 0, false, false, true},
        {"roll boost cadence", 0, false, 0, false, 3, false, 0, false, 150, false, 0, false, false, true, 15},
        {"dive from a jump (stick held)", 6, false, 0, false, 0, true, 0, true, 0, false, 30, false, true, false, 0, 15},
        {"spin taps", 0, false, 0, false, 0, false, 0, false, 0, false, 0, false, false, false, 0, 1, 60},
    };
    // Cappy mod (docs/dev/CAPPY.md): only when it is on.
    Task loop{"cappy loop", 10, true, 40, false};
    loop.cappy = 1;
    Task ground{"cappy ground bounce", 1, true, 20, false};
    ground.cappy = 2;
    Task throws{"cappy throws", 1, false, 0, false};
    throws.cappy = 3;
    // The user's loop (playtest recording 2026-10-01 15:20, frames 15677-15780):
    // standing, Jump held 24 frames, the throw at 19, Jump at 40 (the jump-press
    // dive), Jump again 12 frames after the bounce (the second dive).
    Task standstill{"cappy jump-dive loop", 24, true, 40, false};
    standstill.cappy = 4;
    mTasks.push_back(loop);
    mTasks.push_back(ground);
    mTasks.push_back(throws);
    mTasks.push_back(standstill);
    // "Jump during a ground pound dives" (OdysseyJumpDive): a jump, a ground pound
    // at its peak, Jump 4 frames later: a dive. Jump in a plain jump: no dive.
    // Jump just before a ground pound lands: the ground-pound jump, not a dive.
    Task poundJumpDive{"ground pound jump dive", 2, false, 0, false, 0, true, 0, true};
    poundJumpDive.hopHold = 15;
    poundJumpDive.diveWithA = true;
    poundJumpDive.land = 30;
    Task plainJump{"plain jump then jump", 6, false, 0, false};
    plainJump.airPress = 12;
    plainJump.land = 30;
    Task preLanding{"ground pound jump before landing", 15, false, 0, false, 0, true};
    preLanding.hopHold = 15;
    preLanding.poundPreLandA = true;
    preLanding.land = 30;
    mTasks.push_back(poundJumpDive);
    mTasks.push_back(plainJump);
    mTasks.push_back(preLanding);
    if (const char* dir = std::getenv("PETARI_MOVEMENT_WALL_DIR"); dir != nullptr) {
        const std::string d(dir);
        mWallStick = d == "down" ? Button::StickDown : d == "left" ? Button::StickLeft : d == "right" ? Button::StickRight : Button::StickUp;
    }
    // PETARI_MOVEMENT_ONLY=<name>[,<name>...]: only the tasks whose name contains one of them.
    if (const char* only = std::getenv("PETARI_MOVEMENT_ONLY"); only != nullptr && *only != '\0') {
        std::vector<std::string> keep;
        std::string list = only;
        for (size_t start = 0; start <= list.size();) {
            const size_t comma = std::min(list.find(',', start), list.size());
            if (comma > start) keep.push_back(list.substr(start, comma - start));
            start = comma + 1;
        }
        mTasks.erase(std::remove_if(mTasks.begin(), mTasks.end(),
                                    [&](const Task& t) {
                                        return std::none_of(keep.begin(), keep.end(),
                                                            [&](const std::string& k) { return std::strstr(t.name, k.c_str()) != nullptr; });
                                    }),
                     mTasks.end());
    }
}

const char* MovementDriver::phase() const {
    switch (mPhase) {
    case Phase::Boot: return mBoot.phase();
    case Phase::Settle: return "movement: waiting to stand still";
    case Phase::Run: return "movement: running";
    case Phase::Jump: return "movement: jumping";
    case Phase::Crouch: return "movement: crouching before a long jump";
    case Phase::Hop: return "movement: hop before a ground pound";
    case Phase::Pound: return "movement: ground pound";
    case Phase::Reverse: return "movement: stick back before a sideflip";
    case Phase::Roll: return "movement: rolling";
    case Phase::WallRun: return "movement: running to a wall";
    case Phase::WallClimb: return "movement: jumping against the wall";
    case Phase::WallSlide: return "movement: sliding down the wall";
    case Phase::Landed: return "movement: ground speed after landing";
    case Phase::SpinTaps: return "movement: spin taps";
    case Phase::Land: return "movement: landed";
    case Phase::Cappy: return "movement: Cappy";
    case Phase::Done: return "movement: done";
    }
    return "movement";
}

void MovementDriver::tap(Button button, unsigned long holdFrames, Step& step) {
    step.presses.push_back({button, true});
    step.assertFocus = true;
    mReleases.push_back({mFrame + holdFrames, button});
}

void MovementDriver::finish(Result result, const std::string& reason, Step& step) {
    mResult = result;
    mReason = reason;
    mPhase = Phase::Done;
    if (mStickHeld) step.presses.push_back({Button::StickUp, false});
    mStickHeld = false;
    note(std::string(result == Result::Pass ? "PASS: " : "FAIL: ") + reason);
    step.requestQuit = true;
}

Step MovementDriver::step(const Observation& o) {
    Step step;
    mLog.clear();
    ++mFrame;
    ++mPhaseFrames;
    for (auto it = mReleases.begin(); it != mReleases.end();) {
        if (it->frame <= mFrame) {
            step.presses.push_back({it->button, false});
            it = mReleases.erase(it);
        } else {
            ++it;
        }
    }
    if (mResult != Result::Running) return step;

    if (mPhase == Phase::Boot) {
        step = mBoot.step(o);
        for (const std::string& line : mBoot.log()) {
            if (line.rfind("physical ", 0) != 0 && line.rfind("window focus", 0) != 0) mLog.push_back(line);
        }
        if (mBoot.result() != Result::Running) {
            mResult = mBoot.result() == Result::Blocked ? Result::Blocked : Result::Fail;
            mReason = std::string("before the measurements: ") + mBoot.reason();
            mPhase = Phase::Done;
            step.requestQuit = true;
            return step;
        }
        const bool ready = o.scene == "Game" && o.sceneReady && o.playerValid && o.playerOnGround && !o.demoActive &&
                           o.pausePermitted && !o.talkActive;
        mReady = ready ? mReady + 1 : 0;
        if (mReady < kReadyFrames) return step;
        step.presses.clear();
        for (Button b : {Button::A, Button::B, Button::StickUp, Button::StickDown, Button::StickLeft, Button::StickRight}) {
            step.presses.push_back({b, false});
        }
        step.requestQuit = false;
        mReleases.clear();
        mPhase = Phase::Settle;
        mPhaseFrames = 0;
        mReady = 0;
        note("movement measurements in " + o.stage + " at (" + num(o.playerX) + ", " + num(o.playerY) + ", " + num(o.playerZ) + ")");
        // PETARI_STAGE_WARP=x,y,z or name:<GeneralPos>: start the tasks there (test-only).
        if (const char* warp = std::getenv("PETARI_STAGE_WARP"); warp != nullptr && warp[0] != '\0') {
            if (std::strncmp(warp, "name:", 5) == 0) {
                step.warp = true;
                step.warpName = warp + 5;
            } else if (std::sscanf(warp, "%f,%f,%f", &step.warpX, &step.warpY, &step.warpZ) == 3) {
                step.warp = true;
            }
            if (step.warp) note(std::string("warp to ") + warp);
        }
        return step;
    }
    if (mFrame >= mFrameLimit) {
        finish(Result::Fail, std::string("frame limit reached while ") + phase(), step);
        return step;
    }
    if (!o.playerValid) return step;
    if (o.playerDead) {
        finish(Result::Fail, "Mario died during the measurements", step);
        return step;
    }

    const V pos{o.playerX, o.playerY, o.playerZ};
    const V last{mLastX, mLastY, mLastZ};
    mLastX = pos.x; mLastY = pos.y; mLastZ = pos.z;
    V up{-o.gravityX, -o.gravityY, -o.gravityZ};
    const float upLen = len(up);
    up = upLen > 1e-3f ? scale(up, 1.0f / upLen) : V{0, 1, 0};
    const V moved = sub(pos, last);
    const float horizontal = len(sub(moved, scale(up, dot(moved, up))));

    // The Cappy tasks need the mod (PETARI_MODS=OdysseyMovement,Cappy).
    while (mTask < mTasks.size() && mTasks[mTask].cappy > 0 && mPhase != Phase::Cappy &&
           !(petari_mod_enabled(static_cast<int>(Mods::Mod::Cappy)) &&
             petari_mod_enabled(static_cast<int>(Mods::Mod::OdysseyMovement)))) {
        note(std::string(mTasks[mTask].name) + ": skipped (the Cappy mod is off)");
        ++mTask;
    }
    if (mTask >= mTasks.size()) {
        finish(Result::Pass, "movement measured: " + std::to_string(mTasks.size()) + " moves", step);
        return step;
    }
    const Task& task = mTasks[mTask];
    if (mPhaseFrames > kTaskLimit && mPhase != Phase::SpinTaps) {
        finish(Result::Fail, std::string(task.name) + " did not finish within " + std::to_string(kTaskLimit) + " frames", step);
        return step;
    }

    const auto startJump = [&](Button button = Button::A) {
        mStartX = pos.x; mStartY = pos.y; mStartZ = pos.z;
        mUpX = up.x; mUpY = up.y; mUpZ = up.z;
        mApex = 0.0f;
        mApexFrame = 0;
        mLeftGround = button != Button::A;  // a dive starts in the air
        mTakeoffSpeed = horizontal;
        tap(button, static_cast< unsigned long >(button == Button::A ? task.holdA : kTapFrames), step);
        PetariNative::EfbDump::mark(task.name);  // opt-in image dump (PETARI_XFB_DUMP); inert otherwise
        if (task.releaseStick && mStickHeld) {
            step.presses.push_back({Button::StickUp, false});
            mStickHeld = false;
        }
        if (task.airStick && !mStickHeld) {
            step.presses.push_back({Button::StickUp, true});
            mStickHeld = true;
        }
        mPhase = Phase::Jump;
        mPhaseFrames = 0;
        if (task.cappy > 0) {
            mPhase = Phase::Cappy;
            mBounced = mDive2 = false;
            mAfterBounceApex = 0.0f;
        }
    };

    switch (mPhase) {
    case Phase::Settle:
    case Phase::Land: {
        if (task.afterLanding && mPhase == Phase::Land) {
            startJump();  // the frame after landing
            break;
        }
        if (task.wall) {
            step.presses.push_back({mWallStick, true});
            step.assertFocus = true;
            mPhase = Phase::WallRun;
            mPhaseFrames = 0;
            mReady = 0;
            break;
        }
        if (task.stick) {
            if (!mStickHeld) {
                step.presses.push_back({Button::StickUp, true});
                step.assertFocus = true;
                mStickHeld = true;
            }
            if (task.runFrames > 0) {
                mPhase = Phase::Run;
                mPhaseFrames = 0;
                mMaxSpeed = 0.0f;
                mSpeeds.clear();
            } else {
                startJump();
            }
            break;
        }
        if (mStickHeld) {
            step.presses.push_back({Button::StickUp, false});
            mStickHeld = false;
        }
        // A standstill jump: grounded and still for a while first.
        mReady = (o.playerOnGround && horizontal < kStillSpeed && std::fabs(dot(moved, up)) < kStillSpeed) ? mReady + 1 : 0;
        if (mReady >= kStandStill) {
            mReady = 0;
            if (task.spinTaps > 0) {
                mPhase = Phase::SpinTaps;
                mPhaseFrames = 0;
                mTaps = 0;
                mNextTap = mFrame + 1;
                break;
            }
            if (task.crouch > 0) {
                // Backflip: crouch, then jump. Roll: crouch, then Spin (Z held longer so
                // the Spin lands inside the crouch whatever the input timing).
                tap(Button::Z, static_cast< unsigned long >(task.crouch + task.holdA + (task.roll > 0 ? 10 : 2)), step);
                mPhase = Phase::Crouch;
                mPhaseFrames = 0;
            } else if (task.cappy == 3) {
                mPhase = Phase::Cappy;
                mPhaseFrames = 0;
                mBounceStart = petari_cappy_hits();
                mTaps = petari_cappy_collected();
            } else if (task.groundPound) {
                tap(Button::A, static_cast< unsigned long >(task.hopHold), step);  // a hop (or a jump); the ground pound comes at its peak
                mStartX = pos.x; mStartY = pos.y; mStartZ = pos.z;
                mUpX = up.x; mUpY = up.y; mUpZ = up.z;
                mDive2 = false;
                mPhase = Phase::Hop;
                mPhaseFrames = 0;
                mLeftGround = false;
            } else {
                startJump();
            }
        }
        break;
    }
    case Phase::Hop: {
        if (!o.playerOnGround) mLeftGround = true;
        if (mLeftGround && mPhaseFrames > 6 && dot(moved, up) <= 0.0f) {
            tap(Button::Z, kTapFrames, step);
            note("ground pound at the hop's peak");
            mPhase = Phase::Pound;
            mPhaseFrames = 0;
            mReady = 0;
        }
        break;
    }
    case Phase::Pound: {
        if (task.dive) {
            if (mPhaseFrames == 4) {
                if (task.diveWithA) {
                    startJump(Button::A);  // Jump during the pound's wind-up
                    mLeftGround = true;
                } else {
                    startJump(Button::Spin);  // Spin during the pound's wind-up
                }
            }
            break;
        }
        if (task.poundPreLandA) {
            const float h = dot(sub(pos, V{mStartX, mStartY, mStartZ}), V{mUpX, mUpY, mUpZ});
            if (!mDive2 && !o.playerOnGround && mPhaseFrames > 6 && h < 100.0f && dot(moved, up) < -10.0f) {
                tap(Button::A, 2, step);
                mDive2 = true;
                note(std::string(task.name) + ": Jump " + num(h) + " above the ground, falling " + num(-dot(moved, up)) + " u/f");
            }
            if (mDive2 && o.playerOnGround) {
                mSpeeds.assign(1, horizontal);
                mPhase = Phase::Landed;
                mPhaseFrames = 0;
            }
            break;
        }
        // Count grounded frames after the pound lands; jump on the 8th.
        mReady = o.playerOnGround ? mReady + 1 : 0;
        if (mReady == 8) {
            mReady = 0;
            startJump();
        }
        break;
    }
    case Phase::Crouch: {
        if (static_cast< int >(mPhaseFrames) >= task.crouch) {
            if (task.roll > 0) {
                tap(Button::Spin, 2, step);
                PetariNative::EfbDump::mark(task.name);
                if (task.airStick && !mStickHeld) {
                    step.presses.push_back({Button::StickUp, true});
                    mStickHeld = true;
                }
                mPhase = Phase::Roll;
                mPhaseFrames = 0;
                mSpeeds.clear();
            } else {
                startJump();
            }
        }
        break;
    }
    case Phase::WallRun: {
        // Blocked: the stick is held but Mario barely moves for 10 frames.
        mReady = (mPhaseFrames > 30 && o.playerOnGround && horizontal < 1.0f) ? mReady + 1 : 0;
        if (mReady >= 10) {
            note("wall reached at (" + num(pos.x) + ", " + num(pos.y) + ", " + num(pos.z) + ")");
            tap(Button::A, static_cast< unsigned long >(task.holdA), step);
            mStartX = pos.x; mStartY = pos.y; mStartZ = pos.z;
            mUpX = up.x; mUpY = up.y; mUpZ = up.z;
            mPhase = Phase::WallClimb;
            mPhaseFrames = 0;
            mReady = 0;
            mLeftGround = false;
        } else if (mPhaseFrames > 600) {
            step.presses.push_back({mWallStick, false});
            finish(Result::Fail, "no wall within 600 frames of running", step);
        }
        break;
    }
    case Phase::WallClimb: {
        // On the wall: in the air and falling, then (almost) still along gravity.
        if (!o.playerOnGround) mLeftGround = true;
        const float rise = dot(moved, up);
        if (mLeftGround && o.playerOnGround) {
            step.presses.push_back({mWallStick, false});
            finish(Result::Fail, "landed without hanging on the wall", step);
            break;
        }
        if (dot(sub(pos, V{mStartX, mStartY, mStartZ}), V{mUpX, mUpY, mUpZ}) < -300.0f) {
            step.presses.push_back({mWallStick, false});
            finish(Result::Fail, "fell past the take-off point: no wall to hang on", step);
            break;
        }
        if (mLeftGround && mPhaseFrames > 10 && std::fabs(rise) < 0.05f) {
            PetariNative::EfbDump::mark("wall slide");
            mPhase = Phase::WallSlide;
            mPhaseFrames = 0;
            mSpeeds.clear();
        }
        break;
    }
    case Phase::WallSlide: {
        mSpeeds.push_back(-dot(moved, up));
        if (o.playerOnGround) {
            step.presses.push_back({mWallStick, false});
            finish(Result::Fail, "slid to the ground before the wall jump", step);
            break;
        }
        if (mPhaseFrames >= 20) {
            std::string falls;
            for (float f : mSpeeds) falls += (falls.empty() ? "" : " ") + num(f);
            note("MOVEMENT wall slide: fall per frame " + falls);
            step.presses.push_back({mWallStick, false});
            startJump();
            mLeftGround = true;  // measured from the press, in the air
        }
        break;
    }
    case Phase::SpinTaps: {
        // Holds of 1-6 frames and gaps of 16-40 frames, mixed so presses land at
        // every phase against the 200 Hz reports and the 250 ms shake spacing.
        static const int kHolds[] = {1, 2, 3, 4, 6, 1, 3, 2, 5};
        static const int kGaps[] = {16, 20, 25, 31, 40, 17, 23, 29, 35, 19};
        if (mFrame >= mNextTap && mTaps < task.spinTaps) {
            const int hold = kHolds[mTaps % 9];
            tap(Button::Spin, static_cast< unsigned long >(hold), step);
            mNextTap = mFrame + static_cast< unsigned long >(kGaps[mTaps % 10]);
            ++mTaps;
        }
        if (mTaps >= task.spinTaps && mFrame >= mNextTap + 30) {
            note(std::string("MOVEMENT spin taps: ") + std::to_string(mTaps) + " taps");
            mTaps = 0;
            ++mTask;
            mPhase = Phase::Land;
            mPhaseFrames = 0;
        }
        break;
    }
    case Phase::Landed: {
        mSpeeds.push_back(horizontal);
        if (!o.playerOnGround && mRetakeoff < 0) mRetakeoff = static_cast< long >(mPhaseFrames);
        if (static_cast< int >(mPhaseFrames) >= task.land) {
            std::string speeds;
            for (size_t i = 0; i < mSpeeds.size(); i += 5) speeds += (speeds.empty() ? "" : " ") + num(mSpeeds[i]);
            note(std::string("MOVEMENT ") + task.name + ": speed " + num(mSpeeds[0]) + " on the landing frame; every 5th frame: " + speeds +
                 (mRetakeoff >= 0 ? "; took off again " + std::to_string(mRetakeoff) + " frames after landing" : "; stayed on the ground"));
            mRetakeoff = -1;
            ++mTask;
            mPhase = Phase::Land;
            mPhaseFrames = 0;
            mReady = 0;
        }
        break;
    }
    case Phase::Roll: {
        if (mPhaseFrames > 1) mSpeeds.push_back(horizontal);
        // A boost (15 frames apart at least; the emulated shake behind Spin repeats
        // about four times a second, so 30 frames after the start).
        if (mPhaseFrames == 30 && !task.noBoost) tap(Button::Spin, 2, step);
        if (task.mash > 0 && mPhaseFrames >= 15 && mPhaseFrames % static_cast< unsigned long >(task.mash) == 0) {
            tap(Button::Spin, 2, step);
            ++mTaps;
        }
        if (static_cast< int >(mPhaseFrames) >= task.roll) {
            float top = 0.0f;
            std::string speeds;
            for (size_t i = 0; i < mSpeeds.size(); ++i) {
                top = std::max(top, mSpeeds[i]);
                if (i % 5 == 0) speeds += (speeds.empty() ? "" : " ") + num(mSpeeds[i]);
            }
            if (task.mash > 0) {
                int boosts = 0;
                for (size_t i = 1; i < mSpeeds.size(); ++i) boosts += mSpeeds[i] > mSpeeds[i - 1] + 1.0f;
                note(std::string("MOVEMENT ") + task.name + ": " + std::to_string(boosts) + " boosts from " + std::to_string(mTaps) +
                     " Spin taps (every " + std::to_string(task.mash) + " frames)");
                mTaps = 0;
            }
            note(std::string("MOVEMENT ") + task.name + ": max speed " + num(top) + " u/f; every 5th frame: " + speeds);
            ++mTask;
            mPhase = Phase::Land;
            mPhaseFrames = 0;
            mReady = 0;
        }
        break;
    }
    case Phase::Reverse: {
        if (mPhaseFrames == 2) {
            step.presses.push_back({Button::StickDown, true});
            step.assertFocus = true;
        }
        if (static_cast< int >(mPhaseFrames) >= 2 + task.reverse) {
            startJump();
            mReleases.push_back({mFrame + static_cast< unsigned long >(task.holdA), Button::StickDown});
        }
        break;
    }
    case Phase::Run: {
        if (mPhaseFrames > 1) {
            mSpeeds.push_back(horizontal);
            mMaxSpeed = std::max(mMaxSpeed, horizontal);
        }
        if (static_cast< int >(mPhaseFrames) >= task.runFrames) {
            int reached = -1;
            for (size_t i = 0; i < mSpeeds.size(); ++i) {
                if (mSpeeds[i] >= 0.95f * mMaxSpeed) {
                    reached = static_cast< int >(i) + 2;
                    break;
                }
            }
            std::string speeds;
            for (size_t i = 0; i < mSpeeds.size(); i += 5) speeds += (speeds.empty() ? "" : " ") + num(mSpeeds[i]);
            note(std::string("MOVEMENT run") + (task.crouch > 0 ? " before the long jump" : "") + ": max speed " + num(mMaxSpeed) +
                 " u/f, frame " + std::to_string(reached) + " reached 95%; every 5th frame: " + speeds);
            if (task.cappy == 2) {
                // Running: the cap thrown along the run and held; the run goes on after it.
                tap(Button::CapThrow, 130, step);
                PetariNative::EfbDump::mark(task.name);
                mStartX = pos.x; mStartY = pos.y; mStartZ = pos.z;
                mUpX = up.x; mUpY = up.y; mUpZ = up.z;
                mLeftGround = false;
                mBounced = mDive2 = false;
                mAfterBounceApex = 0.0f;
                mBounceStart = petari_cappy_bounces();
                mPhase = Phase::Cappy;
                mPhaseFrames = 0;
            } else if (task.crouch > 0) {
                tap(Button::Z, static_cast< unsigned long >(task.crouch + task.holdA + 2), step);
                mPhase = Phase::Crouch;
                mPhaseFrames = 0;
            } else if (task.reverse > 0) {
                // Release first: Galaxy arms its turn slide only after the stick
                // passes near neutral (mTurnSlipNeutral) while braking.
                step.presses.push_back({Button::StickUp, false});
                mStickHeld = false;
                mPhase = Phase::Reverse;
                mPhaseFrames = 0;
            } else {
                startJump();
            }
        }
        break;
    }
    case Phase::Jump: {
        if (task.airPress > 0 && static_cast< int >(mPhaseFrames) == task.airPress) {
            tap(Button::A, 2, step);
            note(std::string(task.name) + ": A again " + std::to_string(task.airPress) + " frames after the jump, " +
                 (o.playerOnGround ? "on the ground" : "in the air"));
        }
        if (task.wall && mPhaseFrames == 1) {
            // The press frame still slid down the wall: measure from the take-off.
            mStartX = pos.x; mStartY = pos.y; mStartZ = pos.z;
        }
        if (!mLeftGround && o.playerOnGround && mPhaseFrames % 20 == 0 && mPhaseFrames > 0 && mPhaseFrames <= 60) {
            // A 1-frame tap can fall between two game frames; press again.
            note(std::string(task.name) + ": no take-off after " + std::to_string(mPhaseFrames) + " frames, pressing again");
            tap(Button::A, static_cast< unsigned long >(task.holdA), step);
        }
        if (!mLeftGround && o.playerOnGround) {
            // Measure from the last grounded position before take-off (the press
            // frame can still be in a crouch slide).
            mStartX = pos.x; mStartY = pos.y; mStartZ = pos.z;
        }
        const V start{mStartX, mStartY, mStartZ};
        const V upStart{mUpX, mUpY, mUpZ};
        const float height = dot(sub(pos, start), upStart);
        if (height > mApex) {
            mApex = height;
            mApexFrame = mPhaseFrames;
        }
        if (!o.playerOnGround) mLeftGround = true;
        if (mLeftGround && o.playerOnGround) {
            note(std::string("MOVEMENT ") + task.name + ": apex " + num(mApex) + " after " + std::to_string(mApexFrame) +
                 " frames, take-off speed " + num(mTakeoffSpeed) + ", landed after " + std::to_string(mPhaseFrames) + " frames, from (" +
                 num(mStartX) + ", " + num(mStartY) + ", " + num(mStartZ) + ")");
            if (task.land > 0) {
                mSpeeds.assign(1, horizontal);  // the landing frame
                mPhase = Phase::Landed;
                mPhaseFrames = 0;
                break;
            }
            ++mTask;
            mPhase = Phase::Land;
            mPhaseFrames = 0;
            mReady = 0;
        }
        break;
    }
    case Phase::Cappy: {
        // The Cappy mod's moves; heights from the take-off point along its -gravity
        // (the ground task: from the throw, which its distances need too).
        if (task.cappy == 1 && !mLeftGround && o.playerOnGround) {
            mStartX = pos.x; mStartY = pos.y; mStartZ = pos.z;
        }
        const V start{mStartX, mStartY, mStartZ};
        const V upStart{mUpX, mUpY, mUpZ};
        const float height = dot(sub(pos, start), upStart);
        if (mPhaseFrames == 1 && (task.cappy == 1 || task.cappy == 4)) mBounceStart = petari_cappy_bounces();
        if (!o.playerOnGround) mLeftGround = true;
        if (task.cappy == 3) {
            // Standing: a throw every 90 frames, five times (aim them with
            // PETARI_CAPPY_TEST_AIM); then the hits and items Cappy got.
            if (mPhaseFrames % 90 == 1 && mPhaseFrames < 450) {
                tap(Button::CapThrow, kTapFrames, step);
                static const char* const kLabels[] = {"cappy throws 1", "cappy throws 2", "cappy throws 3", "cappy throws 4", "cappy throws 5"};
                PetariNative::EfbDump::mark(kLabels[mPhaseFrames / 90]);
            }
            if (mPhaseFrames >= 450) {
                note(std::string("MOVEMENT cappy throws: ") + std::to_string(petari_cappy_hits() - mBounceStart) + " enemy hits, " +
                     std::to_string(petari_cappy_collected() - mTaps) + " items collected in 5 throws");
                ++mTask;
                mPhase = Phase::Land;
                mPhaseFrames = 0;
                mReady = 0;
            }
            break;
        }
        if (task.cappy == 4) {
            if (mPhaseFrames == 19) {
                tap(Button::CapThrow, kTapFrames, step);
                PetariNative::EfbDump::mark("cappy throw");
            }
            if (mPhaseFrames == 26) tap(Button::Z, kTapFrames, step);  // the air crouch (ground pound)
            if (mPhaseFrames == 30) {
                tap(Button::A, kTapFrames, step);  // Jump in it: the dive
                PetariNative::EfbDump::mark("cappy dive");
            }
        }
        if (task.cappy == 1) {
            // Running jump (A held 10), the throw at 6, a ground pound at 18, the
            // dive (the throw key) at 22; after the bounce, a pound and a dive again.
            if (mPhaseFrames == 6) {
                tap(Button::CapThrow, kTapFrames, step);
                PetariNative::EfbDump::mark("cappy throw");
            }
            if (mPhaseFrames == 18) tap(Button::Z, kTapFrames, step);
            if (mPhaseFrames == 22) {
                tap(Button::CapThrow, kTapFrames, step);
                PetariNative::EfbDump::mark("cappy dive");
            }
        } else {
            // Ground: running on after the cap; a tapped jump 170 short of it.
            const V moved0 = sub(pos, start);
            const float ahead = len(sub(moved0, scale(upStart, dot(moved0, upStart))));
            if (!mBounced && o.playerOnGround && ahead >= 678.0f - 170.0f && (!mDive2 || mPhaseFrames >= mJumpFrame + 4)) {
                // A 2-frame tap (a 1-frame one can fall between game frames), again
                // if Mario is still on the ground 4 frames later.
                tap(Button::A, 2, step);
                if (!mDive2) PetariNative::EfbDump::mark("cappy ground jump");
                mDive2 = true;  // here: the jump was made
                mJumpFrame = mPhaseFrames;
                note("cappy ground bounce: jump " + num(ahead) + " ahead of the throw");
            }
        }
        if (!mBounced && petari_cappy_bounces() > mBounceStart) {
            mBounced = true;
            mBounceFrame = mPhaseFrames;
            mBounceHeight = height;
            PetariNative::EfbDump::mark("cappy bounce");
            note(std::string(task.name) + ": bounce on frame " + std::to_string(mPhaseFrames) + " at height " + num(height));
        }
        if (mBounced) {
            mAfterBounceApex = std::max(mAfterBounceApex, height - mBounceHeight);
            if (task.cappy == 1 && mPhaseFrames == mBounceFrame + 8) tap(Button::Z, kTapFrames, step);
            if (task.cappy == 1 && mPhaseFrames == mBounceFrame + 12) {
                tap(Button::CapThrow, kTapFrames, step);
                PetariNative::EfbDump::mark("cappy dive 2");
            }
            if (task.cappy == 4 && mPhaseFrames == mBounceFrame + 8) tap(Button::Z, kTapFrames, step);
            if (task.cappy == 4 && mPhaseFrames == mBounceFrame + 12) {
                tap(Button::A, kTapFrames, step);
                PetariNative::EfbDump::mark("cappy dive 2");
            }
        }
        if (mLeftGround && o.playerOnGround && mPhaseFrames > 10 && (task.cappy == 1 || task.cappy == 4 || mDive2 || mBounced)) {
            if (mStickHeld) {
                step.presses.push_back({Button::StickUp, false});
                mStickHeld = false;
            }
            if (!mBounced) {
                finish(Result::Fail, std::string(task.name) + ": landed after " + std::to_string(mPhaseFrames) +
                                         " frames without a cap bounce (Cappy state " + std::to_string(petari_cappy_state()) + ")",
                       step);
                break;
            }
            note(std::string("MOVEMENT ") + task.name + ": bounce on frame " + std::to_string(mBounceFrame) + " at height " +
                 num(mBounceHeight) + ", rose " + num(mAfterBounceApex) + " after it, landed after " + std::to_string(mPhaseFrames) +
                 " frames, " + num(len(sub(pos, start))) + " from the take-off");
            ++mTask;
            mPhase = Phase::Land;
            mPhaseFrames = 0;
            mReady = 0;
        }
        break;
    }
    default:
        break;
    }
    return step;
}

}  // namespace PetariNative::App::Smoke
