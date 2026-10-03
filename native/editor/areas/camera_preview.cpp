#include <petari/editor/camera_preview.hpp>
namespace PetariEditor::CameraPreview {
    namespace {
        Request input;
        View output;
    }  // namespace
    void request(RowId area, Source source) {
        input.area = area;
        input.source = source;
        ++input.serial;
    }
    Request current() {
        return input;
    }
    void publish(View view) {
        output = std::move(view);
    }
    const View& view() {
        return output;
    }
    void acknowledge(const void* copy) {
        if (output.copy == copy)
            output.copy = nullptr;
    }
    bool awaitingReadback() {
        return output.copy != nullptr;
    }
}  // namespace PetariEditor::CameraPreview
