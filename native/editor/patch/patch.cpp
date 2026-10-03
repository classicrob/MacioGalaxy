// Patch mods: apply edits/<Galaxy>.json to the disc's archives (petari/editor/patch.hpp).
#include "petari/editor/patch.hpp"

#include "petari/editor/json.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

namespace PetariEditor::Patch {
namespace {
    using Formats::Bytes;
    using Formats::CellValue;
    using Formats::ColumnType;
    namespace fs = std::filesystem;

    constexpr std::uint64_t kFnvBasis = 0xcbf29ce484222325ull;

    std::uint64_t fnv(std::uint64_t h, const void* pData, size_t size) {
        const auto* p = static_cast< const std::uint8_t* >(pData);
        for (size_t i = 0; i < size; i++) {
            h = (h ^ p[i]) * 0x100000001b3ull;
        }
        return h;
    }

    std::string hex64(std::uint64_t v) {
        char buffer[17];
        std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast< unsigned long long >(v));
        return buffer;
    }

    std::string lower(std::string s) {
        for (char& c : s) c = static_cast< char >(std::tolower(static_cast< unsigned char >(c)));
        return s;
    }

    bool isString(ColumnType type) {
        return type == ColumnType::StringOffset || type == ColumnType::InlineString;
    }

    // A JSON cell for a column of `type`. False when the JSON value's kind doesn't fit.
    bool toCell(const Json::Value& v, ColumnType type, CellValue* pOut) {
        if (isString(type)) {
            if (v.type() != Json::Value::Type::String) return false;
            pOut->kind = CellValue::Kind::String;
            pOut->s = Formats::utf8ToSjis(v.asString());
        } else if (type == ColumnType::Float) {
            if (v.type() != Json::Value::Type::Number) return false;
            pOut->kind = CellValue::Kind::Float;
            pOut->f = static_cast< float >(v.asNumber());
        } else {
            if (v.type() != Json::Value::Type::Number) return false;
            pOut->kind = CellValue::Kind::Int;
            pOut->i = static_cast< std::int32_t >(static_cast< std::int64_t >(v.asNumber()));
        }
        return true;
    }

    // One table being built: the rows carry the disc index they came from (-1: added).
    struct Work {
        Formats::Table table;
        std::vector< int > discIndex;
        const Formats::Table* pDisc = nullptr;  // null for a table the patch created
        bool touched = false;
    };

    struct ArchiveWork {
        Formats::Archive archive;
        std::map< std::string, Work > tables;  // by lower-case path
        bool filesAdded = false;
        std::map< std::string, Formats::Table > disc;  // the disc's tables, parsed once
        std::vector< std::string > mods;
    };

    Work* table(ArchiveWork& a, const std::string& path) {
        auto it = a.tables.find(path);
        if (it != a.tables.end()) return &it->second;
        const Bytes* pBytes = a.archive.file(path);
        if (pBytes == nullptr) return nullptr;
        std::optional< Formats::Table > parsed = Formats::Table::parse(*pBytes);
        if (!parsed) return nullptr;
        const Formats::Table& disc = a.disc.emplace(path, *parsed).first->second;
        Work w;
        w.table = *parsed;
        w.pDisc = &disc;
        for (int r = 0; r < parsed->rowCount(); r++) w.discIndex.push_back(r);
        return &a.tables.emplace(path, std::move(w)).first->second;
    }

    int position(const Work& w, int discRow) {
        auto it = std::find(w.discIndex.begin(), w.discIndex.end(), discRow);
        return it == w.discIndex.end() ? -1 : static_cast< int >(it - w.discIndex.begin());
    }

    bool setCells(Work& w, int row, const Json::Value& cells, std::string* pError) {
        for (const auto& [name, value] : cells.members()) {
            const int column = w.table.column(columnHash(name));
            if (column < 0) {
                *pError = "no column " + name;
                return false;
            }
            CellValue cell;
            if (!toCell(value, w.table.columns()[column].type, &cell)) {
                *pError = "the value of " + name + " doesn't fit its column";
                return false;
            }
            w.table.set(row, column, cell);
        }
        return true;
    }

    // One mod's ops on one archive, all or nothing (`a` is left as it was on failure).
    bool applyOps(ArchiveWork& a, const Json::Value& ops, std::string* pError) {
        ArchiveWork before = a;
        auto fail = [&](const std::string& why) {
            a = std::move(before);
            *pError = why;
            return false;
        };
        // Pointers into `a` (pDisc) must follow it when it is restored or copied: re-point them.
        auto repoint = [](ArchiveWork& w) {
            for (auto& [path, t] : w.tables) {
                auto it = w.disc.find(path);
                t.pDisc = it != w.disc.end() ? &it->second : nullptr;
            }
        };
        repoint(before);
        static const char* const kOrder[] = {"addFile", "addColumn", "set", "delete", "add"};
        for (const char* pPhase : kOrder) {
            std::vector< const Json::Value* > phase;
            for (const Json::Value& op : ops.items()) {
                if (op["op"].asString() == pPhase) phase.push_back(&op);
            }
            if (std::string(pPhase) == "add") {
                std::stable_sort(phase.begin(), phase.end(),
                                 [](const Json::Value* x, const Json::Value* y) { return (*x)["at"].asInt() < (*y)["at"].asInt(); });
            }
            for (const Json::Value* pOp : phase) {
                const Json::Value& op = *pOp;
                const std::string path = lower(op["table"].asString());
                const std::string kind = pPhase;
                if (kind == "addFile") {
                    if (a.archive.has(path) || a.tables.count(path)) continue;  // already there: nothing to do
                    Work w;
                    if (op.has("schemaOf")) {
                        Work* pFrom = table(a, lower(op["schemaOf"].asString()));
                        if (pFrom == nullptr || pFrom->pDisc == nullptr) return fail("addFile " + path + ": no table " + op["schemaOf"].asString());
                        w.table = pFrom->pDisc->schemaOnly();
                    } else {
                        for (const Json::Value& c : op["columns"].items()) {
                            std::optional< ColumnType > type = typeFromName(c["type"].asString());
                            if (!type) return fail("addFile " + path + ": unknown type " + c["type"].asString());
                            CellValue initial;
                            initial.kind = isString(*type) ? CellValue::Kind::String
                                           : *type == ColumnType::Float ? CellValue::Kind::Float
                                                                         : CellValue::Kind::Int;
                            w.table.addColumn(columnHash(c["name"].asString()), *type, initial);
                        }
                    }
                    w.touched = true;
                    a.tables.emplace(path, std::move(w));
                    a.filesAdded = true;
                    continue;
                }
                Work* pWork = table(a, path);
                if (pWork == nullptr) return fail(kind + ": no table " + path);
                Work& w = *pWork;
                w.touched = true;
                if (kind == "addColumn") {
                    std::optional< ColumnType > type = typeFromName(op["type"].asString());
                    if (!type) return fail("addColumn: unknown type " + op["type"].asString());
                    CellValue initial;
                    if (op.has("default") && !toCell(op["default"], *type, &initial)) return fail("addColumn: a bad default");
                    if (!op.has("default")) initial.kind = isString(*type) ? CellValue::Kind::String : *type == ColumnType::Float ? CellValue::Kind::Float : CellValue::Kind::Int;
                    w.table.addColumn(columnHash(op["name"].asString()), *type, initial);
                } else if (kind == "set" || kind == "delete") {
                    const int discRow = op["row"].asInt(-1);
                    if (w.pDisc == nullptr || discRow < 0 || discRow >= w.pDisc->rowCount()) return fail(kind + " " + path + ": no disc row " + std::to_string(discRow));
                    if (fingerprint(*w.pDisc, discRow) != op["fp"].asString()) {
                        return fail(path + " row " + std::to_string(discRow) + " is not the row the patch was made against (another disc?)");
                    }
                    const int at = position(w, discRow);
                    if (kind == "delete") {
                        if (at >= 0) {  // deleting twice (two mods) is fine
                            w.table.removeRow(at);
                            w.discIndex.erase(w.discIndex.begin() + at);
                        }
                    } else if (at >= 0) {  // a set on a row another mod deleted is dropped
                        std::string error;
                        if (!setCells(w, at, op["cells"], &error)) return fail("set " + path + ": " + error);
                    }
                } else if (kind == "add") {
                    const int last = w.table.addRow(-1);
                    std::string error;
                    if (!setCells(w, last, op["cells"], &error)) return fail("add " + path + ": " + error);
                    const int at = std::clamp(op["at"].asInt(last), 0, last);
                    if (at != last) {
                        Bytes record = w.table.rowRecord(last);
                        w.table.removeRow(last);
                        w.table.insertRowRecord(at, std::move(record));
                    }
                    w.discIndex.insert(w.discIndex.begin() + at, -1);
                }
            }
        }
        return true;
    }

    Bytes finish(ArchiveWork& a) {
        if (!a.filesAdded) {
            for (auto& [path, w] : a.tables) {
                if (w.touched) a.archive.replace(path, w.table.build());
            }
            return a.archive.build();
        }
        // New tables: the archive is made again from all of its files (as EditModel::changedArchives).
        std::vector< std::pair< std::string, Bytes > > files;
        for (const std::string& path : a.archive.files()) {
            auto it = a.tables.find(path);
            files.emplace_back(path, it != a.tables.end() && it->second.touched ? it->second.table.build() : *a.archive.file(path));
        }
        for (auto& [path, w] : a.tables) {
            if (!a.archive.has(path)) files.emplace_back(path, w.table.build());
        }
        return Formats::Archive::create(a.archive.root(), files).build();
    }

    std::string readText(const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        return text.str();
    }

    struct AssetBuilderEntry {
        std::string version;
        AssetBuilder builder;
    };
    std::vector< AssetBuilderEntry >& assetBuilders() {
        static std::vector< AssetBuilderEntry > sBuilders;
        return sBuilders;
    }

    std::optional< Bytes > readFile(const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return std::nullopt;
        return Bytes(std::istreambuf_iterator< char >(in), std::istreambuf_iterator< char >());
    }
}  // namespace

std::string fingerprint(const Formats::Table& t, int row) {
    std::uint64_t h = kFnvBasis;
    for (int c = 0; c < static_cast< int >(t.columns().size()); c++) {
        const CellValue v = t.get(row, c);
        h = fnv(h, &t.columns()[c].hash, sizeof(std::uint32_t));
        if (v.kind == CellValue::Kind::String) {
            h = fnv(h, v.s.data(), v.s.size());
        } else if (v.kind == CellValue::Kind::Float) {
            h = fnv(h, &v.f, sizeof(float));
        } else {
            h = fnv(h, &v.i, sizeof(std::int32_t));
        }
    }
    return hex64(h);
}

std::uint32_t columnHash(const std::string& name) {
    if (name.size() == 9 && name[0] == '#') {
        return static_cast< std::uint32_t >(std::strtoul(name.c_str() + 1, nullptr, 16));
    }
    return Formats::jmapHash(name.c_str());
}

const char* typeName(ColumnType type) {
    switch (type) {
    case ColumnType::Long: return "long";
    case ColumnType::InlineString: return "string";
    case ColumnType::Float: return "float";
    case ColumnType::Long2: return "long2";
    case ColumnType::Short: return "short";
    case ColumnType::Byte: return "byte";
    case ColumnType::StringOffset: return "stringOffset";
    case ColumnType::Null: return "null";
    }
    return "long";
}

std::optional< ColumnType > typeFromName(const std::string& name) {
    for (ColumnType type : {ColumnType::Long, ColumnType::InlineString, ColumnType::Float, ColumnType::Long2, ColumnType::Short,
                            ColumnType::Byte, ColumnType::StringOffset}) {
        if (name == typeName(type)) return type;
    }
    return std::nullopt;
}

std::vector< Built > build(const std::vector< Source >& patches, const DiscReader& readDisc, std::vector< std::string >* pLog) {
    auto log = [&](const std::string& line) {
        if (pLog != nullptr) pLog->push_back(line);
    };
    // Archive path (as the patch names it) -> each mod's ops on it, in priority order.
    std::map< std::string, std::vector< std::pair< std::string, Json::Value > > > byArchive;
    std::map< std::string, std::string > spelling;  // lower-case path -> the first spelling seen
    for (const Source& source : patches) {
        std::string error;
        std::optional< Json::Value > doc = Json::parse(source.text, &error);
        if (!doc || !doc->isObject() || (*doc)["format"].asInt() != 1) {
            log("mod " + source.mod + ": its edits file is not a format-1 patch (" + (error.empty() ? "wrong format" : error) + "); skipped");
            continue;
        }
        for (const auto& [path, ops] : (*doc)["archives"].members()) {
            spelling.emplace(lower(path), path);
            byArchive[lower(path)].emplace_back(source.mod, ops);
        }
    }
    std::vector< Built > out;
    for (auto& [key, mods] : byArchive) {
        const std::string& path = spelling[key];
        std::optional< Bytes > raw = readDisc(path);
        std::optional< Formats::Archive > archive = raw ? Formats::Archive::parse(*raw) : std::nullopt;
        if (!archive) {
            log(path + ": not on the disc (or not an archive); its edits are skipped");
            continue;
        }
        ArchiveWork work;
        work.archive = std::move(*archive);
        for (auto& [mod, ops] : mods) {
            std::string error;
            if (applyOps(work, ops, &error)) {
                work.mods.push_back(mod);
            } else {
                log("mod " + mod + ": " + path + " left without its edits: " + error);
            }
        }
        if (work.mods.empty()) continue;
        out.push_back(Built{path, finish(work), work.mods});
    }
    return out;
}

void registerAssetBuilder(const std::string& version, AssetBuilder builder) {
    assetBuilders().push_back({version, std::move(builder)});
}

std::vector< std::pair< std::string, fs::path > > buildForLoader(const std::vector< fs::path >& roots, const fs::path& discFiles,
                                                               const fs::path& cacheDirectory, std::vector< std::string >* pLog) {
    std::vector< Source > sources;
    std::uint64_t key = kFnvBasis;
    for (const fs::path& root : roots) {
        std::error_code ec;
        std::vector< fs::path > files;
        for (fs::directory_iterator it(root / "edits", ec), end; !ec && it != end; it.increment(ec)) {
            if (it->path().extension() == ".json") files.push_back(it->path());
        }
        std::sort(files.begin(), files.end());
        for (const fs::path& file : files) {
            Source source{root.filename().string(), readText(file)};
            key = fnv(key, source.mod.data(), source.mod.size() + 1);
            key = fnv(key, source.text.data(), source.text.size());
            sources.push_back(std::move(source));
        }
    }
    for (const AssetBuilderEntry& b : assetBuilders()) key = fnv(key, b.version.data(), b.version.size() + 1);
    // An editor mod's own stage archives (a new galaxy's): the asset builders read them (terrain
    // brushes in them are baked), so they are part of the key too.
    bool ownStages = false;
    for (const fs::path& root : roots) {
        std::error_code ec;
        std::vector< fs::path > stages;
        for (fs::directory_iterator it(root / "files" / "StageData", ec), end; !ec && it != end; it.increment(ec)) {
            if (it->path().extension() == ".arc" && it->is_regular_file(ec)) stages.push_back(it->path());
        }
        std::sort(stages.begin(), stages.end());
        for (const fs::path& file : stages) {
            ownStages = true;
            const std::string name = file.lexically_relative(root).generic_string();
            const std::uintmax_t size = fs::file_size(file, ec);
            const auto time = fs::last_write_time(file, ec).time_since_epoch().count();
            key = fnv(key, name.data(), name.size());
            key = fnv(key, &size, sizeof(size));
            key = fnv(key, &time, sizeof(time));
        }
    }
    for (const fs::path& root : roots) {
        std::error_code ec;
        for (fs::recursive_directory_iterator it(root / "assets", ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const std::string name = it->path().lexically_relative(root).generic_string();
            const std::uintmax_t size = it->file_size(ec);
            const auto time = it->last_write_time(ec).time_since_epoch().count();
            key = fnv(key, name.data(), name.size());
            key = fnv(key, &size, sizeof(size));
            key = fnv(key, &time, sizeof(time));
        }
    }
    std::vector< std::pair< std::string, fs::path > > out;
    if (sources.empty() && (!ownStages || assetBuilders().empty())) return out;
    auto log = [&](const std::string& line) {
        if (pLog != nullptr) pLog->push_back(line);
    };
    // The cache is keyed by the patches and the disc files they read (size and time: the disc
    // folder is the player's extraction and doesn't change under us).
    std::vector< std::string > discPaths;
    for (const Source& s : sources) {
        if (std::optional< Json::Value > doc = Json::parse(s.text)) {
            for (const auto& member : (*doc)["archives"].members()) discPaths.push_back(member.first);
        }
    }
    std::sort(discPaths.begin(), discPaths.end());
    for (const std::string& path : discPaths) {
        std::error_code ec;
        const std::uintmax_t size = fs::file_size(discFiles / path, ec);
        const auto time = fs::last_write_time(discFiles / path, ec).time_since_epoch().count();
        key = fnv(key, path.data(), path.size());
        key = fnv(key, &size, sizeof(size));
        key = fnv(key, &time, sizeof(time));
    }
    const fs::path cache = cacheDirectory / hex64(key);
    const fs::path manifest = cache / "built.txt";
    std::error_code ec;
    if (fs::is_regular_file(manifest, ec)) {
        std::istringstream lines(readText(manifest));
        std::string path;
        while (std::getline(lines, path)) {
            if (!path.empty() && fs::is_regular_file(cache / "files" / path, ec)) out.emplace_back(path, cache / "files" / path);
        }
        log("patch mods: " + std::to_string(out.size()) + " archive(s) from the cache " + cache.filename().string());
        return out;
    }
    // The row edits, then the generated files. A mod a builder rejects is left out and the rest
    // is built again without it, so a broken mod never serves half its files.
    const DiscReader readDisc = [&](const std::string& path) { return readFile(discFiles / path); };
    std::vector< fs::path > active = roots;
    std::vector< Built > built;
    while (true) {
        std::vector< Source > activeSources;
        for (const Source& s : sources) {
            for (const fs::path& root : active) {
                if (root.filename().string() == s.mod) activeSources.push_back(s);
            }
        }
        std::vector< std::string > buildLog;
        built = build(activeSources, readDisc, &buildLog);
        for (const std::string& line : buildLog) log(line);
        std::string failed;
        for (const fs::path& root : active) {
            for (const AssetBuilderEntry& b : assetBuilders()) {
                std::string error;
                if (!b.builder(root, readDisc, &built, pLog, &error)) {
                    log("mod " + root.filename().string() + ": not applied: " + error);
                    failed = root.filename().string();
                    break;
                }
            }
            if (!failed.empty()) break;
        }
        if (failed.empty()) break;
        active.erase(std::remove_if(active.begin(), active.end(), [&](const fs::path& r) { return r.filename().string() == failed; }),
                     active.end());
    }
    // Older builds are of patches or a disc that changed: replace them.
    for (fs::directory_iterator it(cacheDirectory, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_directory() && it->path() != cache) fs::remove_all(it->path(), ec);
    }
    std::string list;
    for (const Built& b : built) {
        const fs::path file = cache / "files" / b.path;
        fs::create_directories(file.parent_path(), ec);
        std::ofstream outFile(file, std::ios::binary | std::ios::trunc);
        outFile.write(reinterpret_cast< const char* >(b.bytes.data()), static_cast< std::streamsize >(b.bytes.size()));
        if (!outFile) {
            log("patch mods: cannot write " + file.string());
            continue;
        }
        std::string mods;
        for (const std::string& m : b.mods) mods += (mods.empty() ? "" : "+") + m;
        log("patch mods: built " + b.path + " (" + mods + ")");
        out.emplace_back(b.path, file);
        list += b.path + "\n";
    }
    std::ofstream(manifest, std::ios::trunc) << list;  // last: a build cut short is redone
    return out;
}

}  // namespace PetariEditor::Patch
