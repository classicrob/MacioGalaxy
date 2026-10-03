// The object catalog (petari/editor/catalog.hpp): native/editor/catalog/generated.json (the
// decompilation scan and the class sweep), then curated.json (names, help, enum labels, written by
// hand), then the user's own files (<user>/editor/catalog/*.json, the same schema as curated.json;
// e.g. an imported community names file). Later files override earlier ones field by field.
#include "petari/editor/catalog.hpp"
#include "petari/editor/json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

// The shipped files, embedded at build time (native/editor/CMakeLists.txt).
extern const char* const kPetariCatalogGenerated;
extern const char* const kPetariCatalogCurated;

namespace PetariEditor::Catalog {
namespace {
    std::vector< ClassInfo >& store() {
        static std::vector< ClassInfo > sClasses;
        return sClasses;
    }

    std::string lower(std::string text) {
        for (char& c : text) {
            c = static_cast< char >(std::tolower(static_cast< unsigned char >(c)));
        }
        return text;
    }
}  // namespace

const char* categoryName(Category category) {
    static const char* const sNames[] = {"Enemies",   "Items",      "Platforms", "Planets", "Gravity", "Areas",
                                         "Cameras",   "Characters", "Launchers", "Scenery", "Logic",   "Other"};
    const auto index = static_cast< std::size_t >(category);
    return index < sizeof(sNames) / sizeof(sNames[0]) ? sNames[index] : "Other";
}

namespace {
    Category categoryFrom(const std::string& name) {
        static const std::map< std::string, Category > sMap = {
            {"enemy", Category::Enemy},       {"item", Category::Item},     {"platform", Category::Platform},
            {"planet", Category::Planet},     {"gravity", Category::Gravity}, {"area", Category::Area},
            {"camera", Category::Camera},     {"npc", Category::Npc},       {"launcher", Category::Launcher},
            {"decoration", Category::Decoration}, {"logic", Category::Logic}, {"other", Category::Other}};
        auto it = sMap.find(name);
        return it == sMap.end() ? Category::Other : it->second;
    }

    ArgType argTypeFrom(const std::string& name) {
        return name == "float" ? ArgType::Float : name == "bool" ? ArgType::Bool : name == "enum" ? ArgType::Enum : ArgType::Int;
    }

    LinkKind linkKindFrom(const std::string& name) {
        static const std::map< std::string, LinkKind > sMap = {{"rail", LinkKind::Rail},       {"group", LinkKind::Group},
                                                                {"camera", LinkKind::Camera},   {"message", LinkKind::Message},
                                                                {"child", LinkKind::Child},     {"demo", LinkKind::Demo},
                                                                {"cast", LinkKind::Cast}};
        auto it = sMap.find(name);
        return it == sMap.end() ? LinkKind::Group : it->second;
    }

    // Applies one file's entry over a class: every field present replaces the current one; args,
    // switches and links merge by index or field.
    void merge(ClassInfo& info, const Json::Value& e) {
        if (e.has("displayName")) info.displayName = e["displayName"].asString();
        if (e.has("description")) info.description = e["description"].asString();
        if (e.has("category")) info.category = categoryFrom(e["category"].asString());
        if (e.has("placeable")) info.placeable = e["placeable"].asBool(true);
        if (e.has("live_recreate"))
            info.liveRecreate = e["live_recreate"].asString();
        if (e.has("keepable")) info.keepable = e["keepable"].asBool(false);
        if (e.has("appearSound")) info.appearSound = e["appearSound"].asString();
        if (e.has("table")) info.table = e["table"].asString();
        if (e.has("thumbnailArchive")) info.thumbnailArchive = e["thumbnailArchive"].asString();
        if (e.has("thumbnailJoint")) info.thumbnailJoint = e["thumbnailJoint"].asString();
        if (e.has("curated")) info.curated = e["curated"].asBool(false);
        if (e.has("presets")) {
            info.presets.clear();
            for (const Json::Value& p : e["presets"].items()) {
                Preset preset;
                preset.label = p["label"].asString();
                for (const auto& [index, value] : p["args"].members()) {
                    preset.args.emplace_back(std::atoi(index.c_str()), value.asInt());
                }
                info.presets.push_back(std::move(preset));
            }
        }
        for (const Json::Value& alias : e["aliases"].items()) {
            info.aliases.push_back(alias.asString());
        }
        for (const Json::Value& a : e["archives"].items()) {
            if (std::find(info.archives.begin(), info.archives.end(), a.asString()) == info.archives.end()) {
                info.archives.push_back(a.asString());
            }
        }
        for (const Json::Value& a : e["args"].items()) {
            const int index = a["index"].asInt(-1);
            if (index < 0 || index > 7) continue;
            auto it = std::find_if(info.args.begin(), info.args.end(), [&](const Arg& x) { return x.index == index; });
            if (it == info.args.end()) {
                info.args.push_back(Arg{});
                it = info.args.end() - 1;
                it->index = index;
            }
            if (a.has("name")) it->name = a["name"].asString();
            if (a.has("type")) it->type = argTypeFrom(a["type"].asString());
            if (a.has("default")) {
                // A bool default: on is 1, off is -1 (the game's bool getter reads any value but -1 as on).
                const Json::Value& d = a["default"];
                it->defaultValue = d.type() == Json::Value::Type::Bool ? (d.asBool() ? 1 : -1) : d.asInt(-1);
            }
            if (a.has("unit")) it->unit = a["unit"].asString();
            if (a.has("help")) it->help = a["help"].asString();
            if (a.has("choices")) {
                it->choices.clear();
                for (const Json::Value& c : a["choices"].items()) {
                    it->choices.push_back(EnumChoice{c["value"].asInt(), c["label"].asString()});
                }
            }
        }
        std::sort(info.args.begin(), info.args.end(), [](const Arg& a, const Arg& b) { return a.index < b.index; });
        // One field may be both read and written (moving map parts use SW_A/SW_B this way).
        // An override replaces that field's roles as a group, preserving both roles within a file.
        std::set< std::string > replacedSwitches;
        for (const Json::Value& w : e["switches"].items()) {
            const std::string field = w["field"].asString();
            if (w.has("role") && replacedSwitches.insert(field).second)
                info.switches.erase(std::remove_if(info.switches.begin(), info.switches.end(), [&](const Switch& s) { return s.field == field; }),
                                    info.switches.end());
        }
        for (const Json::Value& w : e["switches"].items()) {
            const std::string field = w["field"].asString();
            const auto role = w["role"].asString() == "write" ? SwitchRole::Write : SwitchRole::Read;
            auto it = std::find_if(info.switches.begin(), info.switches.end(),
                                   [&](const Switch& x) { return x.field == field && (!w.has("role") || x.role == role); });
            if (it == info.switches.end()) {
                info.switches.push_back(Switch{field, role, {}});
                it = info.switches.end() - 1;
            }
            if (w.has("help")) it->help = w["help"].asString();
        }
        for (const Json::Value& l : e["links"].items()) {
            const std::string field = l["field"].asString();
            auto it = std::find_if(info.links.begin(), info.links.end(), [&](const Link& x) { return x.field == field; });
            if (it == info.links.end()) {
                info.links.push_back(Link{});
                it = info.links.end() - 1;
                it->field = field;
            }
            if (l.has("kind")) it->kind = linkKindFrom(l["kind"].asString());
            // A link proved required (the sweep) stays required whatever a later file says.
            if (l.has("required")) it->required = it->required || l["required"].asBool(false);
        }
    }

    bool mergeFile(const std::string& text, const char* pWhat, bool create, std::string* pError) {
        std::string error;
        std::optional< Json::Value > root = Json::parse(text, &error);
        if (!root) {
            if (pError != nullptr) *pError = std::string(pWhat) + ": " + error;
            return false;
        }
        for (const Json::Value& e : (*root)["classes"].items()) {
            const std::string name = e["name"].asString();
            if (name.empty()) continue;
            auto it = std::find_if(store().begin(), store().end(), [&](const ClassInfo& c) { return lower(c.name) == lower(name); });
            if (it == store().end()) {
                if (!create) continue;  // curated or user entries only describe classes the game has
                store().push_back(ClassInfo{});
                it = store().end() - 1;
                it->name = name;
                it->displayName = name;
            }
            merge(*it, e);
        }
        return true;
    }

    bool gLoaded = false;
}  // namespace

void addClass(ClassInfo info) {
    auto it = std::find_if(store().begin(), store().end(), [&](const ClassInfo& c) { return lower(c.name) == lower(info.name); });
    if (it != store().end()) {
        *it = std::move(info);
    } else {
        store().push_back(std::move(info));
    }
    std::sort(store().begin(), store().end(), [](const ClassInfo& a, const ClassInfo& b) {
        return lower(a.displayName) < lower(b.displayName);
    });
}

void addPlanets(const std::vector< std::string >& names) {
    for (const std::string& name : names) {
        if (find(name.c_str()) != nullptr) continue;
        ClassInfo info;
        info.name = name;
        info.displayName = name;
        info.description = "A planet or terrain piece (model and collision from its archive)";
        info.category = Category::Planet;
        info.keepable = true;
        info.hasCollision = true;
        info.archives = {name};
        info.thumbnailArchive = name;
        store().push_back(std::move(info));
    }
    std::sort(store().begin(), store().end(), [](const ClassInfo& a, const ClassInfo& b) {
        return lower(a.displayName) < lower(b.displayName);
    });
}

bool load(std::string* pError) {
    return loadWithUser(std::string(), pError);
}

bool loadWithUser(const std::string& userDirectory, std::string* pError) {
    if (gLoaded) {
        return true;
    }
    gLoaded = true;
    if (!mergeFile(kPetariCatalogGenerated, "generated.json", true, pError) || !mergeFile(kPetariCatalogCurated, "curated.json", false, pError)) {
        return false;
    }
    if (!userDirectory.empty()) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(userDirectory) / "editor" / "catalog", ec)) {
            if (entry.path().extension() != ".json") continue;
            std::ifstream in(entry.path());
            const std::string text((std::istreambuf_iterator< char >(in)), {});
            std::string ignored;
            mergeFile(text, entry.path().filename().c_str(), false, &ignored);  // a broken user file is skipped
        }
    }
    std::sort(store().begin(), store().end(), [](const ClassInfo& a, const ClassInfo& b) {
        return lower(a.displayName) < lower(b.displayName);
    });
    return true;
}

const std::vector< ClassInfo >& classes() {
    return store();
}

const ClassInfo* find(const char* pName) {
    const std::string name = lower(pName);
    for (const ClassInfo& info : store()) {
        if (lower(info.name) == name) {
            return &info;
        }
    }
    return nullptr;
}

std::vector< const ClassInfo* > search(const std::string& query, int limit) {
    // Best first: a display-name prefix, then a name or alias prefix, then any substring.
    const std::string q = lower(query);
    std::vector< std::pair< int, const ClassInfo* > > scored;
    for (const ClassInfo& info : store()) {
        int score = 0;
        const std::string display = lower(info.displayName), name = lower(info.name);
        if (display.rfind(q, 0) == 0) {
            score = 3;
        } else if (name.rfind(q, 0) == 0) {
            score = 2;
        } else if (display.find(q) != std::string::npos || name.find(q) != std::string::npos) {
            score = 1;
        }
        for (const std::string& alias : info.aliases) {
            if (score < 2 && lower(alias).find(q) != std::string::npos) {
                score = lower(alias).rfind(q, 0) == 0 ? 2 : 1;
            }
        }
        if (score > 0) {
            scored.emplace_back(score, &info);
        }
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector< const ClassInfo* > out;
    for (const auto& [score, info] : scored) {
        if (static_cast< int >(out.size()) >= limit) {
            break;
        }
        out.push_back(info);
    }
    return out;
}

std::string tablePath(const ClassInfo& info, const std::string& layer) {
    std::string table = info.table;
    if (table.empty()) {
        table = info.category == Category::Area     ? "jmp/placement/areaobjinfo"
                : info.category == Category::Camera ? "jmp/placement/cameracubeinfo"
                                                    : "jmp/placement/objinfo";
    }
    const size_t leaf = table.rfind('/');
    return table.substr(0, leaf + 1) + layer + table.substr(leaf);
}

}  // namespace PetariEditor::Catalog
