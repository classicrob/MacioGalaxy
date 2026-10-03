// Model operations (petari/editor/model_ops.hpp).
#include "petari/editor/model_ops.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <set>

namespace PetariEditor::ModelOps {
namespace {
    constexpr int kLayers = 17;  // Common, Layer A..P

    std::string layerName(int layer) {
        return layer == 0 ? std::string("common") : std::string("layer") + static_cast< char >('a' + layer - 1);
    }

    // "jmp/placement/layera/objinfo" -> 1, and the path with another layer; -1: not layered.
    int layerOf(const std::string& path) {
        const size_t a = path.find('/', 4), b = a == std::string::npos ? a : path.find('/', a + 1);
        if (b == std::string::npos) return -1;
        const std::string part = path.substr(a + 1, b - a - 1);
        for (int l = 0; l < kLayers; l++) {
            if (part == layerName(l)) return l;
        }
        return -1;
    }

    std::string withLayer(const std::string& path, int layer) {
        const size_t a = path.find('/', 4), b = path.find('/', a + 1);
        return path.substr(0, a + 1) + layerName(layer) + path.substr(b);
    }

    std::string leafOf(const std::string& path) {
        return path.substr(path.rfind('/') + 1);
    }

    int intCell(const ModelView& m, RowId row, const char* pColumn) {
        const Cell* pCell = m.cell(row, columnHash(pColumn));
        return pCell != nullptr ? pCell->i : -1;
    }

    // The zone's missions: zone 0 is in all; another zone where a StageObjInfo row of zone 0 in
    // that mission's layers names it.
    std::uint32_t zoneMissions(const ModelView& m, int zone) {
        const int missions = std::min(m.missionCount(), 32);
        const std::uint32_t all = missions >= 32 ? 0xFFFFFFFFu : (1u << missions) - 1;
        if (zone == 0) return all;
        std::uint32_t out = 0;
        for (int t = 0; t < m.tableCount(0); t++) {
            const std::string path = m.tablePath(0, t);
            if (leafOf(path) != "stageobjinfo") continue;
            const int layer = layerOf(path);
            m.forEachRow(0, t, [&](RowId row) {
                const Cell* pName = m.cell(row, columnHash("name"));
                if (m.state(row) == RowState::Deleted || pName == nullptr || pName->s != m.zoneName(zone)) return;
                for (int k = 1; k <= missions; k++) {
                    if (layer < 0 || (m.activeLayers(0, k) & (1u << layer))) out |= 1u << (k - 1);
                }
            });
        }
        return out;
    }

    // The fewest layers whose missions don't overlap and make `mask` exactly (exact first).
    bool cover(const std::vector< std::uint32_t >& layerMasks, std::uint32_t mask, std::vector< int >* pLayers) {
        std::vector< int > best;
        bool found = false;
        std::vector< int > current;
        // Depth-first over layers in order; at most 17 layers and 8 missions in practice.
        std::function< void(int, std::uint32_t) > search = [&](int from, std::uint32_t covered) {
            if (covered == mask) {
                if (!found || current.size() < best.size()) {
                    best = current;
                    found = true;
                }
                return;
            }
            if (found && current.size() + 1 >= best.size()) return;
            for (int l = from; l < kLayers; l++) {
                const std::uint32_t lm = layerMasks[l];
                if (lm == 0 || (lm & covered) || (lm & ~mask)) continue;
                current.push_back(l);
                search(l + 1, covered | lm);
                current.pop_back();
            }
        };
        search(0, 0);
        *pLayers = best;
        return found;
    }

    std::string missionList(std::uint32_t mask) {
        std::string out;
        for (int k = 0; k < 32; k++) {
            if (mask & (1u << k)) out += (out.empty() ? "" : ", ") + std::to_string(k + 1);
        }
        return out.empty() ? "none" : out;
    }

    // Same object: zone, table kind (leaf of a placement path), l_id and every cell (columns by hash).
    bool sameObject(const ModelView& m, RowId a, RowId b) {
        int za = 0, ta = 0, zb = 0, tb = 0;
        if (!m.locate(a, &za, &ta) || !m.locate(b, &zb, &tb) || za != zb) return false;
        if (leafOf(m.tablePath(za, ta)) != leafOf(m.tablePath(zb, tb)) || intCell(m, a, "l_id") != intCell(m, b, "l_id")) return false;
        for (const Column& c : m.columns(za, ta)) {
            const Cell* x = m.cell(a, c.hash);
            const Cell* y = m.cell(b, c.hash);
            if (x == nullptr || y == nullptr || x->type != y->type || x->i != y->i || x->f != y->f || x->s != y->s) return false;
        }
        return true;
    }
}  // namespace

std::uint32_t layerMissions(const ModelView& m, int zone, int layer) {
    const std::uint32_t zoneMask = zoneMissions(m, zone);
    std::uint32_t out = 0;
    for (int k = 1; k <= std::min(m.missionCount(), 32); k++) {
        if ((zoneMask & (1u << (k - 1))) && (m.activeLayers(zone, k) & (1u << layer))) out |= 1u << (k - 1);
    }
    return out;
}

std::vector< std::uint32_t > possibleMissionMasks(const ModelView& m, int zone) {
    std::vector< std::uint32_t > layerMasks(kLayers);
    for (int l = 0; l < kLayers; l++) layerMasks[l] = layerMissions(m, zone, l);
    std::set< std::uint32_t > out;
    // Unions of non-overlapping layers (subset search over the distinct layer masks).
    std::vector< std::uint32_t > distinct;
    for (std::uint32_t lm : layerMasks) {
        if (lm != 0 && std::find(distinct.begin(), distinct.end(), lm) == distinct.end()) distinct.push_back(lm);
    }
    std::function< void(size_t, std::uint32_t) > walk = [&](size_t i, std::uint32_t acc) {
        if (acc != 0) out.insert(acc);
        for (size_t j = i; j < distinct.size(); j++) {
            if (!(distinct[j] & acc)) walk(j + 1, acc | distinct[j]);
        }
    };
    walk(0, 0);
    return std::vector< std::uint32_t >(out.begin(), out.end());
}

namespace {
    // The row and its identical copies in the zone's other layers.
    std::vector< RowId > objectRows(const ModelView& m, RowId row, std::set< RowId >* pTaken) {
        std::vector< RowId > object{row};
        int zone = 0, table = 0;
        if (!m.locate(row, &zone, &table)) return object;
        if (pTaken != nullptr) pTaken->insert(row);
        const std::string leaf = leafOf(m.tablePath(zone, table));
        for (int t = 0; t < m.tableCount(zone); t++) {
            if (t == table || leafOf(m.tablePath(zone, t)) != leaf || layerOf(m.tablePath(zone, t)) < 0) continue;
            m.forEachRow(zone, t, [&](RowId other) {
                if ((pTaken == nullptr || !pTaken->count(other)) && m.state(other) != RowState::Deleted && sameObject(m, row, other)) {
                    object.push_back(other);
                    if (pTaken != nullptr) pTaken->insert(other);
                }
            });
        }
        return object;
    }
}  // namespace

std::uint32_t objectMissions(const ModelView& m, RowId row) {
    std::uint32_t out = 0;
    for (RowId r : objectRows(m, row, nullptr)) out |= m.rowMissions(r);
    return out;
}

MissionChange changeMissions(const ModelView& m, const std::vector< RowId >& rows, std::uint32_t missionMask) {
    MissionChange out;
    out.op.label = "Change missions";
    if (missionMask == 0) {
        return MissionChange{Op{}, {}, "An object needs at least one mission (delete it to remove it)"};
    }
    // Objects: each selected row with its split siblings (copies of it in other layers).
    std::vector< std::vector< RowId > > objects;
    std::set< RowId > taken;
    for (RowId row : rows) {
        if (taken.count(row) || m.state(row) == RowState::Deleted) continue;
        int zone = 0, table = 0;
        if (!m.locate(row, &zone, &table)) continue;
        objects.push_back(objectRows(m, row, &taken));
    }
    for (const std::vector< RowId >& object : objects) {
        int zone = 0, table = 0;
        m.locate(object.front(), &zone, &table);
        const std::string path = m.tablePath(zone, table);
        if (layerOf(path) < 0) {
            out.error = std::string(m.objectName(object.front()) ? m.objectName(object.front()) : "This object") + " isn't in a layered table";
            return MissionChange{Op{}, {}, out.error};
        }
        std::vector< std::uint32_t > layerMasks(kLayers);
        for (int l = 0; l < kLayers; l++) layerMasks[l] = layerMissions(m, zone, l);
        std::vector< int > layers;
        if (!cover(layerMasks, missionMask, &layers)) {
            std::string possible;
            for (std::uint32_t pm : possibleMissionMasks(m, zone)) possible += (possible.empty() ? "" : "; ") + missionList(pm);
            out.error = std::string("An object in ") + m.zoneName(zone) + " can't be in exactly missions " + missionList(missionMask) +
                        " (its layers allow: " + possible + ")";
            return MissionChange{Op{}, {}, out.error};
        }
        // Keep the copies already in a wanted layer; add the missing layers; delete the rest.
        std::map< int, RowId > byLayer;
        for (RowId r : object) {
            int z = 0, t = 0;
            m.locate(r, &z, &t);
            byLayer[layerOf(m.tablePath(z, t))] = r;
        }
        const int lid = intCell(m, object.front(), "l_id");
        for (int l : layers) {
            auto it = byLayer.find(l);
            if (it != byLayer.end()) {
                out.rows.push_back(it->second);
                continue;
            }
            AddRow add;
            add.id = newRowId();
            add.zone = zone;
            add.table = withLayer(path, l);
            add.copyOf = object.front();
            if (lid >= 0) add.cells.push_back({columnHash("l_id"), Cell{CellType::Int, lid, 0, {}}});
            out.op.adds.push_back(add);
            out.rows.push_back(add.id);
        }
        for (const auto& [l, r] : byLayer) {
            if (std::find(layers.begin(), layers.end(), l) == layers.end()) out.op.deletes.push_back(r);
        }
    }
    if (out.op.adds.empty() && out.op.deletes.empty()) out.op = Op{};
    return out;
}

int freeStarId(const ModelView& m, int mission) {
    std::set< int > used;
    for (int zone = 0; zone < m.zoneCount(); zone++) {
        for (int t = 0; t < m.tableCount(zone); t++) {
            m.forEachRow(zone, t, [&](RowId row) {
                const char* pName = m.objectName(row);
                if (pName != nullptr && std::string(pName) == "PowerStar" && m.state(row) != RowState::Deleted &&
                    (m.rowMissions(row) & (1u << (mission - 1)))) {
                    used.insert(intCell(m, row, "Obj_arg0"));
                }
            });
        }
    }
    for (int id = 1; id <= m.missionCount(); id++) {
        if (!used.count(id)) return id;
    }
    return m.missionCount() + 1;
}

}  // namespace PetariEditor::ModelOps
