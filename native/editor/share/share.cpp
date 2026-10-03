// Sharing mods as zips, and installing them (petari/editor/share.hpp).
#include "petari/editor/share.hpp"

#include "petari/editor/json.hpp"

#include <zlib.h>

#include <algorithm>
#include <ctime>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace PetariEditor::Share {
namespace {
    namespace fs = std::filesystem;
    using Bytes = std::vector< std::uint8_t >;

    void put16(Bytes& out, std::uint32_t v) {
        out.push_back(static_cast< std::uint8_t >(v));
        out.push_back(static_cast< std::uint8_t >(v >> 8));
    }
    void put32(Bytes& out, std::uint32_t v) {
        put16(out, v & 0xFFFF);
        put16(out, v >> 16);
    }
    std::uint32_t get16(const Bytes& b, size_t at) {
        return at + 2 <= b.size() ? static_cast< std::uint32_t >(b[at] | b[at + 1] << 8) : 0;
    }
    std::uint32_t get32(const Bytes& b, size_t at) {
        return get16(b, at) | get16(b, at + 2) << 16;
    }

    std::optional< Bytes > deflateRaw(const Bytes& in) {
        z_stream z{};
        if (deflateInit2(&z, Z_BEST_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) return std::nullopt;
        Bytes out(deflateBound(&z, static_cast< uLong >(in.size())));
        z.next_in = const_cast< Bytef* >(in.data());
        z.avail_in = static_cast< uInt >(in.size());
        z.next_out = out.data();
        z.avail_out = static_cast< uInt >(out.size());
        const int result = deflate(&z, Z_FINISH);
        out.resize(z.total_out);
        deflateEnd(&z);
        if (result != Z_STREAM_END) return std::nullopt;
        return out;
    }

    std::optional< Bytes > inflateRaw(const std::uint8_t* pIn, size_t size, size_t expected) {
        z_stream z{};
        if (inflateInit2(&z, -15) != Z_OK) return std::nullopt;
        Bytes out(expected);
        z.next_in = const_cast< Bytef* >(pIn);
        z.avail_in = static_cast< uInt >(size);
        z.next_out = out.data();
        z.avail_out = static_cast< uInt >(out.size());
        const int result = inflate(&z, Z_FINISH);
        const bool ok = (result == Z_STREAM_END || (result == Z_BUF_ERROR && expected == 0)) && z.total_out == expected;
        inflateEnd(&z);
        if (!ok) return std::nullopt;
        return out;
    }

    std::optional< Bytes > readFile(const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return std::nullopt;
        return Bytes(std::istreambuf_iterator< char >(in), std::istreambuf_iterator< char >());
    }

    bool writeFile(const fs::path& path, const Bytes& bytes, std::string* pError) {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        const fs::path temp = path.string() + ".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast< const char* >(bytes.data()), static_cast< std::streamsize >(bytes.size()));
            if (!out) {
                *pError = "cannot write " + temp.string();
                return false;
            }
        }
        fs::rename(temp, path, ec);
        if (ec) {
            *pError = "cannot write " + path.string() + ": " + ec.message();
            fs::remove(temp, ec);
            return false;
        }
        return true;
    }

    bool validFolderName(const std::string& name) {
        if (name.empty() || name.size() > 64 || name[0] == '.') return false;
        return std::all_of(name.begin(), name.end(), [](char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '-' || c == '.';
        });
    }

    bool clutter(const std::string& name) {
        return name == ".DS_Store" || name.rfind("._", 0) == 0 || (name.size() > 4 && name.compare(name.size() - 4, 4, ".tmp") == 0);
    }

    // mod.txt: key=value lines, kept in order (unknown keys and comments too).
    std::vector< std::pair< std::string, std::string > > parseModText(const std::string& text) {
        std::vector< std::pair< std::string, std::string > > out;
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const size_t eq = line.find('=');
            if (eq == std::string::npos || line[0] == '#') {
                out.emplace_back("", line);
                continue;
            }
            auto trim = [](std::string s) {
                const size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
                return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
            };
            out.emplace_back(trim(line.substr(0, eq)), trim(line.substr(eq + 1)));
        }
        return out;
    }

    std::vector< std::string > splitList(const std::string& text) {
        std::vector< std::string > out;
        std::istringstream in(text);
        std::string item;
        while (std::getline(in, item, ',')) {
            const size_t a = item.find_first_not_of(" \t"), b = item.find_last_not_of(" \t");
            if (a != std::string::npos) out.push_back(item.substr(a, b - a + 1));
        }
        return out;
    }

    // A preview from a mod's files, by path below its folder ("mod.txt", "edits/X.json", ...).
    Preview describe(const std::string& folder, const std::map< std::string, const Bytes* >& files) {
        Preview p;
        p.folder = folder;
        p.title = folder;
        std::set< std::string > galaxies, archives;
        for (const auto& [path, pBytes] : files) {
            const std::string text(pBytes->begin(), pBytes->end());
            if (path == "mod.txt") {
                for (const auto& [key, value] : parseModText(text)) {
                    if (key == "name" && !value.empty()) p.title = value;
                    if (key == "author") p.author = value;
                    if (key == "version") p.version = value;
                    if (key == "description") p.description = value;
                    if (key == "needs") p.needs = splitList(value);
                    if (key == "editor") p.editorMod = value == "1";
                }
            } else if (path == "preview.png") {
                p.previewPng = *pBytes;
            } else if (path.rfind("edits/", 0) == 0 && path.size() > 11 && path.compare(path.size() - 5, 5, ".json") == 0) {
                galaxies.insert(path.substr(6, path.size() - 11));
                if (std::optional< Json::Value > doc = Json::parse(text)) {
                    for (const auto& member : (*doc)["archives"].members()) archives.insert(member.first);
                }
            } else if (path == "galaxy.json") {
                if (std::optional< Json::Value > doc = Json::parse(text)) {
                    for (const Json::Value& g : (*doc)["galaxies"].items()) {
                        if (!g["name"].asString().empty()) {
                            galaxies.insert(g["name"].asString());
                            p.newGalaxies.push_back(g["name"].asString());
                        }
                    }
                }
            } else if (path.rfind("files/", 0) == 0) {
                p.fullArchives = true;
                archives.insert(path.substr(6));
                p.fileCount++;
            }
        }
        p.galaxies.assign(galaxies.begin(), galaxies.end());
    // A new galaxy's own archives: StageData/<G>.arc and StageData/<G>/...
    p.newGalaxyFilesOnly = p.fullArchives && !p.newGalaxies.empty();
    for (const std::string& archive : archives) {
        if (!p.fullArchives || archive.rfind("StageData/", 0) != 0) {
            continue;
        }
        bool own = false;
        for (const std::string& g : p.newGalaxies) {
            own = own || archive == "StageData/" + g + ".arc" || archive.rfind("StageData/" + g + "/", 0) == 0;
        }
        p.newGalaxyFilesOnly = p.newGalaxyFilesOnly && own;
    }
        p.archives.assign(archives.begin(), archives.end());
        return p;
    }

    std::vector< std::pair< std::string, fs::path > > folderFiles(const fs::path& root) {
        std::vector< std::pair< std::string, fs::path > > out;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec) || clutter(it->path().filename().string())) continue;
            out.emplace_back(it->path().lexically_relative(root).generic_string(), it->path());
        }
        std::sort(out.begin(), out.end());
        return out;
    }
}  // namespace

Bytes writeZip(const std::vector< ZipEntry >& entries) {
    Bytes out, central;
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    const std::uint32_t dosTime = static_cast< std::uint32_t >(local.tm_hour << 11 | local.tm_min << 5 | local.tm_sec / 2);
    const std::uint32_t dosDate = static_cast< std::uint32_t >((local.tm_year - 80) << 9 | (local.tm_mon + 1) << 5 | local.tm_mday);
    for (const ZipEntry& e : entries) {
        const std::uint32_t crc = static_cast< std::uint32_t >(crc32(0, e.data.data(), static_cast< uInt >(e.data.size())));
        std::optional< Bytes > packed = deflateRaw(e.data);
        const bool deflated = packed && packed->size() < e.data.size();
        const Bytes& body = deflated ? *packed : e.data;
        const std::uint32_t offset = static_cast< std::uint32_t >(out.size());
        auto header = [&](Bytes& b, bool isCentral) {
            put32(b, isCentral ? 0x02014b50 : 0x04034b50);
            if (isCentral) put16(b, 0x031E);  // made by: Unix, 3.0
            put16(b, 20);                     // needed: 2.0
            put16(b, 0x0800);                 // UTF-8 names
            put16(b, deflated ? 8 : 0);
            put16(b, dosTime);
            put16(b, dosDate);
            put32(b, crc);
            put32(b, static_cast< std::uint32_t >(body.size()));
            put32(b, static_cast< std::uint32_t >(e.data.size()));
            put16(b, static_cast< std::uint32_t >(e.path.size()));
            put16(b, 0);  // extra
            if (isCentral) {
                put16(b, 0);                // comment
                put16(b, 0);                // disk
                put16(b, 0);                // internal attributes
                put32(b, 0100644u << 16);   // external: a regular file, rw-r--r--
                put32(b, offset);
            }
            b.insert(b.end(), e.path.begin(), e.path.end());
        };
        header(out, false);
        out.insert(out.end(), body.begin(), body.end());
        header(central, true);
    }
    const std::uint32_t centralOffset = static_cast< std::uint32_t >(out.size());
    out.insert(out.end(), central.begin(), central.end());
    put32(out, 0x06054b50);
    put16(out, 0);
    put16(out, 0);
    put16(out, static_cast< std::uint32_t >(entries.size()));
    put16(out, static_cast< std::uint32_t >(entries.size()));
    put32(out, static_cast< std::uint32_t >(central.size()));
    put32(out, centralOffset);
    put16(out, 0);
    return out;
}

std::optional< std::vector< ZipEntry > > readZip(const Bytes& zip, std::string* pError, std::uint64_t limit) {
    auto fail = [&](const char* pWhy) -> std::optional< std::vector< ZipEntry > > {
        *pError = pWhy;
        return std::nullopt;
    };
    // The end record: the last signature within the comment's reach.
    size_t end = std::string::npos;
    for (size_t at = zip.size() >= 22 ? zip.size() - 22 : 0; zip.size() >= 22; at--) {
        if (get32(zip, at) == 0x06054b50) {
            end = at;
            break;
        }
        if (at == 0 || zip.size() - at > 0xFFFF + 22) break;
    }
    if (end == std::string::npos) return fail("not a zip file");
    const std::uint32_t count = get16(zip, end + 10), centralOffset = get32(zip, end + 16);
    std::vector< ZipEntry > out;
    std::uint64_t total = 0;
    size_t at = centralOffset;
    for (std::uint32_t i = 0; i < count; i++) {
        if (get32(zip, at) != 0x02014b50) return fail("a broken zip directory");
        const std::uint32_t flags = get16(zip, at + 8), method = get16(zip, at + 10), crc = get32(zip, at + 16);
        const std::uint32_t packedSize = get32(zip, at + 20), size = get32(zip, at + 24);
        const std::uint32_t nameLength = get16(zip, at + 28), extraLength = get16(zip, at + 30), commentLength = get16(zip, at + 32);
        const std::uint32_t local = get32(zip, at + 42);
        if (at + 46 + nameLength > zip.size()) return fail("a broken zip directory");
        std::string name(zip.begin() + static_cast< long >(at + 46), zip.begin() + static_cast< long >(at + 46 + nameLength));
        at += 46 + nameLength + extraLength + commentLength;
        if (!name.empty() && name.back() == '/') continue;  // a folder
        if (flags & 1) return fail("encrypted zips aren't supported");
        if (size == 0xFFFFFFFF || packedSize == 0xFFFFFFFF) return fail("zip64 files aren't supported");
        total += size;
        if (total > limit) return fail("the zip unpacks to more than 2 GB");
        if (get32(zip, local) != 0x04034b50) return fail("a broken zip entry");
        const size_t data = local + 30 + get16(zip, local + 26) + get16(zip, local + 28);
        if (data + packedSize > zip.size()) return fail("a zip entry runs past the end");
        ZipEntry entry;
        entry.path = std::move(name);
        if (method == 0) {
            if (packedSize != size) return fail("a broken stored zip entry");
            entry.data.assign(zip.begin() + static_cast< long >(data), zip.begin() + static_cast< long >(data + size));
        } else if (method == 8) {
            std::optional< Bytes > inflated = inflateRaw(zip.data() + data, packedSize, size);
            if (!inflated) return fail("a zip entry doesn't decompress");
            entry.data = std::move(*inflated);
        } else {
            return fail("a zip entry uses an unsupported compression method");
        }
        if (static_cast< std::uint32_t >(crc32(0, entry.data.data(), static_cast< uInt >(entry.data.size()))) != crc) {
            return fail("a zip entry is damaged (checksum)");
        }
        out.push_back(std::move(entry));
    }
    return out;
}

Preview describeFolder(const fs::path& modRoot) {
    std::vector< std::pair< std::string, Bytes > > loaded;
    for (const auto& [relative, path] : folderFiles(modRoot)) {
        // Only the small files are read; archives are listed by name.
        if (relative.rfind("files/", 0) == 0) {
            loaded.emplace_back(relative, Bytes());
        } else if (std::optional< Bytes > bytes = readFile(path)) {
            loaded.emplace_back(relative, std::move(*bytes));
        }
    }
    std::map< std::string, const Bytes* > files;
    for (const auto& [relative, bytes] : loaded) files[relative] = &bytes;
    return describe(modRoot.filename().string(), files);
}

bool exportZip(const fs::path& modRoot, const Info& info, const fs::path& zipPath, std::string* pError) {
    std::error_code ec;
    const std::string folder = modRoot.filename().string();
    if (!fs::is_directory(modRoot, ec) || !validFolderName(folder)) {
        *pError = "no mod folder " + modRoot.string();
        return false;
    }
    // mod.txt: the sheet's fields replace the old values; other lines stay.
    std::string text;
    if (std::optional< Bytes > old = readFile(modRoot / "mod.txt")) text.assign(old->begin(), old->end());
    std::vector< std::pair< std::string, std::string > > lines = parseModText(text);
    auto set = [&](const std::string& key, std::string value) {
        for (char& c : value) c = (c == '\n' || c == '\r') ? ' ' : c;
        for (auto& line : lines) {
            if (line.first == key) {
                line.second = value;
                return;
            }
        }
        lines.emplace_back(key, value);
    };
    if (!info.title.empty()) set("name", info.title);
    if (!info.author.empty()) set("author", info.author);
    if (!info.version.empty()) set("version", info.version);
    if (!info.description.empty()) set("description", info.description);
    std::string needs;
    for (const std::string& n : info.needs) needs += (needs.empty() ? "" : ",") + n;
    if (!info.needs.empty()) set("needs", needs);
    std::string written;
    for (const auto& [key, value] : lines) written += (key.empty() ? value : key + "=" + value) + "\n";
    if (!writeFile(modRoot / "mod.txt", Bytes(written.begin(), written.end()), pError)) return false;
    if (!info.previewPng.empty() && !writeFile(modRoot / "preview.png", info.previewPng, pError)) return false;

    std::vector< ZipEntry > entries;
    for (const auto& [relative, path] : folderFiles(modRoot)) {
        std::optional< Bytes > bytes = readFile(path);
        if (!bytes) {
            *pError = "cannot read " + path.string();
            return false;
        }
        entries.push_back({folder + "/" + relative, std::move(*bytes)});
    }
    return writeFile(zipPath, writeZip(entries), pError);
}

std::optional< Preview > inspectZip(const fs::path& zipPath, std::string* pError) {
    std::optional< Bytes > raw = readFile(zipPath);
    if (!raw) {
        *pError = "cannot read " + zipPath.string();
        return std::nullopt;
    }
    std::optional< std::vector< ZipEntry > > entries = readZip(*raw, pError);
    if (!entries) return std::nullopt;
    std::string folder;
    std::map< std::string, const Bytes* > files;
    for (const ZipEntry& e : *entries) {
        const size_t slash = e.path.find('/');
        const std::string top = slash == std::string::npos ? "" : e.path.substr(0, slash);
        if (top.empty() || (!folder.empty() && top != folder)) {
            *pError = "the zip isn't one mod folder";
            return std::nullopt;
        }
        folder = top;
        files[e.path.substr(slash + 1)] = &e.data;
    }
    if (folder.empty() || !validFolderName(folder)) {
        *pError = "the zip's folder name can't be a mod's";
        return std::nullopt;
    }
    if (!files.count("mod.txt") && !std::any_of(files.begin(), files.end(), [](const auto& f) {
            return f.first.rfind("files/", 0) == 0 || f.first.rfind("edits/", 0) == 0;
        })) {
        *pError = "the zip has no mod in it (no mod.txt, files/ or edits/)";
        return std::nullopt;
    }
    return describe(folder, files);
}

bool installZip(const fs::path& zipPath, const fs::path& modsDirectory, bool replace, std::string* pFolder, std::string* pError) {
    std::optional< Preview > preview = inspectZip(zipPath, pError);
    if (!preview) return false;
    std::optional< Bytes > raw = readFile(zipPath);
    std::optional< std::vector< ZipEntry > > entries = raw ? readZip(*raw, pError) : std::nullopt;
    if (!entries) return false;
    const fs::path target = modsDirectory / preview->folder;
    std::error_code ec;
    if (fs::exists(target, ec)) {
        if (!replace) {
            *pError = "mods/" + preview->folder + " is already installed";
            return false;
        }
        if (!fs::exists(target / "mod.txt", ec) && !fs::exists(target / "files", ec) && !fs::exists(target / "edits", ec)) {
            *pError = "mods/" + preview->folder + " exists and isn't a mod";
            return false;
        }
    }
    // Unpacked beside the target, then swapped in, so a failure leaves the old mod as it was.
    const fs::path staging = modsDirectory / ("." + preview->folder + ".installing");
    fs::remove_all(staging, ec);
    for (const ZipEntry& e : *entries) {
        const fs::path relative = fs::path(e.path.substr(e.path.find('/') + 1)).lexically_normal();
        const std::string text = relative.generic_string();
        if (relative.is_absolute() || text.empty() || text.rfind("..", 0) == 0 || text.find('\\') != std::string::npos) {
            fs::remove_all(staging, ec);
            *pError = "the zip has a path outside its folder: " + e.path;
            return false;
        }
        if (!writeFile(staging / relative, e.data, pError)) {
            fs::remove_all(staging, ec);
            return false;
        }
    }
    fs::remove_all(target, ec);
    fs::rename(staging, target, ec);
    if (ec) {
        *pError = "cannot install into " + target.string() + ": " + ec.message();
        fs::remove_all(staging, ec);
        return false;
    }
    *pFolder = preview->folder;
    return true;
}

}  // namespace PetariEditor::Share
