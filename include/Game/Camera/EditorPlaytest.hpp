#pragma once
#ifdef PETARI_NATIVE
class LiveActor;
namespace EditorPlaytest {
    void init();
    void update(bool playable);
    void draw();
    void testTick(bool playable);
    bool collectStar(LiveActor* star);
    bool miss();
    bool missPending();
}  // namespace EditorPlaytest
#endif
