#pragma once
// Hold-to-skip mod (Mods::Mod::SkipCutscenes, native/MODS.md "Skip cutscenes
// and dialogue"). Holding A (Space by default) for kHoldFrames game frames
// while a cutscene or dialogue is on screen latches a skip that lasts until
// gameplay resumes; the game side (Game/System/CutsceneSkip.hpp) ends each kind
// of cutscene through the game's own end path while the skip is latched.
//
// This is the state machine alone: no game headers, driven once per game frame.

#ifdef __cplusplus
namespace PetariNative::Skip {

constexpr int kHoldFrames = 90;       // 1.5 s at 60 frames a second
// Or 1.5 s of held time when the game runs slower than 60 frames a second, after at
// least kMinHoldFrames frames; one frame counts for at most kMaxFrameSeconds, so a
// single stall (a load) cannot latch a skip by itself.
constexpr double kHoldSeconds = 1.5;
constexpr int kMinHoldFrames = 10;
constexpr double kMaxFrameSeconds = 0.25;
constexpr int kSettleFrames = 10;     // gameplay this many frames in a row ends a skip
constexpr int kTimeoutFrames = 3600;  // a skip that has not ended after 60 s gives up
constexpr unsigned kButtonA = 0x0800; // WPAD_BUTTON_A, as KPAD reports it

// What the game shows, taken after each frame's scene update.
enum Context : unsigned {
    kSkippable = 1u,   // a cutscene, movie or dialogue that a hold may skip
    kGameplay = 2u,    // the player has control: no demo, talk, movie or wipe
    kChoice = 4u,      // a yes/no choice or save prompt waits for the player
    kNewStage = 8u,    // the stage or scene changed since the previous context
};

enum class End { None, Gameplay, Choice, Stage, Timeout, Disabled };

class Machine {
public:
    // One game frame. aHeld: A as the remote reports it, before any filtering;
    // frameSeconds: the time since the previous frame. Returns the buttons to hide
    // from the game this frame (kButtonA or 0).
    unsigned frame(bool aHeld, unsigned context, bool enabled, double frameSeconds = 1.0 / 60.0);

    bool latched() const { return mLatched; }
    bool swallowing() const { return mSwallow; }
    int holdFrames() const { return mHold; }
    // 0..1 while A is held toward a skip (for a fill ring), 1 while latched.
    float progress() const;
    // What the last frame did: true on the frame the skip latched; the reason
    // on the frame it ended (End::None otherwise).
    bool justLatched() const { return mJustLatched; }
    End justEnded() const { return mJustEnded; }
    int latchedFrames() const { return mLatchedFrames; }

private:
    void end(End reason);

    int mHold = 0;
    double mHoldSeconds = 0.0;
    bool mLatched = false;
    bool mSwallow = false;
    int mSettle = 0;
    int mLatchedFrames = 0;
    bool mJustLatched = false;
    End mJustEnded = End::None;
};

const char* endName(End reason);

// The game's machine (petari_skip_* below). Tests only: a fresh one, whose
// frames each count 1/60 s whatever the wall clock says.
Machine& machine();
void resetForTesting();

}  // namespace PetariNative::Skip
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Game side. The context (PetariNative::Skip::Context bits) after a frame's
// scene update; the next petari_skip_pad uses it.
void petari_skip_set_context(unsigned context);
// WPadButton::update on remote channel 0, once per game frame: steps the
// machine with the raw KPAD hold and returns the buttons to hide from the game.
unsigned petari_skip_pad(unsigned rawHold);
// The skip is latched: the game should end what is on screen.
bool petari_skip_active(void);
// 0..1 hold progress (1 while latched), for a fill ring.
float petari_skip_progress(void);
// The last context the game set (tests and the smoke probe).
unsigned petari_skip_context(void);

#ifdef __cplusplus
}
#endif
