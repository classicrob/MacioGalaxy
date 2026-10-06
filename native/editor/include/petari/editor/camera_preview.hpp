#pragma once
#include <petari/editor/edit_api.hpp>
#include <string>
namespace PetariEditor::CameraPreview {
    enum class Source { Marker, Mario };
    struct Request {
        RowId area;
        Source source = Source::Marker;
        std::uint64_t serial = 0;
    };
    struct View {
        RowId area;
        const void* copy = nullptr;
        std::string message;
        Vec3 eye, target;
        float fovy = 45, roll = 0;
        std::uint64_t serial = 0;
        // The play view's camera: "Camera area", "Ground camera" or "Default camera", its parameter ID ("c:0003")
        // and type ("CAM_TYPE_FOLLOW"). area: the camera area it came from, if any.
        std::string camera, cameraId, cameraType;
    };
    void request(RowId area, Source source);
    Request current();
    void publish(View view);
    const View& view();
    void acknowledge(const void* copy);  // either view's copy
    bool awaitingReadback();

    // The play view (friction #14): what Play would show from the play marker (else from Mario), whatever is
    // selected. The game picks the camera as play does: the highest-priority camera area containing the marker,
    // else the ground's collision camera, else the default camera. Its own request, picture and View, beside the
    // selected area's preview above.
    struct PlayRequest {
        bool enabled = false;
        std::uint64_t serial = 0;
    };
    void requestPlay(bool enabled);  // enabled: render again (the UI asks ~10 times a second); false: stop
    PlayRequest currentPlay();
    void publishPlay(View view);
    const View& playView();
    bool awaitingPlayReadback();
}  // namespace PetariEditor::CameraPreview
