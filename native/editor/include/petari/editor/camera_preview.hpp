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
        float fovy = 45;
        std::uint64_t serial = 0;
    };
    void request(RowId area, Source source);
    Request current();
    void publish(View view);
    const View& view();
    void acknowledge(const void* copy);
    bool awaitingReadback();
}  // namespace PetariEditor::CameraPreview
