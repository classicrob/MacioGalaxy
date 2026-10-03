// The Level: open, the command dispatcher, the journal (level_api.hpp).
#include "level_internal.hpp"

#include "checks.hpp"
#include "base.hpp"
#include "model_io.hpp"
#include "world.hpp"

#include <petari/editor/patch.hpp>
#include <petari/platform/dvd.hpp>
#include <petari/platform/mod_folder.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <chrono>
#include <unistd.h>

namespace PetariEditor::LevelApi {

namespace fs = std::filesystem;
namespace DVD = PetariNative::Platform::DVD;
namespace ModFolder = PetariNative::Platform::ModFolder;

Level::Level() : mImpl(new Impl()) {}
Level::~Level() = default;

const EditModel& Level::model() const {
    return *mImpl->model;
}

namespace {
    // Galaxies on the mounted disc: StageData/<G>/<G>Scenario.arc (mods' new galaxies included).
    std::vector< std::string > discGalaxies() {
        std::vector< std::string > out;
        const fs::path stageData = DVD::mountInfo().filesDirectory / "StageData";
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(stageData, ec)) {
            const std::string name = entry.path().filename().string();
            if (entry.is_directory() && fs::exists(entry.path() / (name + "Scenario.arc"), ec)) out.push_back(name);
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    // Closest names first: those containing the query, then sharing its start.
    std::vector< std::string > closest(const std::vector< std::string >& names, const std::string& query, std::size_t limit) {
        std::vector< std::pair< int, std::string > > scored;
        const std::string q = lower(query);
        for (const std::string& n : names) {
            const std::string l = lower(n);
            int score = 0;
            if (l.find(q) != std::string::npos || (!q.empty() && q.find(l.substr(0, std::min< std::size_t >(l.size(), 4))) != std::string::npos)) score = 2;
            std::size_t common = 0;
            while (common < l.size() && common < q.size() && l[common] == q[common]) common++;
            score += static_cast< int >(common);
            scored.push_back({-score, n});
        }
        std::sort(scored.begin(), scored.end());
        std::vector< std::string > out;
        for (std::size_t i = 0; i < scored.size() && i < limit; i++) out.push_back(scored[i].second);
        return out;
    }
}  // namespace

std::unique_ptr< Level > Level::open(const OpenOptions& options, Error* pError) {
    auto failed = [&](Error e) -> std::unique_ptr< Level > {
        if (pError != nullptr) *pError = std::move(e);
        return nullptr;
    };
    std::error_code ec;
    if (options.disc.empty() || (!fs::is_directory(options.disc / "files", ec) && !fs::is_directory(options.disc / "DATA" / "files", ec))) {
        return failed({"bad-disc", "no extracted disc at '" + options.disc.string() + "' (it needs a files/ folder)",
                       "pass --disc <the folder with files/ and sys/>, e.g. build/game-data/RMGE01", {}, "disc"});
    }
    DVD::MountOptions mount{options.disc};
    if (!options.user.empty()) {
        fs::create_directories(options.user, ec);
        ModFolder::resolveForUser(options.user);  // remember the save/catalog directory, without mounting mutable overlays
    }
    if (DVD::isMounted()) DVD::shutdown();
    std::string error;
    if (!DVD::mount(mount, &error)) return failed({"bad-disc", "cannot mount the disc: " + error, "", {}, "disc"});

    std::unique_ptr< Level > level(new Level());
    level->mOptions = options;
    std::string catalogError;
    try {
        if ((options.userMods || !options.mod.empty()) && options.baseDirectory.empty()) {
            level->mOptions.baseDirectory = fs::temp_directory_path() / ("petari-level-base-" + std::to_string(getpid()) + "-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            level->mImpl->ownedBaseDirectory = level->mOptions.baseDirectory;
        }
        level->mImpl->baseMetadata = options.base.isObject() ? options.base : freezeBase(level->mOptions);
        level->mImpl->mountedSource = frozenSource(level->mOptions, level->mImpl->baseMetadata);
        // Only the pristine disc's problems belong to the baseline, including when continuing a mod.
        auto pristine = ModelIO::load(options.galaxy, std::max(1, options.mission), options.user.string(), {}, &error, &catalogError,
                                     ModelIO::readUnmodded, ModelIO::readUnmodded);
        if (pristine) {
            for (const Check& check : computeChecks(*pristine))
                level->mImpl->baseline.insert(normalizedCheckKey(*pristine, check));
        }
        if (pristine && level->mImpl->baseMetadata["files"].items().empty()) {
            level->mImpl->model = std::move(pristine);
        } else {
            level->mImpl->model = ModelIO::load(options.galaxy, std::max(1, options.mission), options.user.string(), {}, &error, &catalogError,
                                              level->mImpl->mountedSource, ModelIO::readUnmodded);
        }
    } catch (const Failure& failure) {
        return failed(failure.error);
    } catch (const std::exception& exception) {
        return failed({"io", exception.what(), "check the workspace and input mod paths", {}, "base"});
    }
    if (!level->mImpl->model) {
        const std::vector< std::string > galaxies = discGalaxies();
        if (std::find(galaxies.begin(), galaxies.end(), options.galaxy) == galaxies.end()) {
            return failed({"unknown-galaxy", "no galaxy '" + options.galaxy + "' on the disc", "`galaxies` lists them (names end in Galaxy)",
                           closest(galaxies, options.galaxy, 8), "galaxy"});
        }
        return failed({"load-failed", "cannot load " + options.galaxy + ": " + error, "", {}, "galaxy"});
    }
    EditModel& model = *level->mImpl->model;
    if (options.mission < 1 || options.mission > model.missionCount()) {
        return failed({"bad-value", options.galaxy + " has missions 1-" + std::to_string(model.missionCount()), "", {}, "mission"});
    }
    level->mImpl->baseFingerprint = modelFingerprint(model);
    const auto& recorded = level->mImpl->baseMetadata["fingerprint"];
    if (recorded.isString() && recorded.asString() != level->mImpl->baseFingerprint)
        return failed({"bad-workspace", "the frozen base no longer matches its recorded content", "restore the workspace base or explicitly refreeze", {}, "base"});
    level->mImpl->baseMetadata.set("fingerprint", level->mImpl->baseFingerprint);
    return level;
}

Json::Value Level::base() const { return mImpl->baseMetadata; }

// ---- the journal -------------------------------------------------------------------------------------

Json::Value Level::journal() const {
    Json::Value j = Json::Value::object();
    j.set("format", 1);
    j.set("cursor", mImpl->model->historyCursor());
    j.set("next_new", static_cast< double >(mImpl->nextNew));
    Json::Value labels = Json::Value::object();
    for (const auto& [name, row] : mImpl->labels) labels.set(name, rowRef(*mImpl->model, row));
    j.set("labels", std::move(labels));
    Json::Value entries = Json::Value::array();
    for (const JournalEntry& e : mImpl->entries) {
        Json::Value entry = Json::Value::object();
        if (!e.key.empty()) entry.set("key", e.key);
        entry.set("label", e.label);
        entry.set("op", e.op);
        entries.push(std::move(entry));
    }
    j.set("entries", std::move(entries));
    return j;
}

bool Level::replay(const Json::Value& journal, Error* pError) {
    Impl& impl = *mImpl;
    int index = 0;
    for (const Json::Value& e : journal["entries"].items()) {
        Op op;
        std::string error;
        if (!opFromJson(*impl.model, e["op"], &op, &error) || !impl.model->apply(op, &error)) {
            if (pError != nullptr) {
                *pError = {"journal", "journal entry " + std::to_string(index) + " (" + e["label"].asString() + ") no longer applies: " + error,
                           "the disc or base mods changed since; start a new workspace or drop the entry", {}, "entries[" + std::to_string(index) + "]"};
            }
            return false;
        }
        impl.entries.push_back({e["key"].asString(), e["label"].asString(), e["op"]});
        index++;
    }
    const int cursor = journal.has("cursor") ? journal["cursor"].asInt() : index;
    while (impl.model->historyCursor() > cursor && impl.model->undo()) {
    }
    impl.nextNew = static_cast< std::uint64_t >(std::max(1.0, journal["next_new"].asNumber(1)));
    for (const auto& [name, ref] : journal["labels"].members()) {
        const RowId row = parseRowRef(*impl.model, ref.asString());
        if (row.valid()) impl.labels[name] = row;
    }
    return true;
}

// ---- commands ----------------------------------------------------------------------------------------

namespace {
    Json::Value classJson(const Catalog::ClassInfo& c, bool full) {
        Json::Value o = Json::Value::object();
        o.set("class", c.name);
        o.set("name", c.displayName);
        o.set("category", Catalog::categoryName(c.category));
        o.set("description", c.description);
        if (!full) return o;
        o.set("curated", c.curated);
        o.set("placeable", c.placeable);
        Json::Value aliases = Json::Value::array();
        for (const std::string& a : c.aliases) aliases.push(a);
        o.set("aliases", std::move(aliases));
        Json::Value args = Json::Value::array();
        for (const Catalog::Arg& a : c.args) {
            Json::Value j = Json::Value::object();
            j.set("index", a.index);
            j.set("field", "Obj_arg" + std::to_string(a.index));
            if (!a.name.empty()) j.set("name", a.name);
            static const char* const kTypes[] = {"int", "float", "bool", "enum"};
            j.set("type", kTypes[static_cast< int >(a.type)]);
            j.set("default", static_cast< double >(a.defaultValue));
            if (!a.unit.empty()) j.set("unit", a.unit);
            if (!a.help.empty()) j.set("help", a.help);
            if (!a.choices.empty()) {
                Json::Value choices = Json::Value::array();
                for (const Catalog::EnumChoice& ch : a.choices) {
                    Json::Value cj = Json::Value::object();
                    cj.set("value", static_cast< double >(ch.value));
                    cj.set("label", ch.label);
                    choices.push(std::move(cj));
                }
                j.set("choices", std::move(choices));
            }
            args.push(std::move(j));
        }
        o.set("args", std::move(args));
        Json::Value switches = Json::Value::array();
        for (const Catalog::Switch& s : c.switches) {
            Json::Value j = Json::Value::object();
            j.set("field", s.field);
            j.set("role", s.role == Catalog::SwitchRole::Read ? "read" : "write");
            if (!s.help.empty()) j.set("help", s.help);
            switches.push(std::move(j));
        }
        o.set("switches", std::move(switches));
        Json::Value links = Json::Value::array();
        for (const Catalog::Link& l : c.links) {
            static const char* const kKinds[] = {"rail", "group", "camera", "message", "child", "demo", "cast"};
            Json::Value j = Json::Value::object();
            j.set("field", l.field);
            j.set("kind", kKinds[static_cast< int >(l.kind)]);
            j.set("required", l.required);
            links.push(std::move(j));
        }
        o.set("links", std::move(links));
        Json::Value presets = Json::Value::array();
        for (const Catalog::Preset& p : c.presets) presets.push(p.label);
        if (!c.presets.empty()) o.set("presets", std::move(presets));
        o.set("table", Catalog::tablePath(c, "common"));
        o.set("has_collision", c.hasCollision);
        return o;
    }

    int intArg(const Json::Value& args, const char* pKey, int fallback) {
        return args.has(pKey) ? args[pKey].asInt(fallback) : fallback;
    }
}  // namespace

Json::Value Level::help(const Json::Value& args) {
    Json::Value out = Json::Value::object();
    out.set("ok", true);
    try {
        const Json::Value help = helpJson(args.isObject() ? args : Json::Value::object());
        for (const auto& [k, v] : help.members()) out.set(k, v);
    } catch (const Failure& f) {
        Json::Value err = Json::Value::object();
        err.set("ok", false);
        err.set("error", f.error.toJson());
        return err;
    }
    return out;
}

Json::Value Level::call(const std::string& command, const Json::Value& rawArgs) {
    Impl& impl = *mImpl;
    EditModel& model = *impl.model;
    const Json::Value args = rawArgs.isObject() ? rawArgs : Json::Value::object();
    Json::Value out = Json::Value::object();
    out.set("ok", true);
    auto merge = [&](const Json::Value& result) {
        for (const auto& [k, v] : result.members()) out.set(k, v);
    };
    try {
        if (args.has("existing_checks") && args["existing_checks"].type() != Json::Value::Type::Bool)
            fail("bad-value", "existing_checks must be boolean", "omit it for compact replies", {}, "existing_checks");
        if (command == "help") {
            merge(helpJson(args));
        } else if (command == "info") {
            out.set("baseline", "pristine-disc");
            out.set("base", impl.baseMetadata);
            out.set("base_fingerprint", impl.baseFingerprint);
            out.set("source_warnings", sourceWarnings(mOptions, impl.baseMetadata));
            out.set("galaxy", model.galaxy());
            out.set("mission", model.scenario());
            Json::Value missions = Json::Value::array();
            for (int m = 1; m <= model.missionCount(); m++) {
                Json::Value j = Json::Value::object();
                j.set("mission", m);
                j.set("name", model.missionName(m));
                missions.push(std::move(j));
            }
            out.set("missions", std::move(missions));
            Json::Value zones = Json::Value::array();
            for (int z = 0; z < model.zoneCount(); z++) {
                Json::Value j = Json::Value::object();
                j.set("index", z);
                j.set("name", model.zoneName(z));
                j.set("placed", model.zonePlaced(z));
                const Frame f = zoneFrame(model, z);
                j.set("origin", vecJson(f.origin, 1));
                j.set("rot", vecJson(G::toEulerDegrees(f.basis), 2));
                const Mtx34 transform = model.zoneWorldMatrix(z);
                Json::Value matrix = Json::Value::array();
                for (int row = 0; row < 4; ++row) {
                    Json::Value values = Json::Value::array();
                    for (int column = 0; column < 4; ++column)
                        values.push(row < 3 ? static_cast<double>(transform.m[row][column]) : (column == 3 ? 1.0 : 0.0));
                    matrix.push(std::move(values));
                }
                j.set("world_matrix", std::move(matrix));
                int rows = 0;
                for (int t = 0; t < model.tableCount(z); t++) model.forEachRow(z, t, [&](RowId) { rows++; });
                j.set("rows", rows);
                zones.push(std::move(j));
            }
            out.set("zones", std::move(zones));
            out.set("objects", static_cast< int >(impl.allRows().size()));
            out.set("history", model.historySize());
            out.set("cursor", model.historyCursor());
            out.set("unsaved", model.unsaved());
            const std::vector< Check > checks = impl.newChecks();
            int errors = 0;
            for (const Check& c : checks) errors += c.severity == Severity::Error;
            out.set("errors", errors);
            out.set("warnings", static_cast< int >(checks.size()) - errors);
            Json::Value labels = Json::Value::object();
            for (const auto& [name, row] : impl.labels) labels.set("@" + name, rowRef(model, row));
            out.set("labels", std::move(labels));
            out.set("coordinates", "pos/rot are world space (degrees, the game's Rz*Ry*Rx); zone_pos/zone_rot are the row's own");
        } else if (command == "refreeze") {
            for (const auto& [key, value] : args.members()) {
                if (key != "discard_journal" && key != "existing_checks") fail("bad-args", "unknown refreeze key: " + key, "", {}, key);
            }
            if (args.has("discard_journal") && args["discard_journal"].type() != Json::Value::Type::Bool)
                fail("bad-value", "discard_journal must be boolean", "", {}, "discard_journal");
            if (model.historySize() && !args["discard_journal"].asBool())
                fail("journal", "refreeze would discard the journal", "save first, then use discard_journal:true", {}, "discard_journal");
            OpenOptions next = mOptions;
            next.base = Json::Value();
            const fs::path target = next.baseDirectory;
            if (!target.empty()) next.baseDirectory = target.string() + ".refreeze-" + std::to_string(getpid());
            Error error;
            auto fresh = Level::open(next, &error);
            if (!fresh) { Json::Value reply = Json::Value::object(); reply.set("ok", false); reply.set("error", error.toJson()); return reply; }
            if (!target.empty() && fs::exists(next.baseDirectory)) {
                const fs::path old = target.string() + ".refreeze-old-" + std::to_string(getpid());
                std::error_code ec;
                fs::remove_all(old, ec);
                const bool existed = fs::exists(target);
                if (existed) fs::rename(target, old);
                try { fs::rename(next.baseDirectory, target); }
                catch (...) { if (existed) fs::rename(old, target); throw; }
                fs::remove_all(old, ec);
                fresh->mOptions.baseDirectory = target;
                fresh->mImpl->mountedSource = frozenSource(fresh->mOptions, fresh->mImpl->baseMetadata);
            }
            if (!impl.ownedBaseDirectory.empty()) {
                fresh->mImpl->ownedBaseDirectory = target;
                impl.ownedBaseDirectory.clear();
            }
            mOptions = std::move(fresh->mOptions);
            mImpl = std::move(fresh->mImpl);
            Json::Value reply = call("info", args);
            reply.set("refrozen", true);
            return reply;
        } else if (command == "galaxies") {
            Json::Value list = Json::Value::array();
            for (const std::string& g : discGalaxies()) list.push(g);
            out.set("galaxies", std::move(list));
        } else if (command == "catalog") {
            const std::string query = args["query"].asString();
            const int limit = intArg(args, "limit", 30);
            std::vector< const Catalog::ClassInfo* > found;
            if (!query.empty()) {
                found = Catalog::search(query, 400);
            } else {
                for (const Catalog::ClassInfo& c : Catalog::classes()) found.push_back(&c);
            }
            std::map< std::string, int > placed;
            for (RowId row : impl.allRows()) {
                if (const char* n = model.objectName(row)) placed[lower(n)]++;
            }
            const std::string category = lower(args["category"].asString());
            const bool here = args["placed_here"].asBool();
            Json::Value list = Json::Value::array();
            int total = 0;
            for (const Catalog::ClassInfo* c : found) {
                if (!c->placeable && !args["all"].asBool()) continue;
                if (!category.empty() && lower(Catalog::categoryName(c->category)).rfind(category, 0) != 0) continue;
                const int count = placed[lower(c->name)];
                if (here && count == 0) continue;
                if (total++ >= limit) continue;
                Json::Value j = classJson(*c, false);
                if (count > 0) j.set("placed_here", count);
                list.push(std::move(j));
            }
            out.set("total", total);
            out.set("classes", std::move(list));
            if (total > limit) out.set("more", "raise \"limit\" or narrow with \"query\"/\"category\"");
        } else if (command == "class") {
            const Catalog::ClassInfo* c = impl.resolveClass(args["class"].asString(), true, "class");
            Json::Value j = classJson(*c, true);
            Json::Value examples = Json::Value::array();
            Json::Value filter = Json::Value::object();
            filter.set("class", c->name);
            for (RowId row : impl.select(filter, "class")) {
                if (examples.items().size() < 5) examples.push(rowRef(model, row));
            }
            j.set("examples_here", std::move(examples));
            merge(j);
        } else if (command == "list") {
            Json::Value filter = args;
            const bool full = args["full"].asBool();
            const int limit = intArg(args, "limit", 200), offset = intArg(args, "offset", 0);
            for (const char* k : {"full", "limit", "offset"}) {
                Json::Value copy = Json::Value::object();
                for (const auto& [key, v] : filter.members()) {
                    if (key != k) copy.set(key, v);
                }
                filter = std::move(copy);
            }
            const std::vector< RowId > rows = impl.select(filter, "list");
            Json::Value list = Json::Value::array();
            for (int i = offset; i < static_cast< int >(rows.size()) && i < offset + limit; i++) list.push(impl.objectJson(rows[i], full));
            out.set("total", static_cast< int >(rows.size()));
            out.set("objects", std::move(list));
            if (static_cast< int >(rows.size()) > offset + limit) out.set("more", "use \"offset\": " + std::to_string(offset + limit));
        } else if (command == "get") {
            if (args.has("refs")) {
                Json::Value list = Json::Value::array();
                int i = 0;
                for (const Json::Value& r : args["refs"].items()) list.push(impl.objectJson(impl.resolveRef(r, "refs[" + std::to_string(i++) + "]"), true));
                out.set("objects", std::move(list));
            } else {
                merge(impl.objectJson(impl.resolveRef(args["ref"], "ref"), true));
            }
        } else if (command == "apply") {
            const std::size_t before = impl.entries.size();
            merge(impl.applyBatch(args, args["dry_run"].asBool()));
            if (mOnCommit && impl.entries.size() > before && !args["dry_run"].asBool()) mOnCommit(impl.entries.back().op);
        } else if (command == "undo" || command == "redo") {
            const int steps = std::max(1, intArg(args, "steps", 1));
            int done = 0;
            Json::Value labels = Json::Value::array();
            for (; done < steps; done++) {
                const int index = command == "undo" ? model.historyCursor() - 1 : model.historyCursor();
                if (!(command == "undo" ? model.undo() : model.redo())) break;
                labels.push(impl.entries[index].label);
            }
            if (done == 0) fail("nothing-to-" + command, "nothing to " + command, command == "undo" ? "the journal is at its start" : "the journal is at its end");
            out.set("steps", done);
            out.set("labels", std::move(labels));
            out.set("cursor", model.historyCursor());
        } else if (command == "history") {
            Json::Value list = Json::Value::array();
            for (int i = 0; i < static_cast< int >(impl.entries.size()); i++) {
                Json::Value j = Json::Value::object();
                j.set("index", i);
                j.set("label", impl.entries[i].label);
                if (!impl.entries[i].key.empty()) j.set("key", impl.entries[i].key);
                j.set("applied", i < model.historyCursor());
                list.push(std::move(j));
            }
            out.set("cursor", model.historyCursor());
            out.set("entries", std::move(list));
        } else if (command == "checks") {
            merge(impl.checksJson(args["all"].asBool()));
        } else if (command == "diff") {
            Json::Value list = Json::Value::array();
            for (RowId row : impl.allRows()) {
                const RowState s = model.state(row);
                if (s == RowState::Original) continue;
                Json::Value j = Json::Value::object();
                j.set("ref", rowRef(model, row));
                const char* n = model.objectName(row);
                j.set("class", n != nullptr ? n : "");
                j.set("state", s == RowState::Added ? "added" : "changed");
                if (s == RowState::Changed) {
                    int zone = 0, table = 0;
                    model.locate(row, &zone, &table);
                    Json::Value cells = Json::Value::object();
                    for (const Column& c : model.columns(zone, table)) {
                        const Cell* now = model.cell(row, c.hash);
                        const Cell* was = model.originalCell(row, c.hash);
                        if (now == nullptr) continue;
                        auto value = [](const Cell* p) {
                            if (p == nullptr) return Json::Value();
                            if (p->type == CellType::Float) return Json::Value(std::round(p->f * 1000.0) / 1000.0);
                            if (p->type == CellType::String || p->type == CellType::StringOffset) return Json::Value(p->s);
                            return Json::Value(static_cast< double >(p->i));
                        };
                        const Json::Value a = value(was), b = value(now);
                        if (Json::write(a) != Json::write(b)) {
                            Json::Value pair = Json::Value::array();
                            pair.push(a);
                            pair.push(b);
                            cells.set(columnLabel(c.hash), std::move(pair));
                        }
                    }
                    j.set("cells", std::move(cells));
                }
                list.push(std::move(j));
            }
            out.set("rows", std::move(list));
            out.set("note", "deleted rows are not listed: see `history`; cells are [disc, now]");
        } else if (command == "save") {
            if (mOptions.user.empty()) fail("no-user", "save needs a user directory", "open with --user <dir> (the CLI defaults it to <workspace>/user)");
            const std::string mod = args["mod"].asString();
            if (mod.empty()) fail("bad-args", "save needs \"mod\": the mod folder name", "e.g. {\"mod\": \"My Good Egg\"}", {}, "mod");
            if (!ModelIO::validModName(mod)) {
                fail("bad-value", "mod: '" + mod + "' is not a mod folder name", "letters, digits, spaces, '-', '_' and '.'; 1-64 characters", {}, "mod");
            }
            // The editor's own save (as the in-game Save sheet), then on for the next start like the game's.
            std::string summary, error;
            if (!ModelIO::saveToDirectory(model, mOptions.user / "mods" / mod, args["full"].asBool(), &summary, &error, impl.mountedSource)) fail("save-failed", error, "", {}, "mod");
            std::string enableError;
            ModFolder::resolveForUser(mOptions.user);  // the mod list now includes it
            ModFolder::setEnabled(mod, true, &enableError);
            const std::string notice = "Saved " + summary + " to mods/" + mod + (enableError.empty() ? " (on from the next start)" : " (turn it on: " + enableError + ")");
            recordSavedSource(impl.baseMetadata, mOptions.user / "mods" / mod);
            impl.model->markSaved();
            out.set("notice", notice);
            out.set("mod_dir", (mOptions.user / "mods" / mod).string());
            out.set("format", args["full"].asBool() ? "full archives" : "patch");
        } else if (command == "ground" || command == "gravity") {
            const G::V3 p = parseVec(args["pos"], "pos");
            World& w = impl.world();
            const Gravity g = w.gravityAt(model, p);
            const G::V3 down = args.has("down") ? G::normalized(parseVec(args["down"], "down"), {0, -1, 0}) : g.down;
            out.set("down", vecJson(down, 4));
            out.set("gravity", args.has("down") ? std::string("given") : g.source);
            out.set("approximate", !args.has("down"));
            if (command == "ground") {
                // Inside a collider: no ground answer (placement would refuse it); say where the surface is.
                if (std::optional< Hit > inside = w.insideOf(p, down)) {
                    out.set("hit", false);
                    out.set("inside", rowRef(model, inside->row));
                    out.set("surface_above", std::round(inside->distance * 10.0) / 10.0);
                    out.set("hint", "the point is inside that collider: start at least surface_above units higher");
                    return out;
                }
                std::optional< Hit > hit = w.ray(p, down, static_cast< float >(args["max"].asNumber(50000)));
                out.set("hit", hit.has_value());
                if (hit) {
                    out.set("point", vecJson(hit->point, 1));
                    out.set("normal", vecJson(hit->normal, 4));
                    out.set("distance", std::round(hit->distance * 10.0) / 10.0);
                    out.set("on", rowRef(model, hit->row));
                    const char* n = model.objectName(hit->row);
                    out.set("on_class", n != nullptr ? n : "");
                }
            }
        } else if (command == "colliders") {
            World& w = impl.world();
            Json::Value list = Json::Value::array();
            for (const World::Collider& c : w.colliders()) {
                Json::Value j = Json::Value::object();
                j.set("ref", rowRef(model, c.row));
                j.set("class", c.name);
                j.set("triangles", static_cast< int >(c.tris.size()));
                j.set("min", vecJson(c.lo, 0));
                j.set("max", vecJson(c.hi, 0));
                list.push(std::move(j));
            }
            out.set("colliders", std::move(list));
            Json::Value skipped = Json::Value::array();
            for (const std::string& n : w.skipped()) skipped.push(n);
            out.set("no_kcl", std::move(skipped));
        } else if (command == "export") {
            const int from = intArg(args, "from", 0), to = std::min(intArg(args, "to", model.historyCursor()), model.historyCursor());
            Json::Value ops = Json::Value::array();
            for (int i = std::max(0, from); i < to; i++) ops.push(impl.entries[i].op);
            out.set("ops", std::move(ops));
        } else {
            std::vector< std::string > names;
            for (const CommandSpec& s : commands()) names.push_back(s.name);
            fail("unknown-command", "no command '" + command + "'", "`help` lists the commands", closest(names, command, 5));
        }
        if (args["existing_checks"].asBool()) {
            Json::Value existing = Json::Value::array();
            const Json::Value allChecks = impl.checksJson(true);
            for (const auto& check : allChecks["checks"].items()) {
                if (check["shipped"].asBool()) existing.push(check);
            }
            out.set("existing_checks", std::move(existing));
        }
    } catch (const Failure& f) {
        Json::Value err = Json::Value::object();
        err.set("ok", false);
        err.set("error", f.error.toJson());
        return err;
    } catch (const std::exception& e) {
        Json::Value err = Json::Value::object();
        err.set("ok", false);
        err.set("error", Error{"internal", e.what(), "please report this", {}, ""}.toJson());
        return err;
    }
    return out;
}

}  // namespace PetariEditor::LevelApi
