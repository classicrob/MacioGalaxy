// Yaz0, RARC and BCSV for the level editor (petari/editor/formats/formats.hpp).
#include "petari/editor/formats/formats.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iconv.h>
#include <memory>
#include <unordered_map>

namespace PetariEditor::Formats {
namespace {
    constexpr std::uint32_t kYaz0Limit = 256u << 20;  // as collect_pipeline_seed_inputs.py

    std::uint32_t readU32(const Bytes& data, std::size_t at) {
        return static_cast< std::uint32_t >(data[at]) << 24 | static_cast< std::uint32_t >(data[at + 1]) << 16 |
               static_cast< std::uint32_t >(data[at + 2]) << 8 | data[at + 3];
    }
    std::uint16_t readU16(const Bytes& data, std::size_t at) {
        return static_cast< std::uint16_t >(data[at] << 8 | data[at + 1]);
    }
    void writeU32(Bytes& data, std::size_t at, std::uint32_t value) {
        data[at] = static_cast< std::uint8_t >(value >> 24);
        data[at + 1] = static_cast< std::uint8_t >(value >> 16);
        data[at + 2] = static_cast< std::uint8_t >(value >> 8);
        data[at + 3] = static_cast< std::uint8_t >(value);
    }
    void appendU32(Bytes& data, std::uint32_t value) {
        data.resize(data.size() + 4);
        writeU32(data, data.size() - 4, value);
    }
    std::string lower(std::string text) {
        for (char& c : text) {
            c = static_cast< char >(std::tolower(static_cast< unsigned char >(c)));
        }
        return text;
    }
    int widthOf(ColumnType type) {
        switch (type) {
        case ColumnType::Long:
        case ColumnType::Long2:
        case ColumnType::Float:
        case ColumnType::StringOffset:
            return 4;
        case ColumnType::Short:
            return 2;
        case ColumnType::Byte:
            return 1;
        case ColumnType::InlineString:
            return 32;
        case ColumnType::Null:
            return 0;
        }
        return 0;
    }
}  // namespace

// ---- Yaz0 ---------------------------------------------------------------------------------------

bool isYaz0(const Bytes& data) {
    return data.size() >= 16 && std::memcmp(data.data(), "Yaz0", 4) == 0;
}

std::optional< Bytes > yaz0Decode(const Bytes& data) {
    if (!isYaz0(data)) {
        return std::nullopt;
    }
    const std::uint32_t size = readU32(data, 4);
    if (size > kYaz0Limit) {
        return std::nullopt;
    }
    Bytes out;
    out.reserve(size);
    std::size_t at = 16;
    std::uint8_t code = 0;
    int bits = 0;
    while (out.size() < size) {
        if (bits == 0) {
            if (at >= data.size()) {
                return std::nullopt;
            }
            code = data[at++];
            bits = 8;
        }
        if (code & 0x80) {
            if (at >= data.size()) {
                return std::nullopt;
            }
            out.push_back(data[at++]);
        } else {
            if (at + 2 > data.size()) {
                return std::nullopt;
            }
            const std::uint8_t a = data[at], b = data[at + 1];
            at += 2;
            const std::size_t distance = ((a & 0x0F) << 8 | b) + 1;
            std::size_t length = a >> 4;
            if (length != 0) {
                length += 2;
            } else {
                if (at >= data.size()) {
                    return std::nullopt;
                }
                length = data[at++] + 0x12;
            }
            if (distance > out.size()) {
                return std::nullopt;
            }
            const std::size_t from = out.size() - distance;
            for (std::size_t i = 0; i < length && out.size() < size; i++) {
                out.push_back(out[from + i]);
            }
        }
        code <<= 1;
        bits--;
    }
    return out;
}

Bytes yaz0Encode(const Bytes& data) {
    Bytes out = {'Y', 'a', 'z', '0'};
    appendU32(out, static_cast< std::uint32_t >(data.size()));
    out.resize(16, 0);
    const std::size_t size = data.size();
    std::unordered_map< std::uint32_t, std::vector< std::size_t > > heads;  // 3-byte key -> positions, newest last
    auto key = [&](std::size_t at) { return static_cast< std::uint32_t >(data[at] << 16 | data[at + 1] << 8 | data[at + 2]); };
    auto remember = [&](std::size_t at) {
        if (at + 3 <= size) {
            std::vector< std::size_t >& chain = heads[key(at)];
            chain.push_back(at);
            if (chain.size() > 48) {
                chain.erase(chain.begin(), chain.begin() + 16);
            }
        }
    };
    std::size_t pos = 0;
    while (pos < size) {
        const std::size_t codeAt = out.size();
        out.push_back(0);
        std::uint8_t code = 0;
        for (int bit = 0; bit < 8 && pos < size; bit++) {
            std::size_t bestLength = 0, bestDistance = 0;
            if (pos + 3 <= size) {
                const std::size_t limit = std::min< std::size_t >(0x111, size - pos);
                auto found = heads.find(key(pos));
                if (found != heads.end()) {
                    const std::vector< std::size_t >& chain = found->second;
                    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                        const std::size_t distance = pos - *it;
                        if (distance > 0x1000) {
                            break;
                        }
                        std::size_t length = 3;
                        while (length < limit && data[*it + length] == data[pos + length]) {
                            length++;
                        }
                        if (length > bestLength) {
                            bestLength = length;
                            bestDistance = distance;
                            if (length == limit) {
                                break;
                            }
                        }
                    }
                }
            }
            if (bestLength >= 3) {
                const std::size_t d = bestDistance - 1;
                if (bestLength >= 0x12) {
                    out.push_back(static_cast< std::uint8_t >(d >> 8));
                    out.push_back(static_cast< std::uint8_t >(d & 0xFF));
                    out.push_back(static_cast< std::uint8_t >(bestLength - 0x12));
                } else {
                    out.push_back(static_cast< std::uint8_t >((bestLength - 2) << 4 | d >> 8));
                    out.push_back(static_cast< std::uint8_t >(d & 0xFF));
                }
                for (std::size_t at = pos; at < pos + bestLength; at++) {
                    remember(at);
                }
                pos += bestLength;
            } else {
                code |= static_cast< std::uint8_t >(0x80 >> bit);
                out.push_back(data[pos]);
                remember(pos);
                pos++;
            }
        }
        out[codeAt] = code;
    }
    return out;
}

// ---- RARC ---------------------------------------------------------------------------------------

std::optional< Archive > Archive::parse(const Bytes& raw) {
    Archive archive;
    archive.mCompressed = isYaz0(raw);
    if (archive.mCompressed) {
        std::optional< Bytes > plain = yaz0Decode(raw);
        if (!plain) {
            return std::nullopt;
        }
        archive.mData = std::move(*plain);
    } else {
        archive.mData = raw;
    }
    const Bytes& d = archive.mData;
    if (d.size() < 0x40 || std::memcmp(d.data(), "RARC", 4) != 0) {
        return std::nullopt;
    }
    const std::uint32_t header = readU32(d, 8);
    if (header != 0x20) {
        return std::nullopt;
    }
    archive.mPayload = header + readU32(d, 12);
    const std::uint32_t nodeCount = readU32(d, header);
    const std::uint32_t nodeOffset = header + readU32(d, header + 4);
    const std::uint32_t entryCount = readU32(d, header + 8);
    archive.mEntryOffset = header + readU32(d, header + 12);
    const std::uint32_t stringOffset = header + readU32(d, header + 20);
    if (nodeOffset + nodeCount * 16 > d.size() || archive.mEntryOffset + entryCount * 20 > d.size() || stringOffset > d.size() ||
        archive.mPayload > d.size()) {
        return std::nullopt;
    }
    auto name = [&](std::uint32_t offset) -> std::optional< std::string > {
        const std::size_t start = stringOffset + offset;
        if (start >= d.size()) {
            return std::nullopt;
        }
        const auto* end = static_cast< const std::uint8_t* >(std::memchr(d.data() + start, 0, d.size() - start));
        if (end == nullptr) {
            return std::nullopt;
        }
        return std::string(reinterpret_cast< const char* >(d.data() + start), end - (d.data() + start));
    };
    std::optional< std::string > root = name(readU32(d, nodeOffset + 4));
    if (!root) {
        return std::nullopt;
    }
    archive.mRoot = lower(*root);

    // Walk the folders from the root (node 0); guard against cycles.
    std::vector< bool > visited(nodeCount, false);
    bool ok = true;
    auto walk = [&](auto&& self, std::uint32_t node, const std::string& path) -> void {
        if (!ok || node >= nodeCount || visited[node]) {
            ok = false;
            return;
        }
        visited[node] = true;
        const std::size_t at = nodeOffset + node * 16;
        const std::uint16_t count = readU16(d, at + 10);
        const std::uint32_t first = readU32(d, at + 12);
        for (std::uint32_t i = first; i < first + count && ok; i++) {
            if (i >= entryCount) {
                ok = false;
                return;
            }
            const std::size_t entry = archive.mEntryOffset + i * 20;
            const std::uint8_t flags = d[entry + 4];
            std::optional< std::string > entryName = name(readU16(d, entry + 6));
            if (!entryName) {
                ok = false;
                return;
            }
            if (*entryName == "." || *entryName == "..") {
                continue;
            }
            const std::uint32_t value = readU32(d, entry + 8);
            const std::uint32_t size = readU32(d, entry + 12);
            if (flags & 2) {
                self(self, value, path + lower(*entryName) + "/");
            } else {
                if (archive.mPayload + static_cast< std::uint64_t >(value) + size > d.size()) {
                    ok = false;
                    return;
                }
                archive.mPathToEntry[path + lower(*entryName)] = i;
                archive.mContents[i] = Bytes(d.begin() + archive.mPayload + value, d.begin() + archive.mPayload + value + size);
            }
        }
    };
    walk(walk, 0, "");
    if (!ok) {
        return std::nullopt;
    }
    return archive;
}

std::vector< std::string > Archive::files() const {
    std::vector< std::pair< std::uint32_t, std::string > > byEntry;
    for (const auto& [path, entry] : mPathToEntry) {
        byEntry.emplace_back(entry, path);
    }
    std::sort(byEntry.begin(), byEntry.end());
    std::vector< std::string > out;
    for (auto& [entry, path] : byEntry) {
        out.push_back(path);
    }
    return out;
}

bool Archive::has(const std::string& path) const {
    return file(path) != nullptr;
}

const Bytes* Archive::file(const std::string& path) const {
    std::string key = lower(path);
    while (!key.empty() && key.front() == '/') {
        key.erase(key.begin());
    }
    auto it = mPathToEntry.find(key);
    if (it == mPathToEntry.end() && key.compare(0, mRoot.size() + 1, mRoot + "/") == 0) {
        it = mPathToEntry.find(key.substr(mRoot.size() + 1));
    }
    return it == mPathToEntry.end() ? nullptr : &mContents.at(it->second);
}

bool Archive::replace(const std::string& path, Bytes contents) {
    const Bytes* current = file(path);
    if (current == nullptr) {
        return false;
    }
    for (auto& [entry, bytes] : mContents) {
        if (&bytes == current) {
            bytes = std::move(contents);
            return true;
        }
    }
    return false;
}

Bytes Archive::build() const {
    // The original file order is the order of the data offsets (stage_edit.py Archive.build).
    std::vector< std::uint32_t > order;
    for (const auto& [entry, bytes] : mContents) {
        order.push_back(entry);
    }
    std::sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) {
        return readU32(mData, mEntryOffset + a * 20 + 8) < readU32(mData, mEntryOffset + b * 20 + 8);
    });
    Bytes out(mData.begin(), mData.begin() + mPayload);
    Bytes body;
    for (std::uint32_t entry : order) {
        const Bytes& contents = mContents.at(entry);
        writeU32(out, mEntryOffset + entry * 20 + 8, static_cast< std::uint32_t >(body.size()));
        writeU32(out, mEntryOffset + entry * 20 + 12, static_cast< std::uint32_t >(contents.size()));
        body.insert(body.end(), contents.begin(), contents.end());
        body.resize((body.size() + 31) & ~std::size_t(31), 0);
    }
    out.insert(out.end(), body.begin(), body.end());
    const std::uint32_t oldLength = readU32(mData, 16);
    const std::uint32_t mram = readU32(mData, 20), aram = readU32(mData, 24);
    const auto length = static_cast< std::uint32_t >(body.size());
    writeU32(out, 4, static_cast< std::uint32_t >(out.size()));
    writeU32(out, 16, length);
    // The preload sizes cover the whole data section on stage archives (MRAM, ARAM 0).
    writeU32(out, 20, mram == oldLength ? length : mram);
    writeU32(out, 24, aram == oldLength ? length : aram);
    return out;
}

Archive Archive::create(const std::string& root, const std::vector< std::pair< std::string, Bytes > >& files) {
    // A folder tree: name -> (files in order, sub-folders in order).
    struct Folder {
        std::string name;
        std::vector< std::pair< std::string, const Bytes* > > files;
        std::vector< std::unique_ptr< Folder > > folders;
        Folder* sub(const std::string& part) {
            for (auto& f : folders) {
                if (f->name == part) return f.get();
            }
            folders.push_back(std::make_unique< Folder >());
            folders.back()->name = part;
            return folders.back().get();
        }
    };
    Folder top;
    top.name = root;
    for (const auto& [path, bytes] : files) {
        Folder* folder = &top;
        size_t start = 0, slash;
        while ((slash = path.find('/', start)) != std::string::npos) {
            folder = folder->sub(path.substr(start, slash - start));
            start = slash + 1;
        }
        folder->files.emplace_back(path.substr(start), &bytes);
    }
    // Breadth-first node order (root first), as Nintendo's packer writes.
    std::vector< Folder* > nodes = {&top};
    for (size_t i = 0; i < nodes.size(); i++) {
        for (auto& f : nodes[i]->folders) nodes.push_back(f.get());
    }
    Bytes strings;
    std::map< std::string, std::uint32_t > stringAt;
    auto intern = [&](const std::string& text) {
        auto it = stringAt.find(text);
        if (it != stringAt.end()) return it->second;
        const auto at = static_cast< std::uint32_t >(strings.size());
        strings.insert(strings.end(), text.begin(), text.end());
        strings.push_back(0);
        stringAt[text] = at;
        return at;
    };
    intern(".");
    intern("..");
    auto nameHash = [](const std::string& text) {
        std::uint16_t h = 0;
        for (char c : text) h = static_cast< std::uint16_t >(h * 3 + static_cast< std::uint8_t >(c));
        return h;
    };
    struct Entry {
        std::uint16_t id, hash;
        std::uint8_t flags;
        std::uint32_t nameOffset, value, size;
        const Bytes* contents;
    };
    std::vector< Entry > entries;
    std::vector< std::uint32_t > firstEntry(nodes.size()), entryCount(nodes.size());
    std::uint16_t nextId = 0;
    std::uint32_t dataSize = 0;
    std::map< const Folder*, std::uint32_t > nodeIndex;
    for (size_t i = 0; i < nodes.size(); i++) nodeIndex[nodes[i]] = static_cast< std::uint32_t >(i);
    std::vector< std::uint32_t > parentOf(nodes.size(), 0xFFFFFFFF);
    for (size_t i = 0; i < nodes.size(); i++) {
        for (auto& f : nodes[i]->folders) parentOf[nodeIndex[f.get()]] = static_cast< std::uint32_t >(i);
    }
    for (size_t i = 0; i < nodes.size(); i++) {
        firstEntry[i] = static_cast< std::uint32_t >(entries.size());
        for (auto& [name, bytes] : nodes[i]->files) {
            entries.push_back({nextId++, nameHash(name), 0x11, intern(name), dataSize, static_cast< std::uint32_t >(bytes->size()), bytes});
            dataSize += (static_cast< std::uint32_t >(bytes->size()) + 31) & ~31u;
        }
        for (auto& f : nodes[i]->folders) {
            entries.push_back({0xFFFF, nameHash(f->name), 0x02, intern(f->name), nodeIndex[f.get()], 0x10, nullptr});
        }
        entries.push_back({0xFFFF, nameHash("."), 0x02, 0, static_cast< std::uint32_t >(i), 0x10, nullptr});
        entries.push_back({0xFFFF, nameHash(".."), 0x02, 2, parentOf[i], 0x10, nullptr});
        entryCount[i] = static_cast< std::uint32_t >(entries.size()) - firstEntry[i];
    }
    for (Folder* f : nodes) intern(f->name);
    while (strings.size() % 32) strings.push_back(0);

    const std::uint32_t infoSize = 0x20;
    const std::uint32_t nodeOffset = infoSize;
    const std::uint32_t entryOffset = (nodeOffset + static_cast< std::uint32_t >(nodes.size()) * 16 + 31) & ~31u;
    const std::uint32_t stringOffset = (entryOffset + static_cast< std::uint32_t >(entries.size()) * 20 + 31) & ~31u;
    const std::uint32_t dataOffset = stringOffset + static_cast< std::uint32_t >(strings.size());
    Bytes out(0x20 + dataOffset, 0);
    std::memcpy(out.data(), "RARC", 4);
    writeU32(out, 8, 0x20);
    writeU32(out, 12, dataOffset);
    writeU32(out, 16, dataSize);
    writeU32(out, 20, dataSize);  // MRAM preload: the whole data section, as stage archives
    writeU32(out, 0x20 + 0, static_cast< std::uint32_t >(nodes.size()));
    writeU32(out, 0x20 + 4, nodeOffset);
    writeU32(out, 0x20 + 8, static_cast< std::uint32_t >(entries.size()));
    writeU32(out, 0x20 + 12, entryOffset);
    writeU32(out, 0x20 + 16, static_cast< std::uint32_t >(strings.size()));
    writeU32(out, 0x20 + 20, stringOffset);
    out[0x20 + 24] = static_cast< std::uint8_t >(nextId >> 8);
    out[0x20 + 25] = static_cast< std::uint8_t >(nextId);
    out[0x20 + 26] = 1;  // ids are synchronised with entry order (files)
    for (size_t i = 0; i < nodes.size(); i++) {
        const size_t at = 0x20 + nodeOffset + i * 16;
        std::string type = i == 0 ? "ROOT" : nodes[i]->name;
        for (char& c : type) c = static_cast< char >(std::toupper(static_cast< unsigned char >(c)));
        type.resize(4, ' ');
        std::memcpy(out.data() + at, type.data(), 4);
        writeU32(out, at + 4, intern(nodes[i]->name));
        const std::uint16_t h = nameHash(nodes[i]->name);
        out[at + 8] = static_cast< std::uint8_t >(h >> 8);
        out[at + 9] = static_cast< std::uint8_t >(h);
        out[at + 10] = static_cast< std::uint8_t >(entryCount[i] >> 8);
        out[at + 11] = static_cast< std::uint8_t >(entryCount[i]);
        writeU32(out, at + 12, firstEntry[i]);
    }
    for (size_t i = 0; i < entries.size(); i++) {
        const size_t at = 0x20 + entryOffset + i * 20;
        const Entry& e = entries[i];
        out[at] = static_cast< std::uint8_t >(e.id >> 8);
        out[at + 1] = static_cast< std::uint8_t >(e.id);
        out[at + 2] = static_cast< std::uint8_t >(e.hash >> 8);
        out[at + 3] = static_cast< std::uint8_t >(e.hash);
        out[at + 4] = e.flags;
        out[at + 6] = static_cast< std::uint8_t >(e.nameOffset >> 8);
        out[at + 7] = static_cast< std::uint8_t >(e.nameOffset);
        writeU32(out, at + 8, e.value);
        writeU32(out, at + 12, e.size);
    }
    std::memcpy(out.data() + 0x20 + stringOffset, strings.data(), strings.size());
    for (const Entry& e : entries) {
        if (e.contents == nullptr) continue;
        Bytes padded = *e.contents;
        padded.resize((padded.size() + 31) & ~std::size_t(31), 0);
        out.insert(out.end(), padded.begin(), padded.end());
    }
    writeU32(out, 4, static_cast< std::uint32_t >(out.size()));
    return *parse(out);
}

// ---- BCSV ---------------------------------------------------------------------------------------

std::uint32_t jmapHash(const char* pName) {
    std::uint32_t hash = 0;
    for (; *pName != '\0'; pName++) {
        hash = static_cast< std::uint32_t >(static_cast< signed char >(*pName)) + hash * 31;
    }
    return hash;
}

std::optional< Table > Table::parse(const Bytes& data) {
    if (data.size() < 16) {
        return std::nullopt;
    }
    Table table;
    const std::uint32_t count = readU32(data, 0);
    const std::uint32_t fields = readU32(data, 4);
    const std::uint32_t dataOffset = readU32(data, 8);
    table.mEntrySize = readU32(data, 12);
    if (fields > 4096 || dataOffset < 16 + fields * 12 || dataOffset > data.size() ||
        static_cast< std::uint64_t >(count) * table.mEntrySize > data.size() - dataOffset) {
        return std::nullopt;
    }
    for (std::uint32_t i = 0; i < fields; i++) {
        const std::size_t at = 16 + i * 12;
        TableColumn column;
        column.hash = readU32(data, at);
        column.mask = readU32(data, at + 4);
        column.offset = readU16(data, at + 8);
        column.shift = data[at + 10];
        column.type = static_cast< ColumnType >(data[at + 11] <= 7 ? data[at + 11] : 7);
        if (column.offset + widthOf(column.type) > table.mEntrySize) {
            return std::nullopt;
        }
        table.mColumns.push_back(column);
    }
    table.mHeader.assign(data.begin(), data.begin() + dataOffset);
    for (std::uint32_t r = 0; r < count; r++) {
        const std::size_t at = dataOffset + static_cast< std::size_t >(r) * table.mEntrySize;
        table.mRows.emplace_back(data.begin() + at, data.begin() + at + table.mEntrySize);
    }
    // The string pool, then '@' padding to 32 bytes (kept unless it ends in the pool's NUL).
    Bytes strings(data.begin() + dataOffset + static_cast< std::size_t >(count) * table.mEntrySize, data.end());
    std::size_t end = strings.size();
    while (end > 0 && strings[end - 1] == '@') {
        end--;
    }
    if (end > 0 && strings[end - 1] == 0) {
        strings.resize(end);
    }
    table.mStrings = std::move(strings);
    return table;
}

int Table::column(std::uint32_t hash) const {
    for (std::size_t i = 0; i < mColumns.size(); i++) {
        if (mColumns[i].hash == hash) {
            return static_cast< int >(i);
        }
    }
    return -1;
}

CellValue Table::get(int row, int columnIndex) const {
    const TableColumn& c = mColumns[columnIndex];
    const Bytes& record = mRows[row];
    CellValue value;
    switch (c.type) {
    case ColumnType::Float: {
        value.kind = CellValue::Kind::Float;
        const std::uint32_t bits = readU32(record, c.offset);
        std::memcpy(&value.f, &bits, 4);
        return value;
    }
    case ColumnType::StringOffset: {
        value.kind = CellValue::Kind::String;
        const std::uint32_t at = readU32(record, c.offset);
        if (at < mStrings.size()) {
            const auto* start = mStrings.data() + at;
            const auto* nul = static_cast< const std::uint8_t* >(std::memchr(start, 0, mStrings.size() - at));
            value.s.assign(reinterpret_cast< const char* >(start), nul != nullptr ? nul - start : mStrings.size() - at);
        }
        return value;
    }
    case ColumnType::InlineString: {
        value.kind = CellValue::Kind::String;
        const auto* start = record.data() + c.offset;
        const auto* nul = static_cast< const std::uint8_t* >(std::memchr(start, 0, 32));
        value.s.assign(reinterpret_cast< const char* >(start), nul != nullptr ? nul - start : 32);
        return value;
    }
    case ColumnType::Null:
        return value;
    default: {
        const int width = widthOf(c.type);
        std::uint32_t raw = 0;
        for (int i = 0; i < width; i++) {
            raw = raw << 8 | record[c.offset + i];
        }
        std::uint32_t v = (raw & c.mask) >> c.shift;
        const int bits = width * 8;
        if (bits < 32 && (v & (1u << (bits - 1)))) {
            v |= ~((1u << bits) - 1);  // sign-extend by width
        }
        value.i = static_cast< std::int32_t >(v);
        return value;
    }
    }
}

std::uint32_t Table::poolString(const std::string& text) {
    // Reuse an exact, NUL-delimited match (as stage_edit.py), else append.
    Bytes needle(text.begin(), text.end());
    needle.push_back(0);
    auto it = mStrings.begin();
    while (true) {
        it = std::search(it, mStrings.end(), needle.begin(), needle.end());
        if (it == mStrings.end()) {
            break;
        }
        if (it == mStrings.begin() || *(it - 1) == 0) {
            return static_cast< std::uint32_t >(it - mStrings.begin());
        }
        ++it;
    }
    const auto at = static_cast< std::uint32_t >(mStrings.size());
    mStrings.insert(mStrings.end(), needle.begin(), needle.end());
    return at;
}

void Table::set(int row, int columnIndex, const CellValue& value) {
    const TableColumn& c = mColumns[columnIndex];
    Bytes& record = mRows[row];
    switch (c.type) {
    case ColumnType::Float: {
        float f = value.kind == CellValue::Kind::Float ? value.f : static_cast< float >(value.i);
        std::uint32_t bits;
        std::memcpy(&bits, &f, 4);
        writeU32(record, c.offset, bits);
        return;
    }
    case ColumnType::StringOffset:
        writeU32(record, c.offset, poolString(value.s));
        return;
    case ColumnType::InlineString:
        std::memset(record.data() + c.offset, 0, 32);
        std::memcpy(record.data() + c.offset, value.s.data(), std::min< std::size_t >(value.s.size(), 32));
        return;
    case ColumnType::Null:
        return;
    default: {
        const int width = widthOf(c.type);
        std::uint32_t raw = 0;
        for (int i = 0; i < width; i++) {
            raw = raw << 8 | record[c.offset + i];
        }
        const std::int32_t v = value.kind == CellValue::Kind::Float ? static_cast< std::int32_t >(value.f) : value.i;
        raw = (raw & ~c.mask) | ((static_cast< std::uint32_t >(v) << c.shift) & c.mask);
        for (int i = width - 1; i >= 0; i--) {
            record[c.offset + i] = static_cast< std::uint8_t >(raw);
            raw >>= 8;
        }
        return;
    }
    }
}

int Table::addColumn(std::uint32_t hash, ColumnType type, const CellValue& initial) {
    if (int existing = column(hash); existing >= 0) {
        return existing;
    }
    const int width = widthOf(type);
    const int align = width >= 4 ? 4 : width;
    std::uint32_t offset = mEntrySize;
    if (align > 1) {
        offset = (offset + align - 1) & ~std::uint32_t(align - 1);
    }
    TableColumn c;
    c.hash = hash;
    c.type = type;
    c.offset = static_cast< std::uint16_t >(offset);
    c.mask = type == ColumnType::Short ? 0xFFFF : type == ColumnType::Byte ? 0xFF : 0xFFFFFFFF;
    // Records stay 4-byte aligned, as the disc's are.
    const std::uint32_t entrySize = (offset + width + 3) & ~std::uint32_t(3);
    for (Bytes& record : mRows) {
        record.resize(entrySize, 0);
    }
    mEntrySize = entrySize;
    mColumns.push_back(c);
    mColumnsChanged = true;
    const int index = static_cast< int >(mColumns.size()) - 1;
    for (int r = 0; r < rowCount(); r++) {
        set(r, index, initial);
    }
    return index;
}

int Table::addRow(int copyOf) {
    if (copyOf >= 0) {
        mRows.push_back(mRows[copyOf]);
    } else {
        mRows.emplace_back(mEntrySize, 0);
        const int row = rowCount() - 1;
        for (int c = 0; c < static_cast< int >(mColumns.size()); c++) {
            if (mColumns[c].type == ColumnType::StringOffset) {
                set(row, c, CellValue{CellValue::Kind::String, 0, 0.0f, ""});
            }
        }
    }
    return rowCount() - 1;
}

void Table::removeRow(int row) {
    mRows.erase(mRows.begin() + row);
}

Bytes Table::buildSingleRow(int row) const {
    Table single = *this;
    single.mRows = {mRows[row]};
    return single.build();
}

Bytes Table::build() const {
    Bytes out;
    if (!mColumnsChanged) {
        out = mHeader;
    } else {
        out.resize(16 + mColumns.size() * 12, 0);
        writeU32(out, 4, static_cast< std::uint32_t >(mColumns.size()));
        writeU32(out, 8, static_cast< std::uint32_t >(out.size()));
        for (std::size_t i = 0; i < mColumns.size(); i++) {
            const TableColumn& c = mColumns[i];
            const std::size_t at = 16 + i * 12;
            writeU32(out, at, c.hash);
            writeU32(out, at + 4, c.mask);
            out[at + 8] = static_cast< std::uint8_t >(c.offset >> 8);
            out[at + 9] = static_cast< std::uint8_t >(c.offset);
            out[at + 10] = c.shift;
            out[at + 11] = static_cast< std::uint8_t >(c.type);
        }
    }
    writeU32(out, 0, static_cast< std::uint32_t >(mRows.size()));
    writeU32(out, 12, mEntrySize);
    for (const Bytes& record : mRows) {
        out.insert(out.end(), record.begin(), record.end());
    }
    out.insert(out.end(), mStrings.begin(), mStrings.end());
    out.resize((out.size() + 31) & ~std::size_t(31), '@');
    return out;
}

// ---- Shift-JIS ----------------------------------------------------------------------------------

namespace {
std::string convert(const std::string& text, const char* pFrom, const char* pTo) {
    bool ascii = std::all_of(text.begin(), text.end(), [](char c) { return static_cast< unsigned char >(c) < 0x80; });
    if (ascii) {
        return text;
    }
    iconv_t cd = iconv_open(pTo, pFrom);
    if (cd == reinterpret_cast< iconv_t >(-1)) {
        return text;
    }
    std::string out(text.size() * 4 + 4, '\0');
    char* pIn = const_cast< char* >(text.data());
    size_t inLeft = text.size();
    char* pOut = out.data();
    size_t outLeft = out.size();
    const size_t result = iconv(cd, &pIn, &inLeft, &pOut, &outLeft);
    iconv_close(cd);
    if (result == static_cast< size_t >(-1)) {
        return text;
    }
    out.resize(out.size() - outLeft);
    return out;
}
}  // namespace

std::string sjisToUtf8(const std::string& sjis) {
    return convert(sjis, "CP932", "UTF-8");
}

std::string utf8ToSjis(const std::string& utf8) {
    return convert(utf8, "UTF-8", "CP932");
}

}  // namespace PetariEditor::Formats
