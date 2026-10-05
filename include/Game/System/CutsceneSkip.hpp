#pragma once

#ifdef PETARI_NATIVE
// Hold-to-skip mod (native/MODS.md "Skip cutscenes and dialogue"; the hold and
// latch are petari/skip.hpp). While a skip is latched, each kind of cutscene
// ends through the game's own end path: movies, the galaxy-entry camera and
// the spin-driver launch take the game's existing skip, talk pages finish as
// if A were pressed (TalkStateEvent::talk), and demos run the game's own
// scene update several times a frame until they end (the fast-forward), so
// every step, callback, flag and scene change happens in order.
namespace CutsceneSkip {
    // Game thread, after each scene update and calcAnim: tells the skip what
    // the game shows (petari_skip_set_context).
    void noteFrame();
    // A skip is latched: whatever is on screen should end.
    bool isActive();
    // GameSystem::frameLoop: another scene update should run this frame. The
    // first call of a frame starts its time budget.
    bool continueFastForward(bool firstStep);
    // Before each extra scene update: the pads keep held buttons, lose new presses.
    void beginFastForwardStep();
    // Game sound effects (not system sounds) are muted while demos fast-forward.
    bool isMutingSE();
    // AudSystem/AudSoundObject: a sound effect start was refused by the mute (counted,
    // and logged when the skip ends).
    void noteMutedSE();
    // A menu that waits for the player (pointer choices with no A default, e.g.
    // the storybook's chapter list) is on screen this update: ends a skip like a choice.
    void noteWaitingForPlayer();
    // What is on screen, for logs: "movie 120/1800", "demo <name>", "talk",
    // "stage clear", "galaxy-entry camera", "spin-driver launch", "gameplay", ...
    const char* describe();
}  // namespace CutsceneSkip
#endif
