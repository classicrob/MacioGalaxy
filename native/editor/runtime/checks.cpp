// The Checks engine (TECH.md 5.5; edit_api.hpp Check): problems in the EditModel that would crash
// or break the game, or are probably unintended. Pure model + catalog reads; Runtime recomputes it
// when the model's revision changes.
#include "checks.hpp"

#include "edit_model.hpp"
#include <petari/editor/catalog.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>

namespace PetariEditor {
namespace {
    std::string endOf(const std::string& path) {
        return path.substr(path.rfind('/') + 1);
    }

    bool isPlacementTable(const std::string& path) {
        const std::string leaf = endOf(path);
        const bool placed = path.rfind("jmp/placement", 0) == 0 || path.rfind("jmp/mapparts", 0) == 0 || path.rfind("jmp/childobj", 0) == 0;
        return placed && leaf != "stageobjinfo";
    }

    std::string label(const ModelView& m, RowId row) {
        const char* pName = m.objectName(row);
        const Catalog::ClassInfo* pInfo = pName != nullptr ? Catalog::find(pName) : nullptr;
        return pInfo != nullptr && !pInfo->displayName.empty() ? pInfo->displayName : (pName != nullptr ? pName : "an object");
    }

    int intCell(const ModelView& m, RowId row, const char* pColumn) {
        const Cell* pCell = m.cell(row, columnHash(pColumn));
        return pCell != nullptr ? pCell->i : -1;
    }

    Op setOp(const std::string& name, RowId row, const char* pColumn, int value) {
        Op op;
        op.label = name;
        op.sets.push_back(SetCell{row, columnHash(pColumn), Cell{CellType::Int, value, 0, {}}});
        return op;
    }

    const char* const kSwitchFields[] = {"SW_APPEAR", "SW_DEAD", "SW_A", "SW_B", "SW_SLEEP", "SW_PARAM", "SW_AWAKE"};

    std::vector< CheckProvider >& providers() {
        static std::vector< CheckProvider > sProviders;
        return sProviders;
    }

    constexpr float kDegToRad = 3.14159265f / 180.0f;

    // The direction a zone's path (l_id) starts in: its first point to its second (pnt0), in zone space.
    bool railStartDirection(const ModelView& m, int zone, int lid, float* pOut) {
        for (int t = 0; t < m.tableCount(zone); t++) {
            if (endOf(m.tablePath(zone, t)) != "commonpathinfo") continue;
            RowId rail;
            m.forEachRow(zone, t, [&](RowId row) {
                if (!rail.valid() && m.state(row) != RowState::Deleted && intCell(m, row, "l_id") == lid) rail = row;
            });
            if (!rail.valid()) return false;
            const std::vector< RowId > points = m.railPoints(rail);
            if (points.size() < 2) return false;
            auto at = [&](RowId p, const char* pAxis) {
                const Cell* pCell = m.cell(p, columnHash(pAxis));
                return pCell != nullptr ? pCell->f : 0.0f;
            };
            const float d[3] = {at(points[1], "pnt0_x") - at(points[0], "pnt0_x"), at(points[1], "pnt0_y") - at(points[0], "pnt0_y"),
                                at(points[1], "pnt0_z") - at(points[0], "pnt0_z")};
            const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (length < 0.001f) return false;
            for (int k = 0; k < 3; k++) pOut[k] = d[k] / length;
            return true;
        }
        return false;
    }

    struct SwitchUse {
        std::vector< RowId > readers, writers;
    };
}  // namespace

void registerCheckProvider(CheckProvider provider) {
    if (std::find(providers().begin(), providers().end(), provider) == providers().end()) providers().push_back(provider);
}

std::vector< Check > computeChecks(const ModelView& m) {
    std::vector< Check > out;
    // Switches by (zone, id): zone-local ids are per zone, ids 1000+ are shared by the galaxy (-1).
    std::map< std::pair< int, int >, SwitchUse > switches;

    for (int zone = 0; zone < m.zoneCount(); zone++) {
        // The zone's rails: CommonPathInfo l_ids, and each rail's point count.
        std::map< int, int > railPoints;  // l_id -> points (-1: unknown)
        int railTable = -1;
        std::map< int, int > pointTables;  // CommonPathPointInfo.<n> -> rows
        for (int t = 0; t < m.tableCount(zone); t++) {
            const std::string path = m.tablePath(zone, t);
            if (endOf(path) == "commonpathinfo") railTable = t;
            if (endOf(path).rfind("commonpathpointinfo.", 0) == 0) {
                int rows = 0;
                m.forEachRow(zone, t, [&](RowId row) { rows += m.state(row) != RowState::Deleted; });
                pointTables[std::atoi(endOf(path).c_str() + 20)] = rows;
            }
        }
        if (railTable >= 0) {
            int index = 0;
            m.forEachRow(zone, railTable, [&](RowId row) {
                if (m.state(row) == RowState::Deleted) return;
                const int lid = intCell(m, row, "l_id");
                auto it = pointTables.find(index++);
                railPoints[lid] = it != pointTables.end() ? it->second : -1;
                if (it != pointTables.end() && it->second < 2) {
                    out.push_back(Check{"rail-short:" + std::to_string(row.v), Severity::Error,
                                        "Path " + std::to_string(lid) + " has " + std::to_string(it->second) + " point" +
                                            (it->second == 1 ? "" : "s") + "; a path needs at least 2",
                                        {row}, "", Op{}, ""});
                }
            });
        }

        for (int t = 0; t < m.tableCount(zone); t++) {
            const std::string path = m.tablePath(zone, t);
            if (!isPlacementTable(path)) continue;
            std::map< int, std::vector< RowId > > lids;
            m.forEachRow(zone, t, [&](RowId row) {
                if (m.state(row) == RowState::Deleted || m.rowMissions(row) == 0) return;
                const char* pName = m.objectName(row);
                const std::string name = pName != nullptr ? pName : "";
                const Catalog::ClassInfo* pInfo = Catalog::find(name.c_str());
                const std::string who = label(m, row);
                const std::string key = std::to_string(row.v);
                if (pInfo == nullptr && !name.empty()) {
                    out.push_back(Check{"unknown-class:" + key, Severity::Warning,
                                        "\"" + name + "\" is not an object the game knows; it will not appear", {row}, "", Op{}, ""});
                }
                if (const int lid = intCell(m, row, "l_id"); lid >= 0) lids[lid].push_back(row);

                // Collision can't stretch (CollisionParts::makeEqualScale): a solid object scaled
                // unevenly draws stretched but collides with one scale on every axis.
                // Generated parts bake scale into their mesh, a planet at scale 1.
                if (pInfo != nullptr && pInfo->hasCollision && !pInfo->bakesScale) {
                    const RowTransform t = m.transform(row);
                    const float x = t.scale.x;
                    if (std::fabs(t.scale.y - x) > 0.001f || std::fabs(t.scale.z - x) > 0.001f) {
                        Op fix;
                        fix.label = "Make the scale even";
                        fix.sets.push_back(SetCell{row, columnHash("scale_y"), Cell{CellType::Float, 0, x, {}}});
                        fix.sets.push_back(SetCell{row, columnHash("scale_z"), Cell{CellType::Float, 0, x, {}}});
                        char text[160];
                        std::snprintf(text, sizeof(text), "%s is scaled unevenly (%.2g, %.2g, %.2g): it draws stretched but collides as if %.2g on every axis",
                                      who.c_str(), x, t.scale.y, t.scale.z, x);
                        out.push_back(Check{"uneven-collision:" + key, Severity::Warning, text, {row}, fix.label, fix, ""});
                    }
                }

                // Links the class needs, and links pointing nowhere.
                const int rail = intCell(m, row, "CommonPath_ID");
                bool railRequired = false;
                if (pInfo != nullptr) {
                    for (const Catalog::Link& link : pInfo->links) {
                        if (!link.required) continue;
                        if (link.kind == Catalog::LinkKind::Rail) railRequired = true;
                        if (intCell(m, row, link.field.c_str()) < 0) {
                            const bool isRail = link.kind == Catalog::LinkKind::Rail;
                            out.push_back(Check{"missing-link:" + link.field + ":" + key, Severity::Error,
                                                who + (isRail ? " needs a path" : " needs a " + link.field + " link"), {row}, "",
                                                Op{}, isRail ? "pick-path" : "pick-" + link.field});
                        }
                    }
                }
                if (rail >= 0 && railPoints.find(rail) == railPoints.end()) {
                    Check check{"dangling-path:" + key, Severity::Error,
                                who + "'s path " + std::to_string(rail) + " doesn't exist in this zone", {row}, "", Op{}, "pick-path"};
                    if (!railRequired) {
                        check.fixLabel = "Unlink the path";
                        check.fix = setOp("Unlink the path", row, "CommonPath_ID", -1);
                    }
                    out.push_back(std::move(check));
                }

                // A Launch or Sling Star flies along its path from where it's placed, but draws facing
                // its own rotation: placed facing elsewhere, it looks wrong (the star points away).
                if ((name.rfind("SuperSpinDriver", 0) == 0 || name == "SpinDriver") && rail >= 0) {
                    float tangent[3] = {0, 0, 0};
                    if (railStartDirection(m, zone, rail, tangent)) {
                        const RowTransform tr = m.transform(row);
                        const float a = tr.dirDeg.x * kDegToRad, b = tr.dirDeg.y * kDegToRad, c = tr.dirDeg.z * kDegToRad;
                        const float front[3] = {std::cos(a) * std::sin(b) * std::cos(c) + std::sin(a) * std::sin(c),
                                                std::cos(a) * std::sin(b) * std::sin(c) - std::sin(a) * std::cos(c), std::cos(a) * std::cos(b)};
                        const float dot = front[0] * tangent[0] + front[1] * tangent[1] + front[2] * tangent[2];
                        if (dot < 0.7071f) {  // more than 45 degrees off
                            Op fix;
                            fix.label = "Face along the path";
                            const float pitch = -std::asin(std::max(-1.0f, std::min(1.0f, tangent[1]))) / kDegToRad;
                            const float yaw = std::atan2(tangent[0], tangent[2]) / kDegToRad;
                            fix.sets.push_back(SetCell{row, columnHash("dir_x"), Cell{CellType::Float, 0, pitch, {}}});
                            fix.sets.push_back(SetCell{row, columnHash("dir_y"), Cell{CellType::Float, 0, yaw, {}}});
                            fix.sets.push_back(SetCell{row, columnHash("dir_z"), Cell{CellType::Float, 0, 0.0f, {}}});
                            out.push_back(Check{"launch-star-facing:" + key, Severity::Warning,
                                                who + " faces away from where its path starts (" +
                                                    std::to_string(static_cast< int >(std::acos(std::max(-1.0f, dot)) / kDegToRad)) + " degrees)",
                                                {row}, fix.label, fix, ""});
                        }
                    }
                }

                // Switches: ranges, and who reads and writes each.
                for (const char* pField : kSwitchFields) {
                    const int id = intCell(m, row, pField);
                    if (id < 0) continue;
                    if ((id >= 128 && id < 1000) || id >= 1128) {
                        out.push_back(Check{"switch-range:" + std::string(pField) + ":" + key, Severity::Error,
                                            who + "'s " + pField + " " + std::to_string(id) +
                                                " is out of range (0-127 in the zone, 1000-1127 shared)",
                                            {row}, "Clear the switch", setOp("Clear the switch", row, pField, -1), ""});
                        continue;
                    }
                    bool write = std::string(pField) == "SW_DEAD";
                    bool known = false;
                    if (pInfo != nullptr) {
                        for (const Catalog::Switch& sw : pInfo->switches) {
                            if (sw.field == pField) {
                                write = sw.role == Catalog::SwitchRole::Write;
                                known = true;
                            }
                        }
                    }
                    if (!known && pInfo != nullptr && !pInfo->switches.empty()) {
                        out.push_back(Check{"switch-unused:" + std::string(pField) + ":" + key, Severity::Warning,
                                            who + " doesn't use " + pField + " (set to " + std::to_string(id) + ")", {row},
                                            "Clear it", setOp("Clear the switch", row, pField, -1), ""});
                        continue;
                    }
                    SwitchUse& use = switches[{id >= 1000 ? -1 : zone, id}];
                    (write ? use.writers : use.readers).push_back(row);
                }
            });
            for (const auto& [lid, rows] : lids) {
                if (rows.size() < 2) continue;
                for (size_t i = 1; i < rows.size(); i++) {
                    out.push_back(Check{"duplicate-lid:" + std::to_string(rows[i].v), Severity::Warning,
                                        "Two objects in " + endOf(path) + " share link id " + std::to_string(lid), {rows[i], rows[0]},
                                        "", Op{}, ""});
                }
            }
        }
    }

    // The galaxy as a whole (the save and the game's screens have fixed room).
    const int missions = m.missionCount();
    if (missions > 8) {
        out.push_back(Check{"missions-over-8", Severity::Error,
                            std::to_string(missions) + " missions: the save keeps stars and coin records for 8 per galaxy", {}, "", Op{}, ""});
    } else if (missions > 7) {
        out.push_back(Check{"missions-over-7", Severity::Warning,
                            std::to_string(missions) + " missions: the mission select and pause screens show 7", {}, "", Op{}, ""});
    }
    {
        const std::string name = m.galaxy();
        bool valid = !name.empty() && name.size() <= 40;
        for (char c : name) {
            valid = valid && ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_');
        }
        if (!valid) {
            out.push_back(Check{"galaxy-name", Severity::Error,
                                "The galaxy's name must be 1-40 letters, digits or '_' (it names its files)", {}, "", Op{}, ""});
        }
    }
    // Launch Stars keep their flight paths in a store of 16 per mission (SpinDriverPathStorage).
    for (int mission = 1; mission <= missions && mission <= 32; mission++) {
        std::vector< RowId > launchers;
        for (int zone = 0; zone < m.zoneCount(); zone++) {
            for (int t = 0; t < m.tableCount(zone); t++) {
                if (!isPlacementTable(m.tablePath(zone, t))) continue;
                m.forEachRow(zone, t, [&](RowId row) {
                    const char* pName = m.objectName(row);
                    if (pName != nullptr && std::string(pName).rfind("SuperSpinDriver", 0) == 0 && m.state(row) != RowState::Deleted &&
                        (m.rowMissions(row) & (1u << (mission - 1)))) {
                        launchers.push_back(row);
                    }
                });
            }
        }
        if (launchers.size() > 16) {
            out.push_back(Check{"launch-stars:" + std::to_string(mission), Severity::Error,
                                "Mission " + std::to_string(mission) + " has " + std::to_string(launchers.size()) +
                                    " Launch Stars; the game keeps paths for 16",
                                launchers, "", Op{}, ""});
        }
    }
    // Power Stars sharing a Star ID (Obj_arg0) in one mission: collecting one counts as the other.
    for (int mission = 1; mission <= missions && mission <= 32; mission++) {
        std::map< int, std::vector< RowId > > stars;
        for (int zone = 0; zone < m.zoneCount(); zone++) {
            for (int t = 0; t < m.tableCount(zone); t++) {
                if (!isPlacementTable(m.tablePath(zone, t))) continue;
                m.forEachRow(zone, t, [&](RowId row) {
                    const char* pName = m.objectName(row);
                    if (pName != nullptr && std::string(pName) == "PowerStar" && m.state(row) != RowState::Deleted &&
                        (m.rowMissions(row) & (1u << (mission - 1)))) {
                        stars[intCell(m, row, "Obj_arg0")].push_back(row);
                    }
                });
            }
        }
        for (const auto& [id, rows] : stars) {
            if (rows.size() < 2) continue;
            out.push_back(Check{"duplicate-star:" + std::to_string(mission) + ":" + std::to_string(id), Severity::Warning,
                                std::to_string(rows.size()) + " Power Stars in mission " + std::to_string(mission) + " share Star ID " +
                                    std::to_string(id) + "; collecting one counts as the others",
                                rows, "", Op{}, ""});
        }
    }
    for (CheckProvider provider : providers()) {
        provider(m, out);
    }

    // Errors first, then by key, so the panel's order is stable.
    std::stable_sort(out.begin(), out.end(), [](const Check& a, const Check& b) {
        if (a.severity != b.severity) return a.severity == Severity::Error;
        return a.key < b.key;
    });
    return out;
}

}  // namespace PetariEditor
