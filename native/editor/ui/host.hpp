#pragma once
#include <petari/editor/ui.hpp>
namespace PetariEditor::Ui::Host {
    ThumbnailReadback readback();
    ThumbnailTexture makeTexture();
    ThumbnailTextureUpdate updateTexture();
}  // namespace PetariEditor::Ui::Host
