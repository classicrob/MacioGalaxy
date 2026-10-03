#include <petari/editor/catalog.hpp>
#include <petari/editor/placement.hpp>

#include <algorithm>
#include <cstring>

namespace PetariEditor::Catalog {
    PlacementLink placementLink(const ClassInfo* info, std::uint32_t field, const char* fieldName) {
        bool declared = false;
        if (info) {
            for (const auto& link : info->links) {
                if (columnHash(link.field.c_str()) != field)
                    continue;
                if (link.required)
                    return PlacementLink::Required;
                declared = true;
            }
            for (const auto& sw : info->switches)
                declared |= columnHash(sw.field.c_str()) == field;
        }
        if (declared || (fieldName && std::strncmp(fieldName, "SW_", 3) == 0))
            return PlacementLink::Optional;
        // These are placement-system links even when a class doesn't read them itself.
        static const char* const common[] = {
            "SW_APPEAR",     "SW_DEAD",         "SW_A",        "SW_B",        "SW_SLEEP",   "SW_AWAKE",  "SW_PARAM",
            "GroupId",       "ClippingGroupId", "ViewGroupId", "DemoGroupId", "CastId",     "MessageId", "CameraSetId",
            "CommonPath_ID", "FollowId",        "ParentID",    "ChildObjId",  "MapParts_ID"};
        for (const char* name : common)
            if (columnHash(name) == field)
                return PlacementLink::Optional;
        return PlacementLink::None;
    }
}  // namespace PetariEditor::Catalog

namespace PetariEditor::Placement {
    namespace {
        bool linked(const Cell& value) {
            if (value.type == CellType::String || value.type == CellType::StringOffset)
                return !value.s.empty();
            return value.type == CellType::Float ? value.f >= 0 : value.i >= 0;
        }
    }  // namespace

    void cleanDonorLinks(const ModelView& model, RowId donor, AddRow& add, bool keepLinks) {
        if (keepLinks)
            return;
        int zone, table;
        if (!model.locate(donor, &zone, &table))
            return;
        const char* name = model.objectName(donor);
        const auto* info = name ? Catalog::find(name) : nullptr;
        for (const auto& column : model.columns(zone, table)) {
            if (Catalog::placementLink(info, column.hash, column.name) != Catalog::PlacementLink::Optional)
                continue;
            const auto* value = model.cell(donor, column.hash);
            if (!value)
                continue;
            Cell neutral = *value;
            neutral.i = -1;
            neutral.f = -1;
            neutral.s.clear();
            const auto it = std::find_if(add.cells.begin(), add.cells.end(), [&](const auto& v) { return v.column == column.hash; });
            if (it == add.cells.end())
                add.cells.push_back({column.hash, neutral});
            else
                it->value = std::move(neutral);
        }
    }

    bool retainsZoneLinks(const ModelView& model, RowId donor, bool keepLinks) {
        int zone, table;
        if (!model.locate(donor, &zone, &table))
            return false;
        const char* name = model.objectName(donor);
        const auto* info = name ? Catalog::find(name) : nullptr;
        for (const auto& column : model.columns(zone, table)) {
            const auto policy = Catalog::placementLink(info, column.hash, column.name);
            if (policy == Catalog::PlacementLink::None || (!keepLinks && policy != Catalog::PlacementLink::Required))
                continue;
            const auto* value = model.cell(donor, column.hash);
            if (value && linked(*value))
                return true;
        }
        return false;
    }
}  // namespace PetariEditor::Placement
