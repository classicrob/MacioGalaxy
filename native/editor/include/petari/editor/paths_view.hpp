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
        float fromX = 0, fromY = 0, toX = 0, toY = 0;
        bool midpointVisible = false, fromVisible = false, toVisible = false;
        std::vector< Curve > segments;
    };
    struct View {
        std::vector< Mark > marks;
        std::vector< Curve > curves;
        std::vector< SocketMark > sockets;
        std::vector< WireMark > wires;
        bool dragging = false;
        bool creating = false, flight = false, groundHit = false;
        int draftPoints = 0;
        std::vector< Mark > draftMarks;
        std::string message;
    };
    struct Input {
        bool begin = false, move = false, end = false, cancel = false;
        RowId point;
        int handle = 0;
        float x = 0, y = 0;
    };
    struct CreateInput {
        bool begin = false, add = false, finish = false, cancel = false, remove = false, hover = false;
        RowId rider;
        float x = 0, y = 0;
    };
    void beginCreate(RowId rider = {});
    bool creating();
    void sendCreate(CreateInput input);
    CreateInput takeCreate();
    void endCreate();
    void publish(View view);
    const View& view();
    void send(Input input);
    Input take();
    void focusWires(RowId hovered, std::string hoverField, RowId selected, std::string selectedField);
    bool wireFocused(const Paths::Socket& socket);
    void setVisibility(Visibility value);
    Visibility visibility();
    void setLinkingPath(bool value);
    bool linkingPath();
    void setLinkingSwitch(bool value);
    bool linkingSwitch();
    void setSocketMenu(RowId row);
    RowId socketMenu();
}  // namespace PetariEditor::PathsView
