// The object catalog (petari/editor/catalog.hpp). This first version has the API and an empty
// catalog; the generated and curated data (native/editor/catalog/) come with editor/catalog.
#include "petari/editor/catalog.hpp"

#include <algorithm>
#include <cctype>

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

bool load(std::string* /*pError*/) {
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

}  // namespace PetariEditor::Catalog
