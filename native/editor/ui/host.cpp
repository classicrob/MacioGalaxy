#include "host.hpp"
namespace PetariEditor::Ui {
    namespace {
        ThumbnailReadback read;
        ThumbnailTexture make;
        ThumbnailTextureUpdate update;
        FileDialog dialog;
    }  // namespace
    void setFileDialogHost(FileDialog host) {
        dialog = host;
    }
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
        FileDialog fileDialog() {
            return dialog;
        }
    }  // namespace Host
}  // namespace PetariEditor::Ui
