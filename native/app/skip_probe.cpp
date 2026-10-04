// PETARI_SKIP_PROBE=1: proof runs for the hold-to-skip mod (native/MODS.md
// "Skip cutscenes and dialogue"), next to any smoke script. Whenever a
// cutscene, movie or dialogue has been on screen for PETARI_SKIP_PROBE_DELAY
// frames (45), it holds Space through the native input layer, as a player
// would, until the skip latches and ends, keeps holding PETARI_SKIP_PROBE_TAIL
// frames (60) into what follows, then releases and watches 90 frames. Every
// step is logged as "[skip-probe] ..." with what the game shows
// (CutsceneSkip::describe). The watch reports whether the game saw A at all
// while the latching hold was down (it must not: no jump) and how far Mario
// rose. PETARI_SKIP_PROBE_SHOTS=<dir>: screenshots before the hold, at the
// latch, when the skip ends and after the watch. PETARI_SKIP_PROBE_MAX=<n>
// stops after n skips. PETARI_SKIP_PROBE_ANSWER=yes: a yes/no left on screen by a
// skip is answered Yes once Space is released (pointer on the game's Prompt.Yes, then
// A), e.g. to let the save after a star write the file. PETARI_SKIP_PROBE_MOVIE=FinalBattle:
// a TEST START of the final-battle movie once normal play has lasted 60 frames (the
// observatory, late save; its MovieStarter needs Rosalina's pre-final talk). Needs the mod on (PETARI_MODS=SkipCutscenes), or, for the
// mod-off check, PETARI_SKIP_OBSERVE_WHEN_OFF=1 with the mod off: the probe then
// holds in cutscenes, nothing latches, and the NO LATCH line counts the frames the
// game saw A (all of them: with the mod off a long hold is plain A). Each hold
// starts the image-dump phase "skip-hold" (petari/efb_dump_mark.hpp), so
// PETARI_SURFACE_DUMP_LABELS=skip-hold (or the XFB/EFB dumps) capture the hold.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "Game/System/CutsceneSkip.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/MoviePlayingSequence.hpp"
#include "Game/Util/GamePadUtil.hpp"
#include "petari/efb_dump_mark.hpp"
#include "petari/input.hpp"
#include "petari/screenshot.hpp"
#include "petari/skip.hpp"
#include "smoke.hpp"

namespace PetariNative::App::Smoke {
namespace {

enum class Phase { Idle, Holding, Latched, Tail, Watch };

struct Probe {
    bool enabled = false;
    int delay = 45;
    int tail = 60;
    int watch = 90;
    int max = 1000;
    std::string shots;

    Phase phase = Phase::Idle;
    unsigned long frame = 0;
    int phaseFrames = 0;
    int skippableFrames = 0;
    int skips = 0;
    int heldGameA = 0;     // frames the game saw A while the latching hold was down
    int heldFrames = 0;
    bool watchStarted = false;
    // PETARI_SKIP_PROBE_ANSWER=yes: a yes/no left by a skip is answered Yes, with Space released.
    bool answerYes = false;
    bool answered = false;        // this prompt's Yes was pressed
    int pointing = 0;             // frames the pointer has been over Prompt.Yes
    unsigned long releaseAt = 0;  // the answering tap's release
    bool wantPointer = false;
    float pointerU = 0.0f, pointerV = 0.0f;
    std::string shown;  // what the game showed at the last logged change during a skip
    // PETARI_SKIP_PROBE_MOVIE=FinalBattle: TEST START of that movie (MR::startMovie, as its
    // MovieStarter does) after 60 frames of normal play, once; the movie and its end are the game's.
    bool startFinalBattle = false;
    int gameplayFrames = 0;
    float startX = 0.0f, startY = 0.0f, startZ = 0.0f;
    float maxRise = 0.0f;
    int groundFrames = 0;
};

Probe& probe() {
    static Probe p = [] {
        Probe p;
        const char* on = std::getenv("PETARI_SKIP_PROBE");
        p.enabled = on != nullptr && on[0] != '\0' && on[0] != '0';
        if (const char* v = std::getenv("PETARI_SKIP_PROBE_DELAY")) p.delay = std::atoi(v);
        if (const char* v = std::getenv("PETARI_SKIP_PROBE_TAIL")) p.tail = std::atoi(v);
        if (const char* v = std::getenv("PETARI_SKIP_PROBE_MAX")) p.max = std::atoi(v);
        if (const char* v = std::getenv("PETARI_SKIP_PROBE_SHOTS")) p.shots = v;
        if (const char* v = std::getenv("PETARI_SKIP_PROBE_ANSWER")) p.answerYes = std::string(v) == "yes";
        if (const char* v = std::getenv("PETARI_SKIP_PROBE_MOVIE")) p.startFinalBattle = std::string(v) == "FinalBattle";
        if (p.enabled) {
            std::fprintf(stderr, "[skip-probe] on: delay %d, tail %d, max %d, shots %s\n", p.delay, p.tail, p.max,
                         p.shots.empty() ? "off" : p.shots.c_str());
        }
        return p;
    }();
    return p;
}

void say(const Probe& p, const char* what) {
    const Observation o = observeGame(true, false);  // the state only: the script keeps its events
    std::fprintf(stderr, "[skip-probe] frame %lu skip %d: %s | %s | context 0x%x | stars %d\n", p.frame, p.skips + 1, what,
                 CutsceneSkip::describe(), petari_skip_context(), o.powerStars);
    std::fflush(stderr);
}

void shot(const Probe& p, const char* label) {
    if (p.shots.empty()) return;
    const std::string file = p.shots + "/skip" + std::to_string(p.skips + 1) + "-" + label + ".png";
    if (Screenshot::request(file.c_str())) {
        std::fprintf(stderr, "[skip-probe] screenshot %s\n", file.c_str());
    }
}

void space(bool down) {
    Input::focusChanged(true);
    Input::keyEvent(Input::Key::Space, down, false);
}

// The game's own view of A on the first remote (what a jump or a talk reads).
bool gameSeesA() {
    return MR::testCorePadButtonA(WPAD_CHAN0) || MR::testCorePadTriggerA(WPAD_CHAN0);
}

void next(Probe& p, Phase phase) {
    p.phase = phase;
    p.phaseFrames = 0;
}

// A yes/no on screen with Space released (PETARI_SKIP_PROBE_ANSWER=yes): point at the game's
// published Prompt.Yes and tap A once it is under the pointer, as a player answers.
void answer(Probe& p, unsigned context) {
    p.wantPointer = false;
    if (p.releaseAt != 0 && p.frame >= p.releaseAt) {
        space(false);
        p.releaseAt = 0;
    }
    if ((context & PetariNative::Skip::kChoice) == 0) {
        p.answered = false;
        p.pointing = 0;
        return;
    }
    if (p.answered) return;
    const Observation o = observeGame(true, true);  // takes the UI targets (the script is done by now)
    for (const Observation::Target& target : o.targets) {
        if (target.id != "Prompt.Yes" || (target.flags & kTargetSelectable) == 0) continue;
        p.wantPointer = true;
        p.pointerU = target.u;
        p.pointerV = target.v;
        p.pointing = (target.flags & kTargetPointing) != 0 ? p.pointing + 1 : 0;
        if (p.pointing >= 10) {
            say(p, "answering the yes/no: Yes (A), Space released before");
            space(true);
            p.releaseAt = p.frame + 6;
            p.answered = true;
        }
        return;
    }
}

}  // namespace

bool skipProbeAnswerPointer(float* u, float* v) {
    const Probe& p = probe();
    if (!p.enabled || !p.wantPointer) return false;
    *u = p.pointerU;
    *v = p.pointerV;
    return true;
}

bool skipProbeFrame() {
    Probe& p = probe();
    if (!p.enabled) return false;
    ++p.frame;
    ++p.phaseFrames;
    const unsigned context = petari_skip_context();
    if (p.answerYes && (p.phase == Phase::Idle || p.phase == Phase::Watch)) answer(p, context);
    if (p.startFinalBattle) {
        p.gameplayFrames = (context & PetariNative::Skip::kGameplay) != 0 ? p.gameplayFrames + 1 : 0;
        if (p.gameplayFrames >= 60 && MR::isExistSceneObj(SceneObj_MoviePlayingSequenceHolder)) {
            p.startFinalBattle = false;
            say(p, "TEST START (not gameplay): MR::startMovie(FinalBattle)");
            MR::startMovie(MoviePlayingSequence::MovieType_FinalBattle);
        }
    }
    const bool skippable = (context & PetariNative::Skip::kSkippable) != 0;
    const bool active = petari_skip_active();

    switch (p.phase) {
    case Phase::Idle:
        if (p.skips >= p.max) break;
        p.skippableFrames = skippable ? p.skippableFrames + 1 : 0;
        if (p.skippableFrames >= p.delay) {
            say(p, "holding Space");
            shot(p, "before");
            PetariNative::EfbDump::mark("skip-hold");
            space(true);
            p.heldGameA = 0;
            p.heldFrames = 0;
            next(p, Phase::Holding);
        }
        break;
    case Phase::Holding:
        ++p.heldFrames;
        if (gameSeesA()) ++p.heldGameA;
        if (active) {
            char text[64];
            std::snprintf(text, sizeof(text), "skip latched after %d frames held", p.phaseFrames);
            say(p, text);
            shot(p, "latched");
            p.heldGameA = 0;
            p.heldFrames = 0;
            next(p, Phase::Latched);
        } else if (p.phaseFrames > PetariNative::Skip::kHoldFrames + 60) {
            char text[96];
            std::snprintf(text, sizeof(text), "NO LATCH: released; the game saw A on %d of %d held frames", p.heldGameA, p.heldFrames);
            say(p, text);
            space(false);
            p.skippableFrames = 0;
            ++p.skips;
            next(p, Phase::Idle);
        }
        break;
    case Phase::Latched:
    case Phase::Tail: {
        // From the latch on, the game must not see the held A at all.
        ++p.heldFrames;
        if (gameSeesA()) ++p.heldGameA;
        if (p.phase == Phase::Latched) {
            // Each change of what is on screen while the skip runs (demo names, talks, movies).
            if (p.shown != CutsceneSkip::describe()) {
                p.shown = CutsceneSkip::describe();
                say(p, "now showing");
            }
            if (p.phaseFrames % 120 == 0) say(p, "still skipping");
            if (!active) {
                char text[64];
                std::snprintf(text, sizeof(text), "skip ended after %d frames", p.phaseFrames);
                say(p, text);
                shot(p, "ended");
                next(p, Phase::Tail);
            }
        } else if (p.phaseFrames >= p.tail) {
            say(p, "releasing Space");
            space(false);
            p.watchStarted = false;
            p.maxRise = 0.0f;
            p.groundFrames = 0;
            next(p, Phase::Watch);
        }
        break;
    }
    case Phase::Watch: {
        const Observation o = observeGame(true, false);
        if (p.phaseFrames <= 6) {  // the release reaches the game within a few reports
            ++p.heldFrames;
            if (gameSeesA()) ++p.heldGameA;
        }
        if (o.playerValid) {
            if (!p.watchStarted) {
                p.watchStarted = true;
                p.startX = o.playerX;
                p.startY = o.playerY;
                p.startZ = o.playerZ;
            }
            const float rise = -((o.playerX - p.startX) * o.gravityX + (o.playerY - p.startY) * o.gravityY +
                                 (o.playerZ - p.startZ) * o.gravityZ);
            if (rise > p.maxRise) p.maxRise = rise;
            if (o.playerOnGround) ++p.groundFrames;
        }
        if (p.phaseFrames >= p.watch) {
            char text[200];
            std::snprintf(text, sizeof(text),
                          "after: the game saw A on %d of %d frames of the latching hold and its release; player %s, "
                          "on ground %d of %d frames, max rise %.1f",
                          p.heldGameA, p.heldFrames, p.watchStarted ? "present" : "absent", p.groundFrames, p.watch,
                          p.maxRise);
            say(p, text);
            shot(p, "after");
            ++p.skips;
            p.skippableFrames = 0;
            next(p, Phase::Idle);
        }
        break;
    }
    }
    return p.phase == Phase::Holding || p.phase == Phase::Latched || p.phase == Phase::Tail;
}

}  // namespace PetariNative::App::Smoke
