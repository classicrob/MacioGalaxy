#pragma once
// A small JSON reader and writer for the level editor's own files (the catalog, patch mods,
// projects). UTF-8 in and out; numbers are doubles. Not a general-purpose library: no streaming,
// inputs are a few megabytes at most.
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace PetariEditor::Json {

class Value {
public:
    enum class Type : std::uint8_t { Null, Bool, Number, String, Array, Object };

    Value() = default;
    Value(bool b) : mType(Type::Bool), mBool(b) {}
    Value(double n) : mType(Type::Number), mNumber(n) {}
    Value(int n) : mType(Type::Number), mNumber(n) {}
    Value(std::string s) : mType(Type::String), mString(std::move(s)) {}
    Value(const char* s) : mType(Type::String), mString(s) {}
    static Value array() {
        Value v;
        v.mType = Type::Array;
        return v;
    }
    static Value object() {
        Value v;
        v.mType = Type::Object;
        return v;
    }

    Type type() const { return mType; }
    bool isNull() const { return mType == Type::Null; }
    bool isObject() const { return mType == Type::Object; }
    bool isArray() const { return mType == Type::Array; }

    bool asBool(bool fallback = false) const { return mType == Type::Bool ? mBool : fallback; }
    double asNumber(double fallback = 0) const { return mType == Type::Number ? mNumber : fallback; }
    int asInt(int fallback = 0) const { return mType == Type::Number ? static_cast< int >(mNumber) : fallback; }
    const std::string& asString() const { return mString; }

    // arrays
    const std::vector< Value >& items() const { return mItems; }
    void push(Value v) { mItems.push_back(std::move(v)); }

    // objects (keys keep insertion order for stable output)
    const Value& operator[](const std::string& key) const;
    bool has(const std::string& key) const;
    void set(const std::string& key, Value v);
    const std::vector< std::pair< std::string, Value > >& members() const { return mMembers; }

private:
    Type mType = Type::Null;
    bool mBool = false;
    double mNumber = 0;
    std::string mString;
    std::vector< Value > mItems;
    std::vector< std::pair< std::string, Value > > mMembers;
};

/// Parses UTF-8 JSON text. nullopt and *pError (with the byte offset) on a syntax error.
std::optional< Value > parse(const std::string& text, std::string* pError = nullptr);
/// Compact JSON; `pretty` puts each array item and object member on its own indented line.
std::string write(const Value& value, bool pretty = false);

}  // namespace PetariEditor::Json
