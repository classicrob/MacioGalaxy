#pragma once
// Native mod: Generic world (Mods::Mod::GenericWorld, native/MODS.md). Terrain,
// sky and object models get plain procedural textures when they load, and the
// common objects (enemies, coins, stars, springs, launch stars) are drawn as
// plain shapes instead of their models. Geometry, collision and behaviour are
// the game's. Every function returns at once (false) while the mod is off.
#ifdef PETARI_NATIVE

class DrawBuffer;
class J3DModelData;

namespace GenericWorld {
    bool on();
    // ResourceHolder: a model was loaded from archive (its root folder name) and
    // file. Rewrites its textures in place (or restores them with the mod off)
    // and notes whether its instances get a stand-in.
    void onModelData(J3DModelData* pModelData, const char* pArchive, const char* pFile);
    // ResourceHolder, before J3D loads a model file (.bdl/.bmd): its material
    // colours (MAT3 tables, MDL3 display lists) lose their hue, keeping brightness.
    void onModelFile(void* pFile, const char* pArchive);
    // DrawBuffer::drawOpa/drawXlu: true when the buffer's model has a stand-in;
    // the stand-ins are drawn (opaque pass) and the model is not.
    bool drawBuffer(const DrawBuffer* pBuffer, bool opaque);
}  // namespace GenericWorld

#endif
