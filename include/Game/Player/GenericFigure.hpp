#pragma once
// Native mod: Generic character (Mods::Mod::GenericCharacter, native/MODS.md).
// The player is drawn as a plain figure built here from ellipsoids and
// capsules, placed on Mario's own joints, so it has his proportions and every
// animation; gameplay is untouched. Every function returns at once (false)
// while the mod is off.
#ifdef PETARI_NATIVE

class MarioActor;

namespace GenericFigure {
    bool on();
    // MarioActor::controlMain, after Cappy: hides the Boo model's materials
    // (the ghost is drawn instead) and shows them again with the mod off.
    void update(MarioActor* pActor);
    // MarioActor::drawMarioModel: true when the figure replaced Mario's models
    // (body, hands, face, ice) this frame.
    bool drawPlayer(const MarioActor* pActor);
    // MarioActor::drawSilhouette: true when Mario's see-through-walls
    // silhouette is skipped (it is drawn from his own models).
    bool drawSilhouette(const MarioActor* pActor);
    // MarioActor::draw: the plain ghost while the player is Boo.
    void drawBoo(const MarioActor* pActor);
}  // namespace GenericFigure

#endif
