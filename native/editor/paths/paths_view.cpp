#include <petari/editor/paths_view.hpp>
#include <utility>
namespace PetariEditor::PathsView {
    namespace {
        View snapshot;
        Input pending;
        Visibility shown = Visibility::Selection;
        bool pathLink = false;
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
