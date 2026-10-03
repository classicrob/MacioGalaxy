#include <petari/editor/paths_view.hpp>
#include <utility>
namespace PetariEditor::PathsView {
    namespace {
        View snapshot;
        Input pending;
        Visibility shown = Visibility::Selection;
        bool pathLink = false;
        RowId hoveredWire, selectedWire;
        std::string hoveredField, selectedField;
    }  // namespace
    void publish(View value) {
        snapshot = std::move(value);
    }
    const View& view() {
        return snapshot;
    }
    void send(Input value) {
        pending = value;
    }
    Input take() {
        Input out = pending;
        pending = {};
        return out;
    }
    void focusWires(RowId hovered, std::string hoverField, RowId selected, std::string selectionField) {
        hoveredWire = hovered;
        hoveredField = std::move(hoverField);
        selectedWire = selected;
        selectedField = std::move(selectionField);
    }
    bool wireFocused(const Paths::Socket& socket) {
        return (socket.row == hoveredWire && socket.field == hoveredField) || (socket.row == selectedWire && socket.field == selectedField);
    }
    void setVisibility(Visibility value) {
        shown = value;
    }
    Visibility visibility() {
        return shown;
    }
    void setLinkingPath(bool value) {
        pathLink = value;
    }
    bool linkingPath() {
        return pathLink;
    }
}  // namespace PetariEditor::PathsView
