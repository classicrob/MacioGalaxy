#pragma once
#include <petari/editor/ui.hpp>
namespace PetariEditor::Ui::Host {
    ThumbnailReadback readback();
    ThumbnailTexture makeTexture();
    ThumbnailTextureUpdate updateTexture();
    FileDialog fileDialog();  // null: no native panels (tests); sheets fall back to a folder field
}  // namespace PetariEditor::Ui::Host
