#pragma once
// Native mod: Cappy, Super Mario Odyssey's cap throw (docs/dev/CAPPY.md). The
// cap's rules are native/include/petari/cappy.hpp; MarioCappy.cpp applies them
// to Galaxy's Mario. Every function returns at once while the Cappy mod is off.
#ifdef PETARI_NATIVE

class MarioActor;
class Mario;

// MarioActor::initParts: the cap and hair models (only with the mod on).
void petariCappyInit(MarioActor* pActor);
// MarioActor::controlMain: the throw button's presses and hold.
void petariCappyInput(MarioActor* pActor);
// MarioActor::updateSwingAction, first: a buffered press throws Cappy, or does
// SMO's other Y moves (dive in a ground pound, roll from a crouch, roll boost).
void petariCappySwingAction(MarioActor* pActor);
// MarioActor::controlMain, after Mario's update: Cappy's flight, the cap
// bounce, the models.
void petariCappyUpdate(MarioActor* pActor);
// MarioJump.cpp: a wall jump removes the cap bounce until the next landing.
void petariCappyWallJump();
// MarioJump.cpp: a dive started; while Cappy is out it keeps him there (as a throw press).
void petariCappyDiveStarted();
// The Generic character mod (GenericFigure.cpp): while Cappy is out, his matrix
// (axes scaled by his drawn size, origin his centre) and true; false while he
// is on Mario's head or the mod is off.
bool petariCappyCapMtx(float (*pOut)[4]);

// MarioJump.cpp (the movement mod's air state lives there).
// The air throw: SMO's hop (no new hop above the last throw's height, `hop`).
bool petariOdysseyAirThrow(Mario* pMario, bool hop);
// Bounce off Cappy: from a dive, a ground pound, or anything else in the air.
void petariOdysseyCapBounce(Mario* pMario);

#endif
