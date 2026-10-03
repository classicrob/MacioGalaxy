#include <petari/editor/paths_view.hpp>
#include <utility>
namespace PetariEditor::PathsView {
    namespace {
        View snapshot;
        Input pending;
        CreateInput pendingCreate;
        bool createActive = false;
        Visibility shown = Visibility::Selection;
        bool pathLink = false, switchLink = false;
        RowId hoveredWire, selectedWire, menuRow;
        std::string hoveredField, selectedField;
    }  // namespace
    void beginCreate(RowId rider) {
        pendingCreate = {};
        pendingCreate.begin = true;
        pendingCreate.rider = rider;
        createActive = true;
        shown = Visibility::Selection;
    }
    bool creating() {
        return createActive;
    }
    void sendCreate(CreateInput input) {
        pendingCreate.add |= input.add;
        pendingCreate.finish |= input.finish;
        pendingCreate.cancel |= input.cancel;
        pendingCreate.remove |= input.remove;
        pendingCreate.hover = input.hover;
        pendingCreate.x = input.x;
        pendingCreate.y = input.y;
    }
    CreateInput takeCreate() {
        auto out = pendingCreate;
        pendingCreate = {};
        return out;
    }
    void endCreate() {
        pendingCreate = {};
        createActive = false;
    }
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
    void setSocketMenu(RowId row) {
        menuRow = row;
    }
    RowId socketMenu() {
        return menuRow;
    }
    void setLinkingSwitch(bool value) {
        switchLink = value;
    }
    bool linkingSwitch() {
        return switchLink;
    }
    bool linkingPath() {
        return pathLink;
    }
}  // namespace PetariEditor::PathsView
