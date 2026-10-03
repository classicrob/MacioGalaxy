#pragma once

#include <petari/editor/edit_api.hpp>

namespace PetariEditor::Placement {
    /// Use Common when the destination layer is inactive in the mission being edited.
    std::string visibleTable(const ModelView& model, int zone, std::string table);
    /// Add neutral overrides for the donor's optional switch and object links. Keep links is an
    /// explicit Browser option; duplication and arrays do not call this helper.
    void cleanDonorLinks(const ModelView& model, RowId donor, AddRow& add, bool keepLinks = false);
    /// A retained link can reference the donor's zone. Keep that zone when choosing ground to place on.
    bool retainsZoneLinks(const ModelView& model, RowId donor, bool keepLinks = false);
}  // namespace PetariEditor::Placement
