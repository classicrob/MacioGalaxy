// The level editor's frame (petari/editor/ui.hpp): the dock layout and the game's viewport,
// headless (no window, no renderer). The presenter's calls are recorded instead.
#include <petari/editor/ui.hpp>

#include "../editor/runtime/runtime_core.hpp"
#include "choice_labels.hpp"
#include "galaxy_info.hpp"
#include "help.hpp"
#include "imgui_internal.h"
#include "journal.hpp"
#include "missions.hpp"
#include "panels.hpp"
#include "paths.hpp"
#include <petari/editor/paths_view.hpp>
#include <petari/editor_pick.hpp>

#include "imgui.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace {

int gFailures = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++gFailures;                                                           \
        }                                                                          \
    } while (0)

struct Area {
    float x = 0, y = 0, w = 0, h = 0;
    int calls = 0;
    int freeAspect = -1;
} gArea;

void frame(float width, float height) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(width, height);
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    PetariEditor::Ui::drawFrame(width, height);
    ImGui::Render();
}

bool near(float a, float b, float tolerance = 1.5f) {
    return std::fabs(a - b) <= tolerance;
}

}  // namespace

// The presenter (native/gx/present/present.h), recorded.
extern "C" void petari_present_set_content_area(float x, float y, float width, float height) {
    gArea.x = x;
    gArea.y = y;
    gArea.w = width;
    gArea.h = height;
    gArea.calls++;
}
extern "C" void petari_present_set_free_aspect(int enabled) {
    gArea.freeAspect = enabled;
}


// A two-zone model for the panels' logic: a changed Coin, an added Goomba, an untouched Rabbit.
using namespace PetariEditor;
struct FakeModel : ModelView {
    std::vector< Column > cols{{columnHash("name"), "name", CellType::String}, {columnHash("pos_x"), "pos_x", CellType::Float},
                                {columnHash("pos_y"), "pos_y", CellType::Float}, {columnHash("Obj_arg0"), "Obj_arg0", CellType::Int},
                                {0x12345678u, nullptr, CellType::Int}};
    struct R {
        RowId id;
        int zone;
        RowState state;
        std::map< std::uint32_t, Cell > now, disc;
    };
    std::vector< R > rows;
    FakeModel() {
        auto cellS = [](const char* s) { Cell c; c.type = CellType::String; c.s = s; return c; };
        auto cellF = [](float f) { Cell c; c.type = CellType::Float; c.f = f; return c; };
        auto cellI = [](int i) { Cell c; c.type = CellType::Int; c.i = i; return c; };
        R coin{{1}, 0, RowState::Changed, {}, {}};
        coin.disc = {{cols[0].hash, cellS("Coin")}, {cols[1].hash, cellF(10)}, {cols[2].hash, cellF(5)}, {cols[3].hash, cellI(-1)}, {cols[4].hash, cellI(7)}};
        coin.now = coin.disc;
        coin.now[cols[1].hash] = cellF(15);
        coin.now[cols[2].hash] = cellF(9);
        coin.now[cols[4].hash] = cellI(8);
        R goomba{{2}, 1, RowState::Added, {{cols[0].hash, cellS("Kuribo")}, {cols[1].hash, cellF(0)}, {cols[2].hash, cellF(0)}, {cols[3].hash, cellI(-1)}}, {}};
        R rabbit{{3}, 1, RowState::Original, {}, {}};
        rabbit.disc = {{cols[0].hash, cellS("Rabbit")}, {cols[1].hash, cellF(1)}, {cols[2].hash, cellF(2)}, {cols[3].hash, cellI(1)}};
        rabbit.now = rabbit.disc;
        R ghost{{4}, 1, RowState::Deleted, {{cols[0].hash, cellS("Coin")}}, {}};  // added, then deleted
        rows = {coin, goomba, rabbit, ghost};
    }
    const R* find(RowId id) const {
        for (const R& r : rows) if (r.id == id) return &r;
        return nullptr;
    }
    std::uint64_t revision() const override { return 1; }
    const char* galaxy() const override { return "TestGalaxy"; }
    int scenario() const override { return 1; }
    int zoneCount() const override { return 2; }
    const char* zoneName(int zone) const override { return zone == 0 ? "TestGalaxy" : "PuffballZone"; }
    bool zonePlaced(int) const override { return true; }
    Mtx34 zoneWorldMatrix(int) const override { return Mtx34{{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}}}; }
    std::uint32_t activeLayers(int, int) const override { return 1; }
    int tableCount(int) const override { return 1; }
    const char* tablePath(int, int) const override { return "jmp/placement/common/objinfo"; }
    const std::vector< Column >& columns(int, int) const override { return cols; }
    void forEachRow(int zone, int, const std::function< void(RowId) >& fn) const override {
        for (const R& r : rows) if (r.zone == zone) fn(r.id);
    }
    bool locate(RowId id, int* zone, int* table) const override {
        const R* r = find(id);
        if (r == nullptr) return false;
        if (zone)
            *zone = r->zone;
        if (table)
            *table = 0;
        return true;
    }
    RowState state(RowId id) const override { return find(id)->state; }
    const Cell* cell(RowId id, std::uint32_t c) const override {
        const R* r = find(id);
        auto it = r->now.find(c);
        return it == r->now.end() ? nullptr : &it->second;
    }
    const Cell* originalCell(RowId id, std::uint32_t c) const override {
        const R* r = find(id);
        auto it = r->disc.find(c);
        return it == r->disc.end() ? nullptr : &it->second;
    }
    const char* objectName(RowId id) const override { return cell(id, cols[0].hash)->s.c_str(); }
    bool hasTransform(RowId) const override { return true; }
    RowTransform transform(RowId) const override { return {}; }
    int historySize() const override { return 0; }
    int historyCursor() const override { return 0; }
    const char* historyLabel(int) const override { return ""; }
    int missionCount() const override { return 1; }
    const char* missionName(int) const override { return "Test mission"; }
    std::uint32_t rowMissions(RowId) const override { return 1; }
};

void testPanelsLogic() {
    namespace Ui = PetariEditor::Ui;
    const FakeModel model;
    const std::vector< Ui::Change > changes = Ui::collectChanges(model);
    CHECK(changes.size() == 2);  // the added-then-deleted Coin is no change
    CHECK(changes[0].name == "Coin" && changes[0].state == RowState::Changed && changes[0].zone == 0);
    // pos_x and pos_y read as one "Position"; an unnamed field by its hash.
    CHECK(changes[0].columns.size() == 2 && changes[0].columns[0] == "Position" && changes[0].columns[1] == "0x12345678");
    CHECK(changes[1].name == "Kuribo" && changes[1].state == RowState::Added && changes[1].columns.empty());

    Cell three;
    three.type = CellType::Int;
    three.i = 3;
    const Op set = Ui::setCellOp(model, {RowId{1}, RowId{3}}, columnHash("Obj_arg0"), three);
    CHECK(set.sets.size() == 2 && set.sets[1].row == RowId{3} && set.sets[1].after.i == 3);
    CHECK(set.label == "Set Obj_arg0 on 2 objects");
    // A column a row lacks is not written there.
    const Op unnamed = Ui::setCellOp(model, {RowId{1}, RowId{2}}, 0x12345678u, three);
    CHECK(unnamed.sets.size() == 1 && unnamed.label == "Set 0x12345678 of Coin");

    const Op revert = Ui::revertCellOp(model, {RowId{1}, RowId{2}}, columnHash("pos_x"));
    CHECK(revert.sets.size() == 1 && revert.sets[0].after.f == 10.0f);  // the added Goomba has no disc value
    const Op rows = Ui::revertRowsOp(model, {RowId{1}});
    CHECK(rows.revertRows.size() == 1 && rows.label == "Reverted Coin" && rows.sets.empty());

    Cell a, b;
    a.type = b.type = CellType::Float;
    a.f = b.f = 2.5f;
    CHECK(Ui::sameCell(a, b) && Ui::cellText(a) == "2.5");
    b.type = CellType::Int;
    CHECK(!Ui::sameCell(a, b));
    // The Browser: one card per class placed, with its count and examples; search by name.
    const std::vector< Ui::BrowserEntry > entries = Ui::collectBrowserEntries(model);
    CHECK(entries.size() == 3);  // Coin, Kuribo, Rabbit (sorted by name; deleted rows do not count)
    CHECK(entries[0].className == "Coin" && entries[0].count == 1 && entries[0].examples.size() == 1 && entries[0].category == "Objects");
    CHECK(Ui::filterEntries(entries, "kur", "").size() == 1 && Ui::filterEntries(entries, "kur", "")[0]->className == "Kuribo");
    CHECK(Ui::filterEntries(entries, "", "").size() == 3 && Ui::filterEntries(entries, "zzz", "").empty());
    CHECK(Ui::filterEntries(entries, "", "Planets").empty());

    // Mission names: shown when drawable, else "Mission <n>" (FakeModel names every mission "Test mission").
    CHECK(Ui::missionLabel(model, 1) == "Test mission");

    // The tool modules' helpers: display names as the panels show them; the help strip is shared.
    CHECK(!Ui::displayName(model, model.rows.front().id).empty());
    Ui::helpText() = "hint";
    CHECK(Ui::helpText() == "hint");

    // Placement tokens: example rows as they are, class placements with the high bit (EP-005).
    CHECK(!Ui::isClassToken(42) && Ui::isClassToken(Ui::kClassTokenBit | 3));
    bool keepLinks = true;
    PetariNative::EditorPick::place(42);
    CHECK(PetariNative::EditorPick::takePlace(&keepLinks) == 42 && !keepLinks);
    PetariNative::EditorPick::place(43, true);
    CHECK(PetariNative::EditorPick::takePlace(&keepLinks) == 43 && keepLinks);
    CHECK(PetariNative::EditorPick::takePlace(&keepLinks) == 0 && !keepLinks);

    // Share: the next version and the zip's name.
    CHECK(Ui::bumpVersion("") == "1.0" && Ui::bumpVersion("1.0") == "1.1" && Ui::bumpVersion("2.9") == "2.10" && Ui::bumpVersion("3") == "4");
    CHECK(Ui::bumpVersion("1.0-beta") == "1.0-beta.1");
    CHECK(Ui::zipFileName("Rooftop Relay", "1.2") == "Rooftop-Relay-1.2.zip" && Ui::zipFileName("", "") == "mod-1.0.zip");

    // The Outliner's Find: case-insensitive contains.
    CHECK(Ui::matchesQuery("HardBlock", "hard") && Ui::matchesQuery("Ground-pound rock", "POUND") && Ui::matchesQuery("x", ""));
    CHECK(!Ui::matchesQuery("Coin", "hard"));

    // Mod names: folder-safe, trimmed, capped; the default from the galaxy.
    CHECK(Ui::modNameFrom("  My/Cool:Mod!  ") == "MyCoolMod" && Ui::modNameFrom("Pipe Dream_2-b") == "Pipe Dream_2-b" && Ui::modNameFrom("///").empty());
    CHECK(Ui::modNameFrom(std::string(60, 'a')).size() == 40);
    CHECK(Ui::defaultModName("CosmosGardenGalaxy") == "CosmosGarden edit" && Ui::defaultModName(nullptr) == "My edit");

    // Checks: counted by severity, errors listed first, each group in Runtime's order.
    {
        std::vector< PetariEditor::Check > list(4);
        list[0].key = "w1", list[0].severity = PetariEditor::Severity::Warning;
        list[1].key = "e1", list[1].severity = PetariEditor::Severity::Error;
        list[2].key = "w2", list[2].severity = PetariEditor::Severity::Warning;
        list[3].key = "e2", list[3].severity = PetariEditor::Severity::Error;
        const Ui::CheckCounts counts = Ui::countChecks(list);
        CHECK(counts.errors == 2 && counts.warnings == 2);
        const std::vector< const PetariEditor::Check* > ordered = Ui::orderChecks(list);
        CHECK(ordered.size() == 4 && ordered[0]->key == "e1" && ordered[1]->key == "e2" && ordered[2]->key == "w1" && ordered[3]->key == "w2");
        CHECK(Ui::countChecks({}).errors == 0 && Ui::orderChecks({}).empty());
    }

    // Friction #8: collision size and anchor in words, at scale_x (the game's one collision scale).
    {
        PetariEditor::Catalog::ClassInfo block;
        CHECK(Ui::collisionSize(block, {1, 1, 1}).empty() && Ui::collisionAnchor(block, 1).empty());
        block.hasCollisionBox = true;
        block.collisionBox = {-74.5f, 0.5f, -74.5f, 74.5f, 149.5f, 74.5f};  // StrongBlock: a 150 cube on its point
        CHECK(Ui::collisionSize(block, {1, 1, 1}) == "Collision 149 x 149 x 149");
        CHECK(Ui::collisionSize(block, {3, 1, 3}) == "Collision ~447 x 447 x 447");  // uneven: collides at X
        CHECK(Ui::collisionAnchor(block, 1) == "stands on its point" && Ui::collisionAnchor(block, 3) == "stands on its point");
        CHECK(Ui::collisionTop(block, 3) == "top 448 above its point");
        PetariEditor::Catalog::ClassInfo ball = block;
        ball.collisionBox = {-50, -50, -50, 50, 50, 50};
        CHECK(Ui::collisionAnchor(ball, 1) == "centred on its point");
        ball.collisionBox = {-50, -80, -50, 50, 20, 50};
        CHECK(Ui::collisionAnchor(ball, 1) == "reaches 80 below its point");
        ball.collisionBox = {-5, 20, -5, 5, 30, 5};
        CHECK(Ui::collisionAnchor(ball, 1) == "floats 20 above its point" && Ui::collisionSize(ball, {1, 1, 1}) == "Collision 10 x 10 x 10");
    }

    // Named settings in words: enum labels (including a named -1), on/off, numbers with units, "default".
    {
        PetariEditor::Catalog::Arg walk;
        walk.type = PetariEditor::Catalog::ArgType::Enum;
        walk.choices = {{-1, "Yes"}, {0, "No"}, {2, "Sleeps"}};
        CHECK(Ui::argValueText(walk, -1) == "Yes" && Ui::argValueText(walk, 2) == "Sleeps" && Ui::argValueText(walk, 7) == "7 (unlisted)");
        walk.choices = {{0, "Walks"}};
        CHECK(Ui::argValueText(walk, -1) == "default");
        PetariEditor::Catalog::Arg flag;
        flag.type = PetariEditor::Catalog::ArgType::Bool;
        CHECK(Ui::argValueText(flag, 1) == "on" && Ui::argValueText(flag, 0) == "off" && Ui::argValueText(flag, -1) == "default");
        PetariEditor::Catalog::Arg wait;
        wait.unit = "frames";
        CHECK(Ui::argValueText(wait, 120) == "120 frames" && Ui::argValueText(wait, -1) == "default");
    }

    // Card names: one line when they fit, else two at a space or CamelCase boundary, the rest ellipsized.
    {
        const auto chars = [](const std::string& text) { return static_cast< float >(text.size()); };  // 1 per character
        CHECK(Ui::cardLabelLines("Goomba", 10.0f, chars) == std::vector< std::string >{"Goomba"});
        CHECK((Ui::cardLabelLines("1-Up Mushroom", 10.0f, chars) == std::vector< std::string >{"1-Up", "Mushroom"}));
        CHECK((Ui::cardLabelLines("BenefitItemInvincible", 12.0f, chars) == std::vector< std::string >{"BenefitItem", "Invincible"}));
        CHECK((Ui::cardLabelLines("AudioEffectCube", 12.0f, chars) == std::vector< std::string >{"AudioEffect", "Cube"}));
        const std::vector< std::string > long_ = Ui::cardLabelLines("ReverseGravityRoomPressGroundC", 12.0f, chars);
        CHECK(long_.size() == 2 && long_[0] == "Reverse" && long_[1].size() <= 12 && long_[1].substr(long_[1].size() - 3) == "...");
        const std::vector< std::string > noBreak = Ui::cardLabelLines("abcdefghijklmnopqrstuvwxyz", 10.0f, chars);
        CHECK(noBreak.size() == 2 && noBreak[0] == "abcdefghij" && noBreak[1] == "klmnopq...");
    }

    // Thumbnails: cropped to the drawn part, squared and padded, inside the image; nothing drawn fails.
    {
        std::vector< unsigned char > rgba(100 * 100 * 4, 0);
        float uv[4];
        CHECK(!Ui::thumbnailCrop(rgba.data(), 100, 100, uv));
        for (int y = 70; y < 90; y++) {
            for (int x = 10; x < 30; x++) rgba[(y * 100 + x) * 4 + 3] = 255;  // a 20x20 square low left
        }
        CHECK(Ui::thumbnailCrop(rgba.data(), 100, 100, uv));
        CHECK(std::fabs((uv[2] - uv[0]) - 0.224f) < 0.01f && std::fabs((uv[3] - uv[1]) - 0.224f) < 0.01f);
        CHECK(std::fabs((uv[0] + uv[2]) * 0.5f - 0.2f) < 0.01f && std::fabs((uv[1] + uv[3]) * 0.5f - 0.8f) < 0.01f);
        for (int x = 0; x < 100; x++) rgba[(50 * 100 + x) * 4 + 3] = 255;  // a full-width line: the crop clamps to the image
        CHECK(Ui::thumbnailCrop(rgba.data(), 100, 100, uv));
        CHECK(uv[0] >= 0.0f && uv[1] >= 0.0f && uv[2] <= 1.0f && uv[3] <= 1.0f);
    }

    Column unnamedColumn;
    unnamedColumn.hash = 0xABCDEF01u;
    CHECK(Ui::columnLabel(unnamedColumn) == "0xabcdef01");
}

void testPathInputCapture() {
    FakeModel model;
    PetariEditor::Core::setModel(&model);
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(800, 600);
    PetariEditor::PathsView::View snapshot;
    snapshot.marks.push_back({RowId{9}, RowId{1}, 0, 0, 0});
    PetariEditor::PathsView::publish(snapshot);
    auto draw = [&](bool down, float x = 400, float y = 300) {
        io.AddMousePosEvent(x, y);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
        ImGui::NewFrame();
        io.WantCaptureMouse = false;
        const bool consumed = PetariEditor::Ui::drawPathsViewport(0, 0, 800, 600);
        ImGui::Render();
        return consumed;
    };
    CHECK(draw(true));
    auto input = PetariEditor::PathsView::take();
    CHECK(input.begin && input.point == RowId{1});
    CHECK(draw(false));
    input = PetariEditor::PathsView::take();
    CHECK(input.end && input.move);
    CHECK(!draw(false));
    PetariNative::EditorPick::takeSelectRows();
    snapshot.marks.clear();
    snapshot.sockets.push_back({RowId{1}, "CommonPath_ID", 0, 0});
    PetariEditor::PathsView::publish(snapshot);
    // A collapsed socket list cannot catch clicks intended for the scene.
    CHECK(!draw(true, 432, 324));
    CHECK(!PetariEditor::PathsView::linkingPath());
    draw(false, 432, 324);
    draw(false, 430, 300);  // Hover the compact handle, then move into its menu.
    CHECK(draw(true, 432, 324));
    CHECK(PetariEditor::PathsView::linkingPath());
    CHECK(draw(false, 432, 324));
    CHECK(!PetariEditor::PathsView::linkingPath());
    draw(false, 600, 450);  // Leaving the menu collapses it again.
    CHECK(!draw(true, 432, 324));
    draw(false, 432, 324);
    PetariEditor::PathsView::publish({});
    PetariEditor::Core::setModel(nullptr);
}

void testMissionInput() {
    namespace Ui = PetariEditor::Ui;
    struct MissionModel : FakeModel {
        bool splitLayers = true, mixedRows = false, matchingRows = false;
        std::uint64_t version = 1;
        MissionModel() {
            cols.push_back({columnHash("l_id"), "l_id", CellType::Int});
            for (auto& row : rows) {
                row.zone = 0;
                row.now[columnHash("l_id")] = {CellType::Int, static_cast<int>(row.id.v), 0, {}};
            }
        }
        std::uint64_t revision() const override { return version; }
        int zoneCount() const override { return 1; }
        int missionCount() const override { return 2; }
        std::uint32_t activeLayers(int, int mission) const override {
            return splitLayers ? (mission == 1 ? 3u : 5u) : 7u;
        }
        std::uint32_t rowMissions(RowId row) const override {
            if (matchingRows) return row == RowId{3} ? 2u : 1u;
            return mixedRows && row == RowId{3} ? 1u : 3u;
        }
        bool locate(RowId row, int* zone, int* table) const override {
            if (!FakeModel::locate(row, zone, table)) return false;
            if (table && matchingRows) *table = row == RowId{3} ? 2 : 1;
            else if (table && mixedRows && row == RowId{3}) *table = 1;
            return true;
        }
        int tableCount(int) const override { return 3; }
        const char* tablePath(int, int table) const override {
            return table == 0 ? "jmp/placement/common/objinfo" : table == 1 ? "jmp/placement/layera/objinfo" : "jmp/placement/layerb/objinfo";
        }
        void forEachRow(int zone, int table, const std::function<void(RowId)>& fn) const override {
            FakeModel::forEachRow(zone, table, [&](RowId row) {
                const int rowTable = matchingRows ? (row == RowId{3} ? 2 : 1) : mixedRows && row == RowId{3} ? 1 : 0;
                if (table == rowTable) fn(row);
            });
        }
    } model;
    Core::setModel(&model);
    Core::drainCommands();
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(800, 600);
    std::vector<RowId> selection{RowId{1}};
    auto draw = [&]() {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(20, 20));
        ImGui::SetNextWindowSize(ImVec2(360, 420));
        ImGui::Begin("Mission test", nullptr, ImGuiWindowFlags_NoSavedSettings);
        Ui::drawMissionsInspector(model, selection);
        ImGui::End();
        ImGui::Render();
    };
    auto click = [&](const char* target) {
        float x = 0, y = 0, w = 0, h = 0;
        CHECK(Ui::testTargetRect(target, &x, &y, &w, &h));
        io.AddMousePosEvent(x + w * .5f, y + h * .5f);
        draw();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        draw();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        draw();
    };
    draw();
    draw();
    click("mission-chip:2");
    CHECK(Core::drainCommands().empty());  // Drafting does not create history.
    click("button:Cancel missions");
    CHECK(Core::drainCommands().empty());
    click("mission-chip:2");
    click("button:Apply missions");
    auto commands = Core::drainCommands();
    CHECK(commands.size() == 1);
    if (commands.size() == 1) {
        CHECK(commands[0].kind == Command::Kind::Apply);
        CHECK(commands[0].op.adds.size() == 1 && commands[0].op.deletes.size() == 1);
        if (commands[0].op.adds.size() == 1)
            CHECK(commands[0].op.adds[0].table == "jmp/placement/layera/objinfo");
    }
    ++model.version;
    draw();
    click("button:Only this mission");
    CHECK(Core::drainCommands().size() == 1);  // The repair commits immediately.
    model.splitLayers = false;
    ++model.version;
    draw();
    click("button:Only this mission");
    CHECK(Core::drainCommands().empty());  // An impossible mask is explained without changing rows.
    CHECK(Ui::helpText().find("TestGalaxy") != std::string::npos);
    model.splitLayers = model.mixedRows = true;
    ++model.version;
    selection = {RowId{1}, RowId{3}};
    draw();
    click("button:Cancel missions");
    CHECK(Core::drainCommands().empty());
    click("button:Apply missions");
    CHECK(Core::drainCommands().size() == 1);  // A mixed union can be applied without toggling a chip.
    click("mission-chip:1");
    click("mission-chip:2");
    click("button:Apply missions");
    CHECK(Core::drainCommands().empty());
    CHECK(Ui::helpText().find("at least one mission") != std::string::npos);
    model.matchingRows = true;
    model.rows[2].now = model.rows[0].now;  // Same object split between layer A and layer B.
    ++model.version;
    selection = {RowId{1}};
    draw();
    click("mission-chip:2");  // Logical membership includes the unselected sibling; turn mission 2 off.
    click("button:Apply missions");
    commands = Core::drainCommands();
    CHECK(commands.size() == 1);
    if (commands.size() == 1) {
        CHECK(commands[0].op.adds.empty());
        CHECK(commands[0].op.deletes == std::vector<RowId>{RowId{3}});
    }
    PetariNative::EditorPick::takeSelectRows();
    Core::setModel(nullptr);
}

void testChoiceLabels() {
    namespace Ui = PetariEditor::Ui;
    CHECK(Ui::galaxyChoiceLabel("EggStarGalaxy") == "Good Egg Galaxy");
    CHECK(Ui::galaxyChoiceLabel("HeavenlyBeachGalaxy") == "Beach Bowl Galaxy");
    CHECK(Ui::galaxyChoiceLabel("CosmosGardenGalaxy") == "Gusty Garden Galaxy");
    CHECK(Ui::galaxyChoiceLabel("CustomGalaxy") == "CustomGalaxy");
    CHECK(Ui::galaxyChoiceLabel("good-egg") == "good-egg");
    CHECK(Ui::galaxyChoiceLabel("").empty());
    CHECK(Ui::skyChoiceLabel("AuroraSky") == "Aurora Sky");
    CHECK(Ui::skyChoiceLabel("MilkyWaySky") == "Milky Way Sky");
    CHECK(Ui::skyChoiceLabel("CustomSky") == "CustomSky");
    CHECK(Ui::skyChoiceLabel("VRDarkSpace") == "VRDarkSpace");
    CHECK(Ui::skyChoiceLabel("").empty());
    PetariEditor::Catalog::ClassInfo sky;
    sky.name = "AuroraSky";
    sky.displayName = "AuroraSky";
    PetariEditor::Catalog::addClass(sky);
    CHECK(Ui::skyChoiceLabel(sky.name) == "Aurora Sky");
    sky.displayName = "Custom Aurora";
    PetariEditor::Catalog::addClass(sky);
    CHECK(Ui::skyChoiceLabel(sky.name) == "Custom Aurora");
}

void testGalaxyInfoInput() {
    namespace Ui = PetariEditor::Ui;
    static std::optional<GalaxyInfo> info;
    static NewGalaxyChoices choices;
    choices.galaxies = {"EggStarGalaxy", "HeavenlyBeachGalaxy"};
    info = GalaxyInfo{};
    info->internalName = "TestModGalaxy";
    info->mod = "TestMod";
    info->name = "Test Galaxy";
    info->missions = {"First star"};
    info->musicFrom = "EggStarGalaxy";
    Core::NewGalaxyServices services;
    services.choices = []() -> const NewGalaxyChoices& { return choices; };
    services.galaxyInfo = []() { return info; };
    services.checkInfo = [](const GalaxyInfo& g) { return g.name.empty() ? std::string("Name is empty") : std::string{}; };
    services.infoNeedsRestart = [](const GalaxyInfo& g) { return info && g.missions.size() != info->missions.size(); };
    Core::setNewGalaxyServices(services);
    Core::drainCommands();
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1000, 900);
    auto draw = [&]() {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(20, 20));
        ImGui::SetNextWindowSize(ImVec2(400, 820));
        Ui::drawInspectorWindow("Galaxy settings test");
        ImGui::Render();
    };
    auto click = [&](const char* target) {
        draw(); draw();  // A changed mission count moves the buttons on the next frame.
        float x = 0, y = 0, w = 0, h = 0;
        CHECK(Ui::testTargetRect(target, &x, &y, &w, &h));
        io.AddMousePosEvent(x + w / 2, y + h / 2);
        draw();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true); draw();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false); draw();
    };
    draw(); draw();
    click("button:Apply galaxy settings");
    CHECK(Core::drainCommands().empty());
    click("button:Add mission");
    CHECK(Core::drainCommands().empty());
    click("button:Cancel galaxy settings");
    click("button:Apply galaxy settings");
    CHECK(Core::drainCommands().empty());
    click("button:Add mission");
    click("combo:Galaxy music");
    click("galaxy-music:HeavenlyBeachGalaxy");
    Core::mutableStatus().unsaved = true;
    draw();
    click("button:Apply galaxy settings");
    CHECK(Core::drainCommands().empty());  // Restart cannot silently discard layout edits.
    click("button:Discard and apply galaxy settings");
    auto commands = Core::drainCommands();
    CHECK(commands.size() == 1 && commands[0].kind == Command::Kind::SetGalaxyInfo);
    if (commands.size() == 1) {
        const auto& g = commands[0].galaxyInfo;
        CHECK(g.discardUnsaved && g.missions.size() == 2 && g.missions[0] == "First star");
        CHECK(g.musicFrom == "HeavenlyBeachGalaxy");
        CHECK(g.name == "Test Galaxy" && g.internalName == "TestModGalaxy" && g.mod == "TestMod");
    }
    click("button:Discard and apply galaxy settings");
    CHECK(Core::drainCommands().empty());
    Core::reportError("Galaxy settings: fixture error"); draw();
    Core::mutableStatus().unsaved = false;
    click("button:Apply galaxy settings");
    commands = Core::drainCommands();
    CHECK(commands.size() == 1 && !commands[0].galaxyInfo.discardUnsaved);
    const auto original = info;
    info.reset(); draw();  // Disc galaxies have no panel or stale draft.
    info = original; draw(); draw();
    click("field:Galaxy name");
    const ImGuiKey commandKey = io.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
    io.AddKeyEvent(commandKey, true);
    io.AddKeyEvent(ImGuiKey_A, true); draw();
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(commandKey, false);
    io.AddInputCharactersUTF8("  Renamed Galaxy  "); draw();
    io.AddKeyEvent(ImGuiKey_Enter, true); draw();
    io.AddKeyEvent(ImGuiKey_Enter, false); draw();
    Core::mutableStatus().unsaved = true;
    click("button:Apply galaxy settings");
    commands = Core::drainCommands();
    CHECK(commands.size() == 1 && commands[0].galaxyInfo.name == "  Renamed Galaxy  " && !commands[0].galaxyInfo.discardUnsaved);
    info->name = "Renamed Galaxy";  // Backend normalization must not leave the panel disabled.
    Core::reportNotice("Saved Renamed Galaxy settings");
    draw(); draw();
    Core::mutableStatus().unsaved = false;
    click("button:Add mission");
    click("button:Apply galaxy settings");
    commands = Core::drainCommands();
    CHECK(commands.size() == 1 && commands[0].galaxyInfo.name == "Renamed Galaxy" && commands[0].galaxyInfo.missions.size() == 2);
    info.reset(); draw();
    Core::setNewGalaxyServices({});
    Core::mutableStatus() = Status{};
}

void testJournalInput() {
    namespace Ui = PetariEditor::Ui;
    CHECK(Ui::journalQuestion(1, 100, 120) == "Restore 1 unsaved edit from a moment ago?");
    CHECK(Ui::journalQuestion(3, 100, 220) == "Restore 3 unsaved edits from 2 minutes ago?");
    CHECK(Ui::journalQuestion(1, 100, 3700) == "Restore 1 unsaved edit from 1 hour ago?");
    CHECK(Ui::journalQuestion(2, 100, 173000) == "Restore 2 unsaved edits from 2 days ago?");
    CHECK(Ui::journalQuestion(1, 0, 120).find("earlier session") != std::string::npos);
    CHECK(Ui::journalQuestion(1, 200, 120).find("a moment ago") != std::string::npos);
    FakeModel model;
    Core::setModel(&model);
    Core::setMode(Mode::Off);
    auto& st = Core::mutableStatus();
    st.journalEdits = 3;
    st.journalTime = 100;
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(800, 600);
    auto draw = [&]() {
        ImGui::NewFrame();
        Ui::drawJournalSheet();
        ImGui::Render();
    };
    auto click = [&](const char* target) {
        float x = 0, y = 0, w = 0, h = 0;
        CHECK(Ui::testTargetRect(target, &x, &y, &w, &h));
        io.AddMousePosEvent(x + w / 2, y + h / 2);
        draw();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        draw();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        draw();
    };
    draw();
    CHECK(ImGui::GetTopMostPopupModal() == nullptr);  // No offer during normal play.
    Core::setMode(Mode::Editing);
    draw(); draw();
    CHECK(ImGui::GetTopMostPopupModal() != nullptr);
    io.AddKeyEvent(ImGuiKey_Escape, true); draw();
    io.AddKeyEvent(ImGuiKey_Escape, false); draw();
    CHECK(ImGui::GetTopMostPopupModal() != nullptr);  // Recovery must be answered.
    CHECK(Core::drainCommands().empty());
    click("button:Restore journal");
    auto commands = Core::drainCommands();
    CHECK(commands.size() == 1 && commands[0].kind == Command::Kind::RestoreJournal);
    click("button:Restore journal");
    CHECK(Core::drainCommands().empty());  // Wait for the command; no double submission.
    Core::reportError("Restore failed for test");
    draw(); draw();
    click("button:Discard journal");
    commands = Core::drainCommands();
    CHECK(commands.size() == 1 && commands[0].kind == Command::Kind::DiscardJournal);
    st.journalEdits = 0;
    draw(); draw();
    CHECK(ImGui::GetTopMostPopupModal() == nullptr);
    st = Status{};
    Core::setModel(nullptr);
    Core::setMode(Mode::Off);
}

void testHelpInput() {
    namespace Ui = PetariEditor::Ui;
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(800, 600);
    auto key = [&](ImGuiKey code, bool down, bool typing = false, bool command = false) {
        io.AddKeyEvent(ImGuiMod_Shift, code == ImGuiKey_Slash);
        io.AddKeyEvent(ImGuiMod_Ctrl, command);
        io.AddKeyEvent(code, down);
        ImGui::NewFrame();
        io.WantTextInput = typing;
        const bool consumed = Ui::updateHelpInput();
        if (consumed) CHECK(!ImGui::IsKeyPressed(ImGuiKey_Escape, false));
        Ui::drawHelpOverlay();
        CHECK(Ui::viewportDrawList() == (Ui::helpVisible() ? ImGui::GetBackgroundDrawList() : ImGui::GetForegroundDrawList()));
        ImGui::Render();
        return consumed;
    };
    CHECK(!Ui::helpVisible());
    key(ImGuiKey_Slash, true, true);  // Typing a question mark does not open help.
    CHECK(!Ui::helpVisible());
    key(ImGuiKey_Slash, false);
    key(ImGuiKey_Slash, true, false, true);
    CHECK(!Ui::helpVisible());
    key(ImGuiKey_Slash, false);
    key(ImGuiKey_Slash, true);
    CHECK(Ui::helpVisible());
    const auto* window = ImGui::FindWindowByName("Keys and tips###editor-help");
    CHECK(window && !(window->Flags & ImGuiWindowFlags_Modal));
    CHECK(window && window->Pos.x >= 0 && window->Pos.y >= 0);
    CHECK(window && window->Size.x <= 800 && window->Size.y <= 600);
    key(ImGuiKey_Slash, false);
    CHECK(key(ImGuiKey_Escape, true));  // Closing help consumes Esc before selection/tools see it.
    CHECK(!Ui::helpVisible());
    key(ImGuiKey_Escape, false);
    key(ImGuiKey_Slash, true);
    CHECK(Ui::helpVisible());
    key(ImGuiKey_Slash, false);
    key(ImGuiKey_Slash, true);
    CHECK(!Ui::helpVisible());
    key(ImGuiKey_Slash, false);
    io.AddKeyEvent(ImGuiMod_Shift, false);
    io.AddInputCharacter('?');  // Non-US layouts can produce the character without Shift+/.
    ImGui::NewFrame();
    Ui::updateHelpInput();
    CHECK(Ui::helpVisible());
    ImGui::Render();
    Ui::toggleHelp();
    Ui::setVisible(true);
    Ui::setFocusMode(true);
    Ui::toggleHelp();
    frame(800, 600);
    CHECK(ImGui::FindWindowByName("Keys and tips###editor-help")->Active);
    CHECK(Ui::helpVisible());
    Ui::toggleHelp();
    Ui::setFocusMode(false);
}

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // the default layout every run
    unsigned char* pixels = nullptr;
    int texWidth = 0, texHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &texWidth, &texHeight);

    namespace Ui = PetariEditor::Ui;
    float x, y, w, h;

    // Hidden: nothing drawn, no viewport, the presenter untouched.
    frame(1440, 900);
    CHECK(!Ui::viewportRect(&x, &y, &w, &h));
    CHECK(gArea.calls == 0);

    // Shown at the MacBook Air default size (UX section 4): the game between a 260-point
    // Outliner and a 300-point Inspector, under the 44-point toolbar, above the 28-point footer.
    Ui::setVisible(true);
    frame(1440, 900);
    frame(1440, 900);  // docking settles on the second frame
    CHECK(Ui::viewportRect(&x, &y, &w, &h));
    std::printf("viewport at 1440x900: %.0f,%.0f %.0fx%.0f\n", x, y, w, h);
    CHECK(near(y, 44.0f));
    CHECK(near(y + h, 900.0f - 28.0f));
    CHECK(near(x, 260.0f, 4.0f));
    CHECK(near(x + w, 1440.0f - 300.0f, 4.0f));
    // The presenter got the same rectangle, and renders the game at its shape.
    CHECK(near(gArea.x, x, 0.01f) && near(gArea.y, y, 0.01f) && near(gArea.w, w, 0.01f) && near(gArea.h, h, 0.01f));
    CHECK(gArea.freeAspect == 1);

    // A bigger window: the docks keep their widths and the game takes the rest.
    frame(2560, 1440);
    frame(2560, 1440);
    CHECK(Ui::viewportRect(&x, &y, &w, &h));
    std::printf("viewport at 2560x1440: %.0f,%.0f %.0fx%.0f\n", x, y, w, h);
    CHECK(near(y, 44.0f) && near(y + h, 1440.0f - 28.0f));
    CHECK(w > 1500.0f && x > 200.0f && x + w < 2560.0f - 200.0f);

    // Focus mode: no panels, the game takes the whole window again.
    Ui::setFocusMode(true);
    frame(2560, 1440);
    CHECK(!Ui::viewportRect(&x, &y, &w, &h));
    CHECK(gArea.w == 0.0f && gArea.h == 0.0f);
    CHECK(gArea.freeAspect == 0);
    Ui::setFocusMode(false);
    frame(2560, 1440);
    CHECK(Ui::viewportRect(&x, &y, &w, &h));

    // Hidden again (back to playing): the whole window, and the game's own aspect.
    Ui::setVisible(false);
    frame(2560, 1440);
    CHECK(!Ui::viewportRect(&x, &y, &w, &h));
    CHECK(gArea.w == 0.0f && gArea.h == 0.0f && gArea.freeAspect == 0);
    const int calls = gArea.calls;
    frame(2560, 1440);
    CHECK(gArea.calls == calls);  // nothing more once the area is cleared

    // A window too small for the panels: the editor steps aside rather than squeezing the game.
    Ui::setVisible(true);
    frame(300, 100);
    CHECK(!Ui::viewportRect(&x, &y, &w, &h));

    testPathInputCapture();
    testMissionInput();
    testHelpInput();
    testGalaxyInfoInput();
    testJournalInput();
    ImGui::DestroyContext();
    testPanelsLogic();
    testChoiceLabels();
    if (gFailures != 0) {
        std::fprintf(stderr, "editor_ui_tests: %d failure(s)\n", gFailures);
        return 1;
    }
    std::printf("editor_ui_tests: OK\n");
    return 0;
}
