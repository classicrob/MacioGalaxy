// The mod galaxy registry. See petari/platform/mod_galaxy.hpp.
#include "petari/platform/mod_galaxy.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <iterator>
#include <mutex>

namespace PetariNative::Platform::ModGalaxy {

namespace {
    // A small JSON reader: objects, arrays, strings (with \uXXXX), numbers, true/false/null.
    struct Json {
        enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
        bool b = false;
        double n = 0.0;
        std::string s;
        std::vector< Json > a;
        std::map< std::string, Json > o;

        const Json* get(const char* key) const {
            auto it = o.find(key);
            return kind == Object && it != o.end() ? &it->second : nullptr;
        }
    };

    class Parser {
    public:
        explicit Parser(const std::string& text) : t(text) {}

        bool parse(Json& out) {
            if (!value(out, 0)) {
                return false;
            }
            space();
            return i == t.size();
        }

    private:
        void space() {
            while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) {
                i++;
            }
        }

        static void utf8(std::string& out, std::uint32_t c) {
            if (c < 0x80) {
                out.push_back(static_cast< char >(c));
            } else if (c < 0x800) {
                out.push_back(static_cast< char >(0xC0 | (c >> 6)));
                out.push_back(static_cast< char >(0x80 | (c & 0x3F)));
            } else if (c < 0x10000) {
                out.push_back(static_cast< char >(0xE0 | (c >> 12)));
                out.push_back(static_cast< char >(0x80 | ((c >> 6) & 0x3F)));
                out.push_back(static_cast< char >(0x80 | (c & 0x3F)));
            } else {
                out.push_back(static_cast< char >(0xF0 | (c >> 18)));
                out.push_back(static_cast< char >(0x80 | ((c >> 12) & 0x3F)));
                out.push_back(static_cast< char >(0x80 | ((c >> 6) & 0x3F)));
                out.push_back(static_cast< char >(0x80 | (c & 0x3F)));
            }
        }

        bool hex4(std::uint32_t* pOut) {
            if (i + 4 > t.size()) {
                return false;
            }
            std::uint32_t v = 0;
            for (int k = 0; k < 4; k++) {
                const char c = t[i++];
                v <<= 4;
                if (c >= '0' && c <= '9') {
                    v |= static_cast< std::uint32_t >(c - '0');
                } else if (c >= 'a' && c <= 'f') {
                    v |= static_cast< std::uint32_t >(c - 'a' + 10);
                } else if (c >= 'A' && c <= 'F') {
                    v |= static_cast< std::uint32_t >(c - 'A' + 10);
                } else {
                    return false;
                }
            }
            *pOut = v;
            return true;
        }

        bool str(std::string& out) {
            i++;  // the opening quote
            while (i < t.size() && t[i] != '"') {
                if (t[i] != '\\') {
                    out.push_back(t[i++]);
                    continue;
                }
                if (++i >= t.size()) {
                    return false;
                }
                const char e = t[i++];
                switch (e) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'r': out.push_back('\r'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'u': {
                    std::uint32_t c;
                    if (!hex4(&c)) {
                        return false;
                    }
                    if (c >= 0xD800 && c < 0xDC00 && i + 6 <= t.size() && t[i] == '\\' && t[i + 1] == 'u') {
                        i += 2;
                        std::uint32_t low;
                        if (!hex4(&low)) {
                            return false;
                        }
                        c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
                    }
                    utf8(out, c);
                    break;
                }
                default: out.push_back(e); break;
                }
            }
            if (i >= t.size()) {
                return false;
            }
            i++;
            return true;
        }

        bool value(Json& out, int depth) {
            space();
            if (i >= t.size() || depth > 32) {
                return false;
            }
            const char c = t[i];
            if (c == '{') {
                out.kind = Json::Object;
                i++;
                space();
                if (i < t.size() && t[i] == '}') {
                    i++;
                    return true;
                }
                while (true) {
                    space();
                    std::string key;
                    if (i >= t.size() || t[i] != '"' || !str(key)) {
                        return false;
                    }
                    space();
                    if (i >= t.size() || t[i++] != ':') {
                        return false;
                    }
                    if (!value(out.o[key], depth + 1)) {
                        return false;
                    }
                    space();
                    if (i < t.size() && t[i] == ',') {
                        i++;
                        continue;
                    }
                    if (i < t.size() && t[i] == '}') {
                        i++;
                        return true;
                    }
                    return false;
                }
            }
            if (c == '[') {
                out.kind = Json::Array;
                i++;
                space();
                if (i < t.size() && t[i] == ']') {
                    i++;
                    return true;
                }
                while (true) {
                    out.a.emplace_back();
                    if (!value(out.a.back(), depth + 1)) {
                        return false;
                    }
                    space();
                    if (i < t.size() && t[i] == ',') {
                        i++;
                        continue;
                    }
                    if (i < t.size() && t[i] == ']') {
                        i++;
                        return true;
                    }
                    return false;
                }
            }
            if (c == '"') {
                out.kind = Json::String;
                return str(out.s);
            }
            if (t.compare(i, 4, "true") == 0) {
                out.kind = Json::Bool;
                out.b = true;
                i += 4;
                return true;
            }
            if (t.compare(i, 5, "false") == 0) {
                out.kind = Json::Bool;
                i += 5;
                return true;
            }
            if (t.compare(i, 4, "null") == 0) {
                i += 4;
                return true;
            }
            const std::size_t start = i;
            while (i < t.size() && (std::strchr("+-.eE", t[i]) != nullptr || (t[i] >= '0' && t[i] <= '9'))) {
                i++;
            }
            if (i == start) {
                return false;
            }
            out.kind = Json::Number;
            out.n = std::strtod(t.substr(start, i - start).c_str(), nullptr);
            return true;
        }

        const std::string& t;
        std::size_t i = 0;
    };

    std::u16string utf16(const std::string& s) {
        std::u16string out;
        for (std::size_t i = 0; i < s.size();) {
            const unsigned char c = static_cast< unsigned char >(s[i]);
            std::uint32_t cp;
            int extra;
            if (c < 0x80) {
                cp = c;
                extra = 0;
            } else if ((c & 0xE0) == 0xC0) {
                cp = c & 0x1F;
                extra = 1;
            } else if ((c & 0xF0) == 0xE0) {
                cp = c & 0x0F;
                extra = 2;
            } else {
                cp = c & 0x07;
                extra = 3;
            }
            i++;
            for (int k = 0; k < extra && i < s.size(); k++, i++) {
                cp = (cp << 6) | (static_cast< unsigned char >(s[i]) & 0x3F);
            }
            if (cp >= 0x10000) {
                cp -= 0x10000;
                out.push_back(static_cast< char16_t >(0xD800 + (cp >> 10)));
                out.push_back(static_cast< char16_t >(0xDC00 + (cp & 0x3FF)));
            } else {
                out.push_back(static_cast< char16_t >(cp));
            }
        }
        return out;
    }

    // A text object ({"en": "...", "fr": "..."}) or a plain string (English).
    bool texts(const Json* pValue, std::map< std::string, std::u16string >* pOut) {
        if (pValue == nullptr) {
            return false;
        }
        if (pValue->kind == Json::String) {
            (*pOut)["en"] = utf16(pValue->s);
            return true;
        }
        if (pValue->kind != Json::Object) {
            return false;
        }
        for (const auto& [language, text] : pValue->o) {
            if (text.kind == Json::String) {
                (*pOut)[language] = utf16(text.s);
            }
        }
        return pOut->count("en") != 0;
    }

    bool validName(const std::string& name) {
        if (name.empty() || name.size() > 40) {  // GameDataTemporaryInGalaxy keeps 48 bytes; "Appear%s" 0x30
            return false;
        }
        for (char c : name) {
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
                return false;
            }
        }
        return true;
    }

    std::mutex gMutex;
    const char* (*gLanguageProvider)() = nullptr;
    std::deque< Galaxy > gGalaxies;  // deque: message pointers stay valid as galaxies are added

    const std::u16string* pick(const std::map< std::string, std::u16string >& texts, const char* pLanguage) {
        auto it = pLanguage != nullptr ? texts.find(pLanguage) : texts.end();
        if (it == texts.end()) {
            it = texts.find("en");
        }
        return it != texts.end() ? &it->second : nullptr;
    }

    Galaxy* findLocked(const char* pName) {
        for (Galaxy& galaxy : gGalaxies) {
            if (!galaxy.rejected && galaxy.name == pName) {
                return &galaxy;
            }
        }
        return nullptr;
    }
}  // namespace

bool loadText(const std::string& text, const std::string& mod, std::string* pError) {
    Json root;
    if (!Parser(text).parse(root) || root.kind != Json::Object) {
        *pError = "not valid JSON";
        return false;
    }
    const Json* pFormat = root.get("format");
    if (pFormat == nullptr || pFormat->kind != Json::Number || pFormat->n != 1) {
        *pError = "\"format\" must be 1";
        return false;
    }
    const Json* pList = root.get("galaxies");
    if (pList == nullptr || pList->kind != Json::Array || pList->a.empty()) {
        *pError = "\"galaxies\" must be a non-empty list";
        return false;
    }
    std::vector< Galaxy > parsed;
    for (const Json& entry : pList->a) {
        Galaxy galaxy;
        galaxy.mod = mod;
        const Json* pName = entry.get("name");
        if (pName == nullptr || pName->kind != Json::String || !validName(pName->s)) {
            *pError = "a galaxy needs a \"name\" of letters, digits and _ (at most 40)";
            return false;
        }
        galaxy.name = pName->s;
        if (!texts(entry.get("title"), &galaxy.title)) {
            *pError = galaxy.name + ": \"title\" needs an \"en\" text";
            return false;
        }
        if (!texts(entry.get("shortTitle"), &galaxy.shortTitle)) {
            galaxy.shortTitle = galaxy.title;
        }
        if (const Json* pMissions = entry.get("missions"); pMissions != nullptr && pMissions->kind == Json::Array) {
            for (const Json& mission : pMissions->a) {
                galaxy.missions.emplace_back();
                if (!texts(&mission, &galaxy.missions.back())) {
                    *pError = galaxy.name + ": a mission needs an \"en\" name";
                    return false;
                }
            }
        }
        if (const Json* pMusic = entry.get("music"); pMusic != nullptr && pMusic->kind == Json::String) {
            galaxy.music = pMusic->s;
        }
        parsed.push_back(std::move(galaxy));
    }
    std::lock_guard< std::mutex > lock(gMutex);
    for (Galaxy& galaxy : parsed) {
        if (findLocked(galaxy.name.c_str()) == nullptr) {
            gGalaxies.push_back(std::move(galaxy));
        }
    }
    return true;
}

void load(const std::vector< std::filesystem::path >& modRoots, std::vector< std::string >* pLog) {
    for (const std::filesystem::path& root : modRoots) {
        const std::filesystem::path file = root / "galaxy.json";
        std::ifstream stream(file, std::ios::binary);
        if (!stream) {
            continue;
        }
        const std::string text((std::istreambuf_iterator< char >(stream)), std::istreambuf_iterator< char >());
        const std::string mod = root.filename().string();
        const std::size_t before = all().size();
        std::string error;
        if (!loadText(text, mod, &error)) {
            pLog->push_back("galaxy registry: " + mod + "/galaxy.json refused: " + error);
            continue;
        }
        for (const Galaxy* pGalaxy : all()) {
            if (pGalaxy->mod == mod) {
                pLog->push_back("galaxy registry: " + pGalaxy->name + " from mod " + mod + " (" + std::to_string(pGalaxy->missions.size()) +
                                " missions" + (pGalaxy->music.empty() ? "" : ", music of " + pGalaxy->music) + ")");
            }
        }
        if (all().size() == before) {
            pLog->push_back("galaxy registry: " + mod + "/galaxy.json added nothing (names already registered)");
        }
    }
}

void clear() {
    std::lock_guard< std::mutex > lock(gMutex);
    gGalaxies.clear();
}

const Galaxy* find(const char* pName) {
    if (pName == nullptr) {
        return nullptr;
    }
    std::lock_guard< std::mutex > lock(gMutex);
    return findLocked(pName);
}

std::vector< const Galaxy* > all() {
    std::lock_guard< std::mutex > lock(gMutex);
    std::vector< const Galaxy* > out;
    for (const Galaxy& galaxy : gGalaxies) {
        if (!galaxy.rejected) {
            out.push_back(&galaxy);
        }
    }
    return out;
}

void setLanguageProvider(const char* (*pProvider)()) {
    gLanguageProvider = pProvider;
}

const char16_t* message(const char* pId, const char* pLanguage) {
    if (pId == nullptr) {
        return nullptr;
    }
    if (pLanguage == nullptr) {
        pLanguage = gLanguageProvider != nullptr ? gLanguageProvider() : "en";
    }
    std::lock_guard< std::mutex > lock(gMutex);
    if (gGalaxies.empty()) {
        return nullptr;
    }
    const std::string id(pId);
    auto suffix = [&](const char* pPrefix, std::string* pRest) {
        const std::size_t length = std::strlen(pPrefix);
        if (id.compare(0, length, pPrefix) != 0) {
            return false;
        }
        *pRest = id.substr(length);
        return true;
    };
    std::string rest;
    const std::u16string* pText = nullptr;
    if (suffix("GalaxyNameShort_", &rest)) {
        if (Galaxy* pGalaxy = findLocked(rest.c_str())) {
            pText = pick(pGalaxy->shortTitle, pLanguage);
        }
    } else if (suffix("GalaxyName_", &rest)) {
        if (Galaxy* pGalaxy = findLocked(rest.c_str())) {
            pText = pick(pGalaxy->title, pLanguage);
        }
    } else if (suffix("ScenarioName_", &rest)) {
        // <G><n>: the longest registered name that prefixes it, then the mission number.
        for (Galaxy& galaxy : gGalaxies) {
            if (galaxy.rejected || rest.size() <= galaxy.name.size() || rest.compare(0, galaxy.name.size(), galaxy.name) != 0) {
                continue;
            }
            const std::string number = rest.substr(galaxy.name.size());
            if (number.find_first_not_of("0123456789") != std::string::npos) {
                continue;
            }
            const int n = std::atoi(number.c_str());
            if (n >= 1 && n <= static_cast< int >(galaxy.missions.size())) {
                pText = pick(galaxy.missions[n - 1], pLanguage);
            }
        }
    }
    return pText != nullptr ? pText->c_str() : nullptr;
}

const char* musicFrom(const char* pGalaxy) {
    const Galaxy* pFound = find(pGalaxy);
    return pFound != nullptr && !pFound->music.empty() ? pFound->music.c_str() : nullptr;
}

void reject(const char* pName, const char* pReason) {
    std::lock_guard< std::mutex > lock(gMutex);
    if (Galaxy* pGalaxy = findLocked(pName)) {
        pGalaxy->rejected = true;
        std::fprintf(stderr, "[mods] galaxy registry: %s rejected: %s\n", pName, pReason);
    }
}

std::uint16_t saveKey(const char* pName) {
    std::uint32_t h = 0;
    for (const char* p = pName; *p != '\0'; p++) {
        h = static_cast< std::uint32_t >(static_cast< signed char >(*p)) + h * 31;
    }
    return static_cast< std::uint16_t >(h);
}

}  // namespace PetariNative::Platform::ModGalaxy
