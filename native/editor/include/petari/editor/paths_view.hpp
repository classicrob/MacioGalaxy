#pragma once
#include <petari/editor/debug_draw.hpp>
#include <petari/editor/paths.hpp>

namespace PetariEditor::PathsView {
    enum class Visibility { Selection, All, None };
    struct Mark {
        RowId rail, point;
        int handle = 0;  // 0 point, 1 incoming, 2 outgoing
        float x = 0, y = 0;
    };
    struct Curve {
        RowId rail;
        int segment = 0;
        float t0 = 0, t1 = 0;
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    };
    struct SocketMark {
        RowId row;
        std::string field;
        float x = 0, y = 0;
        std::uint32_t color = 0xffffffff;
        bool output = false;
    };
    struct WireMark {
        Paths::Socket from, to;
        float x = 0, y = 0;
        std::vector< Curve > segments;
    };
    struct View {
        std::vector< Mark > marks;
        std::vector< Curve > curves;
        std::vector< SocketMark > sockets;
        std::vector< WireMark > wires;
        bool dragging = false;
        std::string message;
    };
    struct Input {
        bool begin = false, move = false, end = false, cancel = false;
        RowId point;
        int handle = 0;
        float x = 0, y = 0;
    };
    void publish(View view);
    const View& view();
    void send(Input input);
    Input take();
    void setVisibility(Visibility value);
    Visibility visibility();
    void setLinkingPath(bool value);
    bool linkingPath();
}  // namespace PetariEditor::PathsView
