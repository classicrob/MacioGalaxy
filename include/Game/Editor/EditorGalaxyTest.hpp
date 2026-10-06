#pragma once

#ifdef PETARI_NATIVE
// The new-galaxy live check (spike S8; src/Game/Editor/EditorGalaxyTest.cpp). GameScene::update
// calls it every frame; nothing happens unless PETARI_EDITOR_GALAXY_TEST is set.
namespace EditorGalaxyTest {
    void update(bool actionNerve);
}
#endif
