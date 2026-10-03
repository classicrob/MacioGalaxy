#include "host.hpp"
namespace PetariEditor::Ui {
    namespace {
        ThumbnailReadback read;
        ThumbnailTexture make;
        ThumbnailTextureUpdate update;
    }  // namespace
    void setThumbnailHost(ThumbnailReadback reader, ThumbnailTexture maker, ThumbnailTextureUpdate updater) {
        read = reader;
        make = maker;
        update = updater;
    }
    namespace Host {
        ThumbnailReadback readback() {
            return read;
        }
        ThumbnailTexture makeTexture() {
            return make;
        }
        ThumbnailTextureUpdate updateTexture() {
            return update;
        }
    }  // namespace Host
}  // namespace PetariEditor::Ui
