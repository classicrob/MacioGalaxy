#include "edit_model.hpp"
#include <petari/editor/catalog.hpp>
#include <petari/editor/placement.hpp>

#include <cstdio>
#include <cstdlib>
#include <map>

namespace E = PetariEditor;
namespace F = E::Formats;
namespace C = E::Catalog;

namespace {
    int checks = 0;
    void check(bool value, const char* label) {
        ++checks;
        if (!value) {
            std::fprintf(stderr, "FAIL: %s\n", label);
            std::exit(1);
        }
    }
    F::CellValue integer(int x) {
        return {F::CellValue::Kind::Int, x, 0, {}};
    }
    F::CellValue text(const char* x) {
        return {F::CellValue::Kind::String, 0, 0, x};
    }
}  // namespace

int main() {
    std::string error;
    check(C::load(&error), "catalog loads");
    for (const char* name : {"SuperSpinDriver", "SuperSpinDriverGreen", "SuperSpinDriverPink"})
        check(C::placementLink(C::find(name), E::columnHash("CommonPath_ID")) == C::PlacementLink::Required,
              "every Launch Star colour keeps its required path");
    C::ClassInfo custom;
    custom.name = "PlacementFixture";
    custom.links = {{C::LinkKind::Rail, "CommonPath_ID", true}, {C::LinkKind::Camera, "Obj_arg7", false}, {C::LinkKind::Demo, "CustomDemo", false}};
    C::addClass(std::move(custom));
    const auto* info = C::find("PlacementFixture");
    check(C::placementLink(info, E::columnHash("l_id")) == C::PlacementLink::None, "identity is not donor wiring");
    check(C::placementLink(info, E::columnHash("ShapeModelNo")) == C::PlacementLink::None, "model variant remains a setting");
    check(C::placementLink(info, E::columnHash("CommonPath_ID")) == C::PlacementLink::Required, "required catalog link wins");
    check(C::placementLink(nullptr, E::columnHash("SW_EXTRA"), "SW_EXTRA") == C::PlacementLink::Optional, "switch prefix is wiring");

    F::Table scenarios;
    scenarios.addRow(-1);
    scenarios.addColumn(E::columnHash("ScenarioNo"), F::ColumnType::Long, integer(1));
    scenarios.addColumn(E::columnHash("TestGalaxy"), F::ColumnType::Long, integer(1));
    F::Table zones;
    zones.addRow(-1);
    zones.addColumn(E::columnHash("ZoneName"), F::ColumnType::StringOffset, text("TestGalaxy"));
    zones.addRow(-1);
    zones.set(1, zones.column(E::columnHash("ZoneName")), text("ChildZone"));
    F::Table objects;
    objects.addRow(-1);
    objects.addColumn(E::columnHash("name"), F::ColumnType::StringOffset, text("PlacementFixture"));
    for (const char* field : {"l_id", "SW_APPEAR", "SW_SLEEP", "SW_A", "ViewGroupId", "ClippingGroupId", "CameraSetId", "GroupId", "CommonPath_ID",
                              "Obj_arg7", "Obj_arg0", "ShapeModelNo"})
        objects.addColumn(E::columnHash(field), F::ColumnType::Long, integer(75));
    objects.addColumn(E::columnHash("CustomDemo"), F::ColumnType::StringOffset, text("OldDemo"));
    objects.addColumn(E::columnHash("scale_x"), F::ColumnType::Float, {F::CellValue::Kind::Float, 0, 2.5f, {}});
    std::map< std::string, F::Bytes > disc{
        {"/StageData/TestGalaxy/TestGalaxyScenario.arc",
         F::Archive::create("stage", {{"ScenarioData.bcsv", scenarios.build()}, {"ZoneList.bcsv", zones.build()}}).build()},
        {"/StageData/TestGalaxy.arc", F::Archive::create("stage", {{"jmp/placement/common/objinfo", objects.build()}}).build()},
        {"/StageData/ChildZone.arc", F::Archive::create("stage", {{"jmp/placement/common/objinfo", objects.build()}}).build()}};
    auto model = E::EditModel::load(
        "TestGalaxy", 1,
        [&](const std::string& path) -> std::optional< F::Bytes > {
            const auto it = disc.find(path);
            return it == disc.end() ? std::nullopt : std::optional< F::Bytes >(it->second);
        },
        &error);
    check(bool(model), "fixture loads");
    check(E::Placement::visibleTable(*model, 0, "jmp/placement/layera/objinfo") == "jmp/placement/layera/objinfo",
          "active destination layer retained");
    check(E::Placement::visibleTable(*model, 0, "jmp/placement/layerb/objinfo") == "jmp/placement/common/objinfo",
          "inactive destination layer uses Common");
    check(E::Placement::visibleTable(*model, 1, "jmp/placement/layera/objinfo") == "jmp/placement/common/objinfo",
          "cross-zone donor uses destination visibility");
    check(E::Placement::visibleTable(*model, 1, "jmp/path/commonpathinfo") == "jmp/path/commonpathinfo", "non-layer table unchanged");
    E::RowId donor;
    model->forEachRow(0, 0, [&](E::RowId row) { donor = row; });
    E::AddRow add;
    add.id = E::newRowId();
    add.zone = 0;
    add.table = "jmp/placement/common/objinfo";
    add.copyOf = donor;
    E::Placement::cleanDonorLinks(*model, donor, add);
    E::Op op;
    op.label = "Place independent fixture";
    op.adds = {add};
    check(model->apply(op, &error), "placement applies");
    for (const char* field : {"SW_APPEAR", "SW_SLEEP", "SW_A", "ViewGroupId", "ClippingGroupId", "CameraSetId", "GroupId", "Obj_arg7"}) {
        check(model->cell(add.id, E::columnHash(field))->i == -1, "optional link cleared");
        check(model->cell(donor, E::columnHash(field))->i == 75, "donor unchanged");
    }
    check(model->cell(add.id, E::columnHash("CustomDemo"))->s.empty(), "string catalog link cleared");
    check(model->cell(add.id, E::columnHash("CommonPath_ID"))->i == 75, "required flight path kept");
    check(model->cell(add.id, E::columnHash("Obj_arg0"))->i == 75 && model->cell(add.id, E::columnHash("ShapeModelNo"))->i == 75,
          "ordinary settings and shape variant kept");
    check(model->cell(add.id, E::columnHash("scale_x"))->f == 2.5f, "donor scale kept");
    check(model->cell(add.id, E::columnHash("l_id"))->i != 75, "new identity allocated");
    check(E::Placement::retainsZoneLinks(*model, donor), "required rail anchors the zone");
    check(model->historySize() == 1 && model->undo() && !model->locate(add.id, nullptr, nullptr), "one undo removes placement");
    check(model->redo() && model->cell(add.id, E::columnHash("SW_APPEAR"))->i == -1, "redo retains clean placement");

    E::AddRow kept = add;
    kept.id = E::newRowId();
    kept.cells.clear();
    E::Placement::cleanDonorLinks(*model, donor, kept, true);
    check(kept.cells.empty(), "Keep links adds no reset overrides");
    op.adds = {kept};
    check(model->apply(op, &error) && model->cell(kept.id, E::columnHash("SW_APPEAR"))->i == 75, "Keep links preserves donor wiring");
    E::Op ordinary;
    ordinary.sets = {{donor, E::columnHash("name"), {E::CellType::StringOffset, 0, 0, "Coin"}}};
    check(model->apply(ordinary, &error), "change donor to optional-path class");
    check(!E::Placement::retainsZoneLinks(*model, donor), "clean optional links permit destination zone");
    check(E::Placement::retainsZoneLinks(*model, donor, true), "Keep links retains donor zone");
    std::printf("PASS: %d clean placement checks\n", checks);
}
