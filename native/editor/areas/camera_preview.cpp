#include <petari/editor/camera_preview.hpp>
namespace PetariEditor::CameraPreview {
    namespace {
        Request input;
        View output;
        PlayRequest playInput;
        View playOutput;
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
        if (playOutput.copy == copy)
            playOutput.copy = nullptr;
    }
    bool awaitingReadback() {
        return output.copy != nullptr;
    }
    void requestPlay(bool enabled) {
        playInput.enabled = enabled;
        if (enabled)
            ++playInput.serial;
    }
    PlayRequest currentPlay() {
        return playInput;
    }
    void publishPlay(View view) {
        playOutput = std::move(view);
    }
    const View& playView() {
        return playOutput;
    }
    bool awaitingPlayReadback() {
        return playOutput.copy != nullptr;
    }
}  // namespace PetariEditor::CameraPreview
