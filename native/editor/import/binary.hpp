#pragma once
#include <cstring>
#include <petari/editor/import.hpp>
#include <stdexcept>
namespace PetariEditor::Imports::Binary {
    inline void put16(Bytes& b, size_t at, unsigned v) {
        b[at] = v >> 8;
        b[at + 1] = v;
    }
    inline void put32(Bytes& b, size_t at, unsigned v) {
        b[at] = v >> 24;
        b[at + 1] = v >> 16;
        b[at + 2] = v >> 8;
        b[at + 3] = v;
    }
    inline void u32(Bytes& b, unsigned v) {
        auto n = b.size();
        b.resize(n + 4);
        put32(b, n, v);
    }
    inline void f32(Bytes& b, float f) {
        unsigned bits;
        std::memcpy(&bits, &f, 4);
        u32(b, bits);
    }
    inline void align(Bytes& b, size_t n) {
        while (b.size() % n) {
            b.push_back(0);
        }
    }
    inline Bytes names(const std::string& name) {
        Bytes b(8);
        put16(b, 0, 1);
        put16(b, 2, 65535);
        unsigned hash = 0;
        for (unsigned char c : name) {
            hash = hash * 3 + c;
        }
        put16(b, 4, hash);
        put16(b, 6, 8);
        b.insert(b.end(), name.begin(), name.end());
        b.push_back(0);
        return b;
    }
    struct Block {
        Bytes data;
        Block(const char* tag, size_t size) : data(size) {
            std::memcpy(data.data(), tag, 4);
        }
        size_t table(const Bytes& bytes, size_t field, size_t alignment = 4) {
            align(data, alignment);
            auto offset = data.size();
            put32(data, field, offset);
            data.insert(data.end(), bytes.begin(), bytes.end());
            return offset;
        }
        Bytes finish() {
            align(data, 32);
            put32(data, 4, data.size());
            return std::move(data);
        }
    };
}  // namespace PetariEditor::Imports::Binary
