// Tests for the level editor's disc formats (petari/editor/formats/formats.hpp): Yaz0 round
// trips, BCSV reads, writes, added columns and rows on a synthetic table, broken input, and,
// with an extracted disc in build/game-data, every stage archive and every table in it
// rebuilding byte for byte when unedited (the property native_stage_edit checks in Python).

#include "petari/editor/formats/formats.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace F = PetariEditor::Formats;
namespace fs = std::filesystem;

namespace {
int checks = 0;
void check(bool condition, const std::string& label) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", label.c_str());
        std::exit(1);
    }
}

F::Bytes readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return F::Bytes(std::istreambuf_iterator< char >(in), {});
}

void put32(F::Bytes& b, std::uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast< std::uint8_t >(v >> s));
}
void put16(F::Bytes& b, std::uint16_t v) {
    b.push_back(static_cast< std::uint8_t >(v >> 8));
    b.push_back(static_cast< std::uint8_t >(v));
}

// name (string pool), l_id (long), pos_x (float), ShapeModelNo (short), packed byte flags in a
// masked long; two rows: Coin / Kuribo.
F::Bytes sampleTable() {
    F::Bytes b;
    put32(b, 2);           // rows
    put32(b, 5);           // fields
    put32(b, 16 + 5 * 12);  // data offset
    put32(b, 16);          // entry size
    auto field = [&](const char* name, std::uint32_t mask, std::uint16_t offset, std::uint8_t shift, std::uint8_t type) {
        put32(b, F::jmapHash(name));
        put32(b, mask);
        put16(b, offset);
        b.push_back(shift);
        b.push_back(type);
    };
    field("name", 0xFFFFFFFF, 0, 0, 6);
    field("l_id", 0xFFFFFFFF, 4, 0, 0);
    field("pos_x", 0xFFFFFFFF, 8, 0, 2);
    field("ShapeModelNo", 0xFFFF, 12, 0, 4);
    field("Flags", 0x0000FF00, 12, 8, 0);  // shares the word with nothing (offset 12 low half is the short)
    // row 0: "Coin", l_id 3, 1.5, -1
    put32(b, 0);
    put32(b, 3);
    put32(b, 0x3FC00000);
    put16(b, 0xFFFF);
    put16(b, 0);
    // row 1: "Kuribo", l_id 7, -2.0, 2
    put32(b, 5);
    put32(b, 7);
    put32(b, 0xC0000000);
    put16(b, 2);
    put16(b, 0);
    for (char c : std::string("Coin\0Kuribo\0", 12)) b.push_back(static_cast< std::uint8_t >(c));
    while (b.size() % 32) b.push_back('@');
    return b;
}

void testYaz0() {
    const std::vector< F::Bytes > inputs = {F::Bytes{}, F::Bytes{'a'}, F::Bytes(5000, 0), [] {
        F::Bytes b;
        for (int i = 0; i < 2304; i++) b.push_back(static_cast< std::uint8_t >(i * 7 + (i >> 5)));
        return b;
    }()};
    for (const F::Bytes& in : inputs) {
        F::Bytes packed = F::yaz0Encode(in);
        check(F::isYaz0(packed), "yaz0 header");
        std::optional< F::Bytes > out = F::yaz0Decode(packed);
        check(out && *out == in, "yaz0 round trip " + std::to_string(in.size()));
    }
    check(F::yaz0Encode(F::Bytes(5000, 0)).size() < 200, "yaz0 compresses runs");
    F::Bytes broken = F::yaz0Encode(F::Bytes(300, 'x'));
    broken.resize(18);
    check(!F::yaz0Decode(broken), "truncated yaz0 refused");
}

void testTable() {
    const F::Bytes raw = sampleTable();
    std::optional< F::Table > table = F::Table::parse(raw);
    check(table.has_value(), "table parses");
    check(table->build() == raw, "unedited table rebuilds identically");
    const int name = table->column(F::jmapHash("name")), lid = table->column(F::jmapHash("l_id"));
    const int pos = table->column(F::jmapHash("pos_x")), shape = table->column(F::jmapHash("ShapeModelNo"));
    check(table->get(1, name).s == "Kuribo" && table->get(0, lid).i == 3, "string and long");
    check(table->get(1, pos).f == -2.0f && table->get(0, shape).i == -1 && table->get(1, shape).i == 2, "float and short");

    table->set(0, pos, F::CellValue{F::CellValue::Kind::Float, 0, 9.25f, {}});
    table->set(1, name, F::CellValue{F::CellValue::Kind::String, 0, 0, "Coin"});  // reuses the pool string
    table->set(0, shape, F::CellValue{F::CellValue::Kind::Int, -3, 0, {}});
    std::optional< F::Table > edited = F::Table::parse(table->build());
    check(edited && edited->get(0, pos).f == 9.25f && edited->get(1, name).s == "Coin" && edited->get(0, shape).i == -3,
          "edits survive a rebuild");
    check(edited->build().size() == raw.size(), "a reused string does not grow the pool");

    const int added = table->addColumn(F::jmapHash("CommonPath_ID"), F::ColumnType::Short, F::CellValue{F::CellValue::Kind::Int, -1, 0, {}});
    const int row = table->addRow(1);
    table->set(row, lid, F::CellValue{F::CellValue::Kind::Int, 99, 0, {}});
    std::optional< F::Table > grown = F::Table::parse(table->build());
    check(grown && grown->rowCount() == 3 && grown->columns().size() == 6, "added row and column");
    check(grown->get(2, grown->column(F::jmapHash("l_id"))).i == 99 && grown->get(2, name).s == "Coin", "added row copies");
    check(grown->get(0, grown->column(F::jmapHash("CommonPath_ID"))).i == -1 && added == 5, "added column initial value");
    check(grown->get(0, pos).f == 9.25f, "columns keep their values after a relayout");
    grown->removeRow(0);
    check(grown->rowCount() == 2 && grown->get(0, name).s == "Coin", "removed row");

    F::Bytes truncated(raw.begin(), raw.begin() + 40);
    check(!F::Table::parse(truncated), "truncated table refused");
    check(!F::Archive::parse(F::Bytes(64, 0)), "non-RARC refused");
}

void testDisc() {
    const fs::path stageData = fs::path(PETARI_TEST_DISC_FILES) / "StageData";
    if (!fs::is_directory(stageData)) {
        std::printf("editor formats: no extracted disc; disc checks skipped\n");
        return;
    }
    int archives = 0, tables = 0, cameras = 0;
    for (const auto& entry : fs::directory_iterator(stageData)) {
        if (entry.path().extension() != ".arc") continue;
        const F::Bytes raw = readFile(entry.path());
        std::optional< F::Archive > archive = F::Archive::parse(raw);
        check(archive.has_value(), "parses " + entry.path().filename().string());
        const F::Bytes plain = F::isYaz0(raw) ? *F::yaz0Decode(raw) : raw;
        check(archive->build() == plain, "rebuilds identically " + entry.path().filename().string());
        for (const std::string& path : archive->files()) {
            const bool camera = fs::path(path).extension() == ".bcam";
            if (path.rfind("jmp/", 0) != 0 && !camera) continue;
            if (camera) ++cameras;
            const F::Bytes* bytes = archive->file(path);
            std::optional< F::Table > table = F::Table::parse(*bytes);
            check(table.has_value(), "table parses " + entry.path().filename().string() + ":" + path);
            check(table->build() == *bytes, "table rebuilds identically " + entry.path().filename().string() + ":" + path);
            ++tables;
        }
        ++archives;
    }
    check(archives > 200, "every disc stage archive seen");
    check(cameras > 100, "disc camera tables included in byte-for-byte rebuild");
    std::printf("editor formats: %d disc archives, %d tables (%d bcam) rebuilt identically\n", archives, tables, cameras);
}
}  // namespace

int main() {
    testYaz0();
    testTable();
    testDisc();
    std::printf("editor formats: %d checks passed\n", checks);
    return 0;
}
