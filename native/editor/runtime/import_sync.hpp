#pragma once
namespace PetariEditor {
    class EditModel;
}
namespace PetariEditor::ImportSync {
    void registerWithEditor();
    void placeRequested(EditModel& model);
}  // namespace PetariEditor::ImportSync
