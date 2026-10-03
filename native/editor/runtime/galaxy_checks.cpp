// Checks for a mod galaxy (docs/dev/level-editor/new-galaxy-flow.md): what's left to make it
// playable. Keys start with "galaxy-", which the session never takes into its baseline, so a
// new galaxy's list opens with them ("Mission 1 has no Power Star").
#include "galaxy_checks.hpp"

#include <petari/editor/check_providers.hpp>
#include <petari/editor/formats/formats.hpp>
#include <petari/editor/terrain/brush.hpp>
#include <petari/editor/terrain/kcl_reader.hpp>
#include <petari/host_allocation.hpp>
#include <petari/platform/dvd.hpp>
#include <petari/platform/mod_galaxy.hpp>
#include <revolution/dvd.h>

#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>

namespace PetariEditor::Runtime {
namespace {
    namespace T = Terrain;
    namespace F = Formats;

    std::optional< F::Bytes > readDisc(const std::string& path) {
        const s32 entry = DVDConvertPathToEntrynum(path.c_str());
        if (entry < 0) {
            return std::nullopt;
        }
        std::ifstream in(PetariNative::Platform::DVD::hostPathForEntry(entry), std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        return F::Bytes(std::istreambuf_iterator< char >(in), {});
    }

    std::int32_t intCell(const ModelView& m, RowId row, const char* pColumn) {
        const Cell* pCell = m.cell(row, columnHash(pColumn));
        return pCell != nullptr ? pCell->i : -1;
    }

    // The planets the game places by name (PlanetMapDataTable), once.
    const std::set< std::string >& planets() {
        static std::set< std::string > sNames;
        static bool sDone = false;
        if (!sDone) {
            sDone = true;
            const auto raw = readDisc("/ObjectData/PlanetMapDataTable.arc");
            const auto archive = raw ? F::Archive::parse(*raw) : std::nullopt;
            const F::Bytes* pTable = archive ? archive->file("planetmapdatatable.bcsv") : nullptr;
            const auto table = pTable ? F::Table::parse(*pTable) : std::nullopt;
            const int column = table ? table->column(F::jmapHash("PlanetName")) : -1;
            for (int r = 0; column >= 0 && r < table->rowCount(); r++) {
                sNames.insert(table->get(r, column).s);
            }
        }
        return sNames;
    }

    // A planet's collision (ObjectData/<Planet>.arc's .kcl) in its own space, cached by name.
    const T::Mesh* planetMesh(const std::string& name) {
        static std::map< std::string, std::optional< T::Mesh > > sMeshes;
        auto it = sMeshes.find(name);
        if (it == sMeshes.end()) {
            std::string lower = name;
            for (char& c : lower) {
                c = static_cast< char >(std::tolower(static_cast< unsigned char >(c)));
            }
            const auto raw = readDisc("/ObjectData/" + name + ".arc");
            const auto archive = raw ? F::Archive::parse(*raw) : std::nullopt;
            const F::Bytes* pKcl = archive ? archive->file(lower + ".kcl") : nullptr;
            it = sMeshes.emplace(name, pKcl != nullptr ? T::readKcl(*pKcl) : std::nullopt).first;
        }
        return it->second ? &*it->second : nullptr;
    }

    // Row space -> world: scale, then the game's rotation (Rz Ry Rx, MR::makeMtxRotate), the
    // position, then the zone's matrix.
    T::Vec3 toWorld(const T::Vec3& local, const RowTransform& t, bool scaled, const Mtx34& zone) {
        const float d = 3.14159265f / 180.0f;
        const float sx = std::sin(t.dirDeg.x * d), cx = std::cos(t.dirDeg.x * d), sy = std::sin(t.dirDeg.y * d), cy = std::cos(t.dirDeg.y * d),
                    sz = std::sin(t.dirDeg.z * d), cz = std::cos(t.dirDeg.z * d);
        const T::Vec3 p = scaled ? T::Vec3{local.x * t.scale.x, local.y * t.scale.y, local.z * t.scale.z} : local;
        const float r[3][3] = {{cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx},
                               {sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx},
                               {-sy, cy * sx, cy * cx}};
        const T::Vec3 row{r[0][0] * p.x + r[0][1] * p.y + r[0][2] * p.z + t.pos.x, r[1][0] * p.x + r[1][1] * p.y + r[1][2] * p.z + t.pos.y,
                          r[2][0] * p.x + r[2][1] * p.y + r[2][2] * p.z + t.pos.z};
        return {zone.m[0][0] * row.x + zone.m[0][1] * row.y + zone.m[0][2] * row.z + zone.m[0][3],
                zone.m[1][0] * row.x + zone.m[1][1] * row.y + zone.m[1][2] * row.z + zone.m[1][3],
                zone.m[2][0] * row.x + zone.m[2][1] * row.y + zone.m[2][2] * row.z + zone.m[2][3]};
    }

    void galaxyChecks(const ModelView& m, std::vector< Check >& out) {
        if (m.galaxy() == nullptr || PetariNative::Platform::ModGalaxy::find(m.galaxy()) == nullptr) {
            return;  // the disc's galaxies are as shipped
        }
        PetariNative::HostAllocationScope host;
        const int missions = m.missionCount();
        std::vector< bool > starred(static_cast< std::size_t >(missions) + 1, false);
        RowId start;
        T::Vec3 startAt;
        T::Mesh ground;  // everything to land on, in world space
        for (int zone = 0; zone < m.zoneCount(); zone++) {
            const Mtx34 zoneMtx = m.zoneWorldMatrix(zone);
            for (int table = 0; table < m.tableCount(zone); table++) {
                const std::string path = m.tablePath(zone, table);
                const bool objects = path.size() > 8 && path.compare(path.size() - 8, 8, "/objinfo") == 0;
                const bool starts = path.size() > 10 && path.compare(path.size() - 10, 10, "/startinfo") == 0;
                if (!objects && !starts) {
                    continue;
                }
                m.forEachRow(zone, table, [&](RowId row) {
                    if (m.state(row) == RowState::Deleted) {
                        return;
                    }
                    const char* pName = m.objectName(row);
                    const std::string name = pName != nullptr ? pName : "";
                    const RowTransform t = m.transform(row);
                    if (starts) {
                        if (!start.valid() && name == "Mario" && intCell(m, row, "MarioNo") <= 0) {
                            start = row;
                            startAt = toWorld({0, 0, 0}, t, false, zoneMtx);
                        }
                        return;
                    }
                    if (name == "PowerStar") {
                        const std::int32_t star = intCell(m, row, "Obj_arg0");
                        if (star >= 1 && star <= missions && (m.rowMissions(row) & (1u << (star - 1))) != 0) {
                            starred[static_cast< std::size_t >(star)] = true;
                        }
                        return;
                    }
                    const T::Mesh* pLocal = nullptr;
                    T::Mesh brush;
                    bool scaled = true;
                    if (name == T::kBrushClass) {
                        brush = T::brushMesh(T::brushFromRow(intCell(m, row, "Obj_arg0"), intCell(m, row, "Obj_arg1"), intCell(m, row, "Obj_arg2"),
                                                             intCell(m, row, "Obj_arg3"), {t.scale.x, t.scale.y, t.scale.z}));
                        pLocal = &brush;
                        scaled = false;  // the brush's mesh is at its size already
                    } else if (planets().count(name) != 0) {
                        pLocal = planetMesh(name);
                    }
                    if (pLocal == nullptr) {
                        return;
                    }
                    const std::uint32_t base = static_cast< std::uint32_t >(ground.positions.size());
                    for (const T::Vec3& p : pLocal->positions) {
                        ground.positions.push_back(toWorld(p, t, scaled, zoneMtx));
                    }
                    for (std::uint32_t index : pLocal->indices) {
                        ground.indices.push_back(base + index);
                    }
                });
            }
        }
        for (int mission = 1; mission <= missions; mission++) {
            if (!starred[static_cast< std::size_t >(mission)]) {
                out.push_back(Check{"galaxy-no-star:" + std::to_string(mission), Severity::Warning,
                                    "Mission " + std::to_string(mission) + " has no Power Star (place one with star number " + std::to_string(mission) +
                                        " in a layer the mission uses)",
                                    {}, "", Op{}, ""});
            }
        }
        if (!start.valid()) {
            out.push_back(Check{"galaxy-no-start", Severity::Error, "The galaxy has no start for Mario", {}, "", Op{}, ""});
        } else if (!T::raycast(ground, startAt, {0.0f, -1.0f, 0.0f}, 5000.0f)) {
            out.push_back(Check{"galaxy-start-midair:" + std::to_string(start.v), Severity::Warning,
                                "Mario starts in mid-air: nothing to land on within 5,000 units below the start", {start}, "", Op{}, ""});
        }
    }
}  // namespace

void registerGalaxyChecks() {
    registerCheckProvider(&galaxyChecks);
}

}  // namespace PetariEditor::Runtime
