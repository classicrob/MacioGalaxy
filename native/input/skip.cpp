#include "petari/skip.hpp"

#include <atomic>
#include <cstdio>

#include "petari/mods.hpp"

namespace PetariNative::Skip {

unsigned Machine::frame(bool aHeld, unsigned context, bool enabled) {
    mJustLatched = false;
    mJustEnded = End::None;
    if (!enabled) {
        if (mLatched) end(End::Disabled);
        mHold = 0;
        mSwallow = false;
        return 0;
    }

    // The hold that latched a skip stays hidden from the game until it is
    // released, the release frame included, so it never becomes a jump.
    unsigned hide = 0;
    if (mSwallow) {
        hide = kButtonA;
        if (!aHeld) mSwallow = false;
    }

    if (mLatched) {
        ++mLatchedFrames;
        if ((context & kNewStage) != 0) {
            end(End::Stage);
        } else if ((context & kChoice) != 0) {
            end(End::Choice);
        } else if ((context & kGameplay) != 0) {
            if (++mSettle >= kSettleFrames) end(End::Gameplay);
        } else {
            mSettle = 0;
        }
        if (mLatched && mLatchedFrames >= kTimeoutFrames) end(End::Timeout);
        return hide;
    }

    // Frames count only while something skippable is on screen, so a hold
    // that began in gameplay counts from when the cutscene or talk appears.
    if (!mSwallow && aHeld && (context & kSkippable) != 0 && (context & kChoice) == 0) {
        if (++mHold >= kHoldFrames) {
            mHold = 0;
            mLatched = true;
            mSwallow = true;
            mSettle = 0;
            mLatchedFrames = 0;
            mJustLatched = true;
            hide = kButtonA;
        }
    } else {
        mHold = 0;
    }
    return hide;
}

void Machine::end(End reason) {
    mLatched = false;
    mSettle = 0;
    mJustEnded = reason;
}

float Machine::progress() const {
    if (mLatched) return 1.0f;
    return static_cast<float>(mHold) / static_cast<float>(kHoldFrames);
}

const char* endName(End reason) {
    switch (reason) {
    case End::None: return "none";
    case End::Gameplay: return "gameplay";
    case End::Choice: return "choice";
    case End::Stage: return "stage change";
    case End::Timeout: return "timeout";
    case End::Disabled: return "mod turned off";
    }
    return "?";
}

namespace {
Machine gMachine;
std::atomic<unsigned> gContext{0};
// kNewStage holds until the next pad frame reads it: the game may set the
// context several times between two pad frames (the fast-forward).
std::atomic<bool> gNewStage{false};
std::atomic<bool> gActive{false};
std::atomic<float> gProgress{0.0f};
unsigned long gFrame = 0;
}  // namespace

Machine& machine() { return gMachine; }

void resetForTesting() {
    gMachine = Machine();
    gContext.store(0);
    gNewStage.store(false);
    gActive.store(false);
    gProgress.store(0.0f);
    gFrame = 0;
}

}  // namespace PetariNative::Skip

extern "C" void petari_skip_set_context(unsigned context) {
    using namespace PetariNative;
    if ((context & Skip::kNewStage) != 0) Skip::gNewStage.store(true, std::memory_order_relaxed);
    Skip::gContext.store(context & ~Skip::kNewStage, std::memory_order_relaxed);
}

extern "C" unsigned petari_skip_pad(unsigned rawHold) {
    using namespace PetariNative;
    ++Skip::gFrame;
    unsigned context = Skip::gContext.load(std::memory_order_relaxed);
    if (Skip::gNewStage.exchange(false, std::memory_order_relaxed)) context |= Skip::kNewStage;
    Skip::Machine& m = Skip::gMachine;
    const unsigned hide = m.frame((rawHold & Skip::kButtonA) != 0, context, Mods::enabled(Mods::Mod::SkipCutscenes));
    if (m.justLatched()) {
        std::fprintf(stderr, "[skip] latched at frame %lu after %d frames held (context 0x%x)\n", Skip::gFrame, Skip::kHoldFrames,
                     context);
    }
    if (m.justEnded() != Skip::End::None) {
        std::fprintf(stderr, "[skip] ended at frame %lu: %s, after %d frames (context 0x%x)\n", Skip::gFrame,
                     Skip::endName(m.justEnded()), m.latchedFrames(), context);
    }
    Skip::gActive.store(m.latched(), std::memory_order_relaxed);
    Skip::gProgress.store(m.progress(), std::memory_order_relaxed);
    return hide;
}

extern "C" bool petari_skip_active(void) {
    return PetariNative::Skip::gActive.load(std::memory_order_relaxed);
}

extern "C" unsigned petari_skip_context(void) {
    return PetariNative::Skip::gContext.load(std::memory_order_relaxed);
}

extern "C" float petari_skip_progress(void) {
    return PetariNative::Skip::gProgress.load(std::memory_order_relaxed);
}
