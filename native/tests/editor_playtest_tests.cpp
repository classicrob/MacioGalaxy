#include "edit_model.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <petari/editor/catalog.hpp>
#include <petari/editor/gizmo_math.hpp>
#include <petari/editor/playtest.hpp>
using namespace PetariEditor;
namespace P = Playtest;
static int checkCount = 0;
void require(bool condition, const char* label) {
    ++checkCount;
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        std::exit(1);
    }
}
bool close(float a, float b) {
    return std::fabs(a - b) < 0.001f;
}
void trailTests() {
    P::Trail t;
    t.record({}, {}, {0, -1, 0}, P::Motion::Ground);
    require(t.samples().empty(), "normal play does not record");
    t.begin();
    t.record({0, 0, 0}, {1, 0, 0}, {-1, 0, 0}, P::Motion::Air);
    t.record({1, 0, 0}, {0, 0, 0}, {-1, 0, 0}, P::Motion::Air);
    auto samples = t.samples();
    require(!samples[0].peak && samples[1].peak, "peak uses the highest sampled point along gravity X, not world Y");
    require(!samples[0].connected && samples[1].connected, "first sample disconnected");
    t.pause();
    t.record({}, {}, {}, P::Motion::Ground);
    require(t.frames() == 2, "Edit pauses timer");
    t.resume();
    t.record({2, 0, 0}, {-1, 0, 0}, {-1, 0, 0}, P::Motion::Dive);
    require(t.frames() == 3 && !t.samples().back().connected, "resume keeps timer and breaks edit-time gap");
    t.record({3000, 0, 0}, {1, 0, 0}, {-1, 0, 0}, P::Motion::CapBounce);
    require(!t.samples().back().connected, "teleport cannot draw long segment");
    t.record({std::numeric_limits< float >::quiet_NaN(), 0, 0}, {}, {}, P::Motion::Ground);
    require(t.samples().size() == 4, "nonfinite sample ignored");
    for (int i = 0; i < 4000; ++i)
        t.record({float(i), 0, 0}, {1, 0, 0}, {0, -1, 0}, P::Motion::Ground);
    samples = t.samples();
    require(samples.size() == 3600 && close(samples.front().position.x, 400), "ring keeps last sixty seconds in order");
    require(!samples.front().connected && samples.back().frame == 4005, "ring boundary does not wrap line");
    require(t.frames() == 4005, "timer survives ring wrap");
    auto mesh = P::geometry(samples);
    require(mesh.lines.size() > 1000 && mesh.lines.size() < 2000 && mesh.triangles.empty(), "trail dotted, bounded, unfilled");
    require(P::elapsed(42 * 60) == "0:42" && P::elapsed(61 * 60) == "1:01", "clear duration format");
    require(P::motionColor(P::Motion::Ground) != P::motionColor(P::Motion::Air) &&
                P::motionColor(P::Motion::Dive) != P::motionColor(P::Motion::CapBounce),
            "distinct movement colours");
    std::vector< P::Sample > states;
    for (int i = 0; i < 5; ++i)
        states.push_back({{float(i * 10), 0, 0}, {0, 1, 0}, static_cast< P::Motion >(i % 4), i == 2, i > 0, 1});
    mesh = P::geometry(states);
    require(mesh.lines.size() == 8, "four coloured segments plus four peak edges");
    require(mesh.lines[0].color == P::motionColor(P::Motion::Air) && mesh.lines[1].color == P::motionColor(P::Motion::Dive),
            "air and dive segment colours");
    require(mesh.lines[6].color == P::motionColor(P::Motion::CapBounce) && mesh.lines[7].color == P::motionColor(P::Motion::Ground),
            "cap bounce and ground segment colours");
    t.begin();
    require(t.frames() == 0 && t.samples().empty(), "new run resets timer and trail");
    t.record({}, {0, 1, 0}, {0, -1, 0}, P::Motion::Air);
    t.record({0, 1, 0}, {0, -1, 0}, {0, -1, 0}, P::Motion::Ground);
    require(!t.samples()[0].peak, "landing is not a jump peak");
    t.miss({5, 6, 7}, {0, -2, 0}, {0, -1, 0}, P::Motion::Air);
    samples = t.samples();
    require(t.active() && !t.running() && samples.size() == 3, "miss preserves run and freezes at the terminal sample");
    require(samples.back().missed && close(samples.back().position.x, 5) && close(samples.back().position.y, 6), "miss stores the exact death point");
    const auto missedFrames = t.frames();
    t.miss({100, 0, 0}, {}, {0, -1, 0}, P::Motion::Air);
    t.record({}, {}, {}, P::Motion::Ground);
    require(t.frames() == missedFrames && t.samples().size() == 3, "duplicate miss and frozen updates do not advance trail");
    mesh = P::geometry({samples.back()});
    require(mesh.lines.size() == 3 && mesh.lines.front().color == 0xff5050ff, "red endpoint remains visible without a connecting segment");
    t.resume();
    t.record({8, 6, 7}, {}, {0, -1, 0}, P::Motion::Ground);
    require(!t.samples().back().missed && !t.samples().back().connected, "resume does not carry the terminal flag across the edit gap");
    t.begin();
    require(t.samples().empty(), "fresh Play removes the previous miss marker");
    t.miss({1, 2, 3}, {}, {0, -1, 0}, P::Motion::Air);
    require(t.samples().size() == 1 && t.samples().front().missed, "miss before the first update still records a death point");
    t.end();
    t.resume();
    require(!t.active() && !t.running() && t.samples().empty(), "end cannot be resumed");
}
void keepTests() {
    namespace F = Formats;
    auto integer = [](int n) { return F::CellValue{F::CellValue::Kind::Int, n, 0, {}}; };
    auto real = [](float n) { return F::CellValue{F::CellValue::Kind::Float, 0, n, {}}; };
    auto str = [](const char* s) { return F::CellValue{F::CellValue::Kind::String, 0, 0, s}; };
    auto table = [](std::vector< std::pair< const char*, F::CellValue > > values) {
        F::Bytes bytes(16, 0);
        bytes[11] = 16;
        auto t = *F::Table::parse(bytes);
        for (const auto& v : values)
            t.addColumn(F::jmapHash(v.first),
                        v.second.kind == F::CellValue::Kind::String ? F::ColumnType::StringOffset :
                        v.second.kind == F::CellValue::Kind::Float  ? F::ColumnType::Float :
                                                                      F::ColumnType::Long,
                        v.second);
        int row = t.addRow();
        for (std::size_t i = 0; i < values.size(); ++i)
            t.set(row, int(i), values[i].second);
        return t.build();
    };
    auto cells = std::vector< std::pair< const char*, F::CellValue > >{{"name", str("KeepTest")}, {"CommonPath_ID", integer(-1)},
                                                                       {"pos_x", real(0)},        {"pos_y", real(0)},
                                                                       {"pos_z", real(0)},        {"dir_x", real(0)},
                                                                       {"dir_y", real(0)},        {"dir_z", real(0)},
                                                                       {"scale_x", real(1)},      {"scale_y", real(1)},
                                                                       {"scale_z", real(1)}};
    std::map< std::string, F::Bytes > files;
    files["/StageData/TestGalaxy/TestGalaxyScenario.arc"] =
        F::Archive::create("stage", {{"ScenarioData.bcsv", table({{"ScenarioNo", integer(1)}, {"TestGalaxy", integer(0)}})},
                                     {"ZoneList.bcsv", table({{"ZoneName", str("TestGalaxy")}})}})
            .build();
    files["/StageData/TestGalaxy.arc"] =
        F::Archive::create("stage", {{"jmp/placement/common/objinfo", table(cells)}, {"jmp/placement/common/childobjinfo", table(cells)}}).build();
    auto model = EditModel::load(
        "TestGalaxy", 1,
        [&](const std::string& path) -> std::optional< F::Bytes > {
            auto it = files.find(path);
            return it == files.end() ? std::nullopt : std::optional< F::Bytes >(it->second);
        },
        nullptr);
    require(bool(model), "fixture model loaded");
    RowId row, child;
    for (int t = 0; t < model->tableCount(0); ++t)
        model->forEachRow(0, t, [&](RowId r) {
            if (std::string(model->tablePath(0, t)).find("childobjinfo") != std::string::npos)
                child = r;
            else
                row = r;
        });
    require(!P::keepReason(*model, row).empty(), "unknown class denied");
    Catalog::ClassInfo info;
    info.name = "KeepTest";
    info.keepable = true;
    Catalog::addClass(info);
    require(P::keepReason(*model, row).empty(), "catalog permits verified class");
    require(!P::keepReason(*model, child).empty(), "child denied even when class keepable");
    std::string error;
    auto op = P::keepOp(*model, {{row, {{4, 5, 6}, {10, 20, 30}, {1, 2, 3}}}}, &error);
    require(error.empty() && !op.sets.empty(), "Keep builds op");
    require(model->apply(op, nullptr) && close(model->transform(row).pos.x, 4), "Keep persists live position");
    require(model->historySize() == 1 && model->undo() && close(model->transform(row).pos.x, 0), "Keep one undo restores layout");
    require(model->redo() && close(model->transform(row).scale.y, 2), "Keep redo includes scale");
    op = P::keepOp(*model, {{row, {}}, {child, {}}}, &error);
    require(!error.empty() && op.sets.empty(), "mixed selection failure keeps nothing");
    Op rail;
    Cell railId;
    railId.i = 0;
    rail.sets.push_back({row, columnHash("CommonPath_ID"), railId});
    model->apply(rail, nullptr);
    require(!P::keepReason(*model, row).empty(), "rail ownership denied");
    model->undo();
    auto invalid = RowTransform{};
    invalid.pos.x = std::numeric_limits< float >::infinity();
    require(P::keepOp(*model, {{row, invalid}}, &error).sets.empty() && !error.empty(), "nonfinite transform denied");
    const Mtx34 zone{{{0, -1, 0, 100}, {1, 0, 0, 200}, {0, 0, 1, 300}}};
    auto local = P::inZone(zone, {{98, 201, 303}, {0, 0, 90}, {2, 3, 4}});
    require(close(local.pos.x, 1) && close(local.pos.y, 2) && close(local.pos.z, 3), "child zone inverse position");
    require(close(local.dirDeg.x, 0) && close(local.dirDeg.y, 0) && close(local.dirDeg.z, 0), "child zone inverse rotation");
    require(close(local.scale.y, 3), "zone transform preserves scale");
}
int main() {
    trailTests();
    keepTests();
    std::printf("playtest: %d checks PASS\n", checkCount);
}
