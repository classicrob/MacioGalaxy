// The level editor's small JSON reader and writer (petari/editor/json.hpp).
#include "petari/editor/json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace PetariEditor::Json {
namespace {
    const Value& nullValue() {
        static const Value sNull;
        return sNull;
    }

    struct Parser {
        const std::string& text;
        size_t at = 0;
        std::string error;

        void skip() {
            while (at < text.size() && (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' || text[at] == '\r')) {
                at++;
            }
        }
        bool fail(const char* pWhat) {
            if (error.empty()) {
                error = std::string(pWhat) + " at byte " + std::to_string(at);
            }
            return false;
        }
        bool literal(const char* pWord) {
            const size_t length = std::char_traits< char >::length(pWord);
            if (text.compare(at, length, pWord) != 0) {
                return fail("unexpected token");
            }
            at += length;
            return true;
        }
        static void appendUtf8(std::string& out, std::uint32_t cp) {
            if (cp < 0x80) {
                out += static_cast< char >(cp);
            } else if (cp < 0x800) {
                out += static_cast< char >(0xC0 | (cp >> 6));
                out += static_cast< char >(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                out += static_cast< char >(0xE0 | (cp >> 12));
                out += static_cast< char >(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast< char >(0x80 | (cp & 0x3F));
            } else {
                out += static_cast< char >(0xF0 | (cp >> 18));
                out += static_cast< char >(0x80 | ((cp >> 12) & 0x3F));
                out += static_cast< char >(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast< char >(0x80 | (cp & 0x3F));
            }
        }
        bool hex4(std::uint32_t* pOut) {
            if (at + 4 > text.size()) {
                return fail("short \\u escape");
            }
            *pOut = static_cast< std::uint32_t >(std::strtoul(text.substr(at, 4).c_str(), nullptr, 16));
            at += 4;
            return true;
        }
        bool string(std::string* pOut) {
            if (text[at] != '"') {
                return fail("expected a string");
            }
            at++;
            while (at < text.size() && text[at] != '"') {
                char c = text[at++];
                if (c != '\\') {
                    *pOut += c;
                    continue;
                }
                if (at >= text.size()) {
                    return fail("unterminated escape");
                }
                c = text[at++];
                switch (c) {
                case 'n': *pOut += '\n'; break;
                case 't': *pOut += '\t'; break;
                case 'r': *pOut += '\r'; break;
                case 'b': *pOut += '\b'; break;
                case 'f': *pOut += '\f'; break;
                case 'u': {
                    std::uint32_t cp = 0;
                    if (!hex4(&cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && text.compare(at, 2, "\\u") == 0) {
                        at += 2;
                        std::uint32_t low = 0;
                        if (!hex4(&low)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                    appendUtf8(*pOut, cp);
                    break;
                }
                default: *pOut += c; break;
                }
            }
            if (at >= text.size()) {
                return fail("unterminated string");
            }
            at++;
            return true;
        }
        bool value(Value* pOut, int depth) {
            if (depth > 64) {
                return fail("nesting too deep");
            }
            skip();
            if (at >= text.size()) {
                return fail("unexpected end");
            }
            const char c = text[at];
            if (c == '{') {
                at++;
                *pOut = Value::object();
                skip();
                if (at < text.size() && text[at] == '}') {
                    at++;
                    return true;
                }
                while (true) {
                    skip();
                    std::string key;
                    if (!string(&key)) return false;
                    skip();
                    if (at >= text.size() || text[at] != ':') return fail("expected ':'");
                    at++;
                    Value member;
                    if (!value(&member, depth + 1)) return false;
                    pOut->set(key, std::move(member));
                    skip();
                    if (at < text.size() && text[at] == ',') {
                        at++;
                        continue;
                    }
                    if (at < text.size() && text[at] == '}') {
                        at++;
                        return true;
                    }
                    return fail("expected ',' or '}'");
                }
            }
            if (c == '[') {
                at++;
                *pOut = Value::array();
                skip();
                if (at < text.size() && text[at] == ']') {
                    at++;
                    return true;
                }
                while (true) {
                    Value item;
                    if (!value(&item, depth + 1)) return false;
                    pOut->push(std::move(item));
                    skip();
                    if (at < text.size() && text[at] == ',') {
                        at++;
                        continue;
                    }
                    if (at < text.size() && text[at] == ']') {
                        at++;
                        return true;
                    }
                    return fail("expected ',' or ']'");
                }
            }
            if (c == '"') {
                std::string s;
                if (!string(&s)) return false;
                *pOut = Value(std::move(s));
                return true;
            }
            if (c == 't') {
                *pOut = Value(true);
                return literal("true");
            }
            if (c == 'f') {
                *pOut = Value(false);
                return literal("false");
            }
            if (c == 'n') {
                *pOut = Value();
                return literal("null");
            }
            char* pEnd = nullptr;
            const double number = std::strtod(text.c_str() + at, &pEnd);
            if (pEnd == text.c_str() + at) {
                return fail("unexpected character");
            }
            at = static_cast< size_t >(pEnd - text.c_str());
            *pOut = Value(number);
            return true;
        }
    };

    void escape(std::string& out, const std::string& s) {
        out += '"';
        for (unsigned char c : s) {
            switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out += buffer;
                } else {
                    out += static_cast< char >(c);
                }
            }
        }
        out += '"';
    }

    void emit(std::string& out, const Value& v, bool pretty, int indent) {
        const std::string pad = pretty ? "\n" + std::string((indent + 1) * 2, ' ') : "";
        const std::string close = pretty ? "\n" + std::string(indent * 2, ' ') : "";
        switch (v.type()) {
        case Value::Type::Null: out += "null"; break;
        case Value::Type::Bool: out += v.asBool() ? "true" : "false"; break;
        case Value::Type::Number: {
            const double n = v.asNumber();
            char buffer[32];
            if (std::floor(n) == n && std::fabs(n) < 1e15) {
                std::snprintf(buffer, sizeof(buffer), "%.0f", n);
            } else {
                std::snprintf(buffer, sizeof(buffer), "%.9g", n);
            }
            out += buffer;
            break;
        }
        case Value::Type::String: escape(out, v.asString()); break;
        case Value::Type::Array:
            out += '[';
            for (size_t i = 0; i < v.items().size(); i++) {
                out += i ? "," : "";
                out += pad;
                emit(out, v.items()[i], pretty, indent + 1);
            }
            out += v.items().empty() ? "" : close;
            out += ']';
            break;
        case Value::Type::Object:
            out += '{';
            for (size_t i = 0; i < v.members().size(); i++) {
                out += i ? "," : "";
                out += pad;
                escape(out, v.members()[i].first);
                out += pretty ? ": " : ":";
                emit(out, v.members()[i].second, pretty, indent + 1);
            }
            out += v.members().empty() ? "" : close;
            out += '}';
            break;
        }
    }
}  // namespace

const Value& Value::operator[](const std::string& key) const {
    for (const auto& [name, value] : mMembers) {
        if (name == key) {
            return value;
        }
    }
    return nullValue();
}

bool Value::has(const std::string& key) const {
    for (const auto& member : mMembers) {
        if (member.first == key) {
            return true;
        }
    }
    return false;
}

void Value::set(const std::string& key, Value v) {
    for (auto& member : mMembers) {
        if (member.first == key) {
            member.second = std::move(v);
            return;
        }
    }
    mMembers.emplace_back(key, std::move(v));
}

std::optional< Value > parse(const std::string& text, std::string* pError) {
    Parser parser{text};
    Value root;
    if (!parser.value(&root, 0)) {
        if (pError != nullptr) {
            *pError = parser.error;
        }
        return std::nullopt;
    }
    parser.skip();
    if (parser.at != text.size()) {
        if (pError != nullptr) {
            *pError = "trailing characters at byte " + std::to_string(parser.at);
        }
        return std::nullopt;
    }
    return root;
}

std::string write(const Value& value, bool pretty) {
    std::string out;
    emit(out, value, pretty, 0);
    return out;
}

}  // namespace PetariEditor::Json
