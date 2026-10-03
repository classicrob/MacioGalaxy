#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <petari/editor/catalog.hpp>
#include <petari/editor/gizmo_math.hpp>
#include <petari/editor/playtest.hpp>
#include <set>

namespace PetariEditor::Playtest {
    namespace {
        namespace D = DebugDraw;
        namespace G = Gizmo;
        std::atomic< bool > shown{true};
        std::mutex mutex;
        View snapshot;
        std::vector< RowId > pendingKeep;
        G::V3 v(Vec3 p) {
            return {p.x, p.y, p.z};
        }
        Vec3 vec(G::V3 p) {
            return {p.x, p.y, p.z};
        }
        bool finite(Vec3 p) {
            return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
        }
    }  // namespace
    void Trail::begin() {
        end();
        active_ = running_ = true;
    }
    void Trail::resume() {
        if (active_) {
            running_ = true;
            connect_ = false;
        }
    }
    void Trail::pause() {
        running_ = false;
        connect_ = false;
    }
    void Trail::end() {
        next_ = size_ = frames_ = 0;
        active_ = running_ = connect_ = false;
        vertical_ = 0;
    }
    void Trail::record(Vec3 p, Vec3 velocity, Vec3 gravity, Motion motion) {
        if (!running_)
            return;
        ++frames_;
        if (!finite(p) || !finite(velocity) || !finite(gravity)) {
            connect_ = false;
            return;
        }
        const auto up = G::normalized(v(gravity) * -1);
        const float vertical = G::dot(v(velocity), up);
        bool connected = connect_ && size_;
        if (connected) {
            auto& previous = ring_[(next_ + capacity - 1) % capacity];
            connected = G::length(v(p) - v(previous.position)) < 2000;
            if (connected && previous.motion != Motion::Ground && motion != Motion::Ground && vertical_ > 0 && vertical <= 0)
                previous.peak = true;
        }
        ring_[next_] = {p, vec(up), motion, false, connected, frames_};
        next_ = (next_ + 1) % capacity;
        size_ = std::min(size_ + 1, capacity);
        vertical_ = vertical;
        connect_ = true;
    }
    std::vector< Sample > Trail::samples() const {
        std::vector< Sample > out;
        out.reserve(size_);
        for (std::size_t i = 0; i < size_; ++i)
            out.push_back(ring_[(next_ + capacity - size_ + i) % capacity]);
        if (!out.empty())
            out.front().connected = false;
        return out;
    }
    std::uint32_t motionColor(Motion motion) {
        constexpr std::uint32_t colors[] = {0xe090e580, 0xe0f2cf67, 0xe054a9ff, 0xe0ea90df};
        return colors[static_cast< unsigned >(motion)];
    }
    DebugDraw::Mesh geometry(const std::vector< Sample >& samples) {
        D::Mesh mesh;
        for (std::size_t i = 0; i < samples.size(); ++i) {
            const auto& s = samples[i];
            if (i && s.connected && s.frame % 4 < 2)
                mesh.lines.push_back({samples[i - 1].position, s.position, motionColor(s.motion), 2});
            if (s.peak) {
                const auto b = G::gravityFrame(v(s.up), {0, 0, 1});
                const auto p = v(s.position) + b.y * 12;
                const G::V3 corners[] = {p + b.y * 22, p + b.x * 16, p - b.y * 22, p - b.x * 16};
                for (int j = 0; j < 4; ++j)
                    mesh.lines.push_back({vec(corners[j]), vec(corners[(j + 1) % 4]), 0xff7beeff, 3});
            }
        }
        return mesh;
    }
    std::string elapsed(std::uint64_t frames) {
        const auto seconds = frames / 60;
        char text[48];
        std::snprintf(text, sizeof(text), "%llu:%02llu", static_cast< unsigned long long >(seconds / 60),
                      static_cast< unsigned long long >(seconds % 60));
        return text;
    }
    std::string keepReason(const ModelView& model, RowId row) {
        int zone, table;
        if (!model.locate(row, &zone, &table) || model.state(row) == RowState::Deleted)
            return "The object is no longer in the layout.";
        if (!model.hasTransform(row))
            return "This selection has no placement transform.";
        const std::string path = model.tablePath(zone, table);
        if (path.find("childobjinfo") != std::string::npos)
            return "A parent controls this object's placement.";
        const auto* rail = model.cell(row, columnHash("CommonPath_ID"));
        if (rail && rail->i >= 0)
            return "A path controls this object's placement.";
        const char* name = model.objectName(row);
        const auto* info = name ? Catalog::find(name) : nullptr;
        if (!info || !info->keepable)
            return "Keep is not verified for this object type.";
        return {};
    }
    RowTransform inZone(const Mtx34& zone, const RowTransform& world) {
        const G::Basis z{
            {zone.m[0][0], zone.m[1][0], zone.m[2][0]}, {zone.m[0][1], zone.m[1][1], zone.m[2][1]}, {zone.m[0][2], zone.m[1][2], zone.m[2][2]}};
        auto inverse = [&](G::V3 p) -> G::V3 { return {G::dot(p, z.x), G::dot(p, z.y), G::dot(p, z.z)}; };
        const auto rotation = G::fromEulerDegrees(v(world.dirDeg));
        return {vec(inverse(v(world.pos) - G::V3{zone.m[0][3], zone.m[1][3], zone.m[2][3]})),
                vec(G::toEulerDegrees({inverse(rotation.x), inverse(rotation.y), inverse(rotation.z)})), world.scale};
    }
    Op keepOp(const ModelView& model, const std::vector< LiveTransform >& live, std::string* error) {
        auto fail = [&](const std::string& why) {
            if (error)
                *error = why;
            return Op{};
        };
        if (error)
            error->clear();
        if (live.empty())
            return fail("Select a moved object to Keep.");
        Op op;
        op.label = live.size() == 1 ? "Keep played transform" : "Keep played transforms";
        std::set< RowId > seen;
        for (const auto& item : live) {
            const auto reason = keepReason(model, item.row);
            if (!reason.empty())
                return fail(reason);
            if (!finite(item.world.pos) || !finite(item.world.dirDeg) || !finite(item.world.scale))
                return fail("The live transform is invalid.");
            if (!seen.insert(item.row).second)
                continue;
            int zone;
            model.locate(item.row, &zone, nullptr);
            const auto local = inZone(model.zoneWorldMatrix(zone), item.world);
            const Vec3 values[] = {local.pos, local.dirDeg, local.scale};
            constexpr const char* names[][3] = {{"pos_x", "pos_y", "pos_z"}, {"dir_x", "dir_y", "dir_z"}, {"scale_x", "scale_y", "scale_z"}};
            for (int kind = 0; kind < 3; ++kind) {
                const float components[] = {values[kind].x, values[kind].y, values[kind].z};
                for (int axis = 0; axis < 3; ++axis) {
                    const auto hash = columnHash(names[kind][axis]);
                    const auto* old = model.cell(item.row, hash);
                    if (old && old->type == CellType::Float && std::fabs(old->f - components[axis]) < 0.0001f)
                        continue;
                    Cell cell;
                    cell.type = CellType::Float;
                    cell.f = components[axis];
                    op.sets.push_back({item.row, hash, cell});
                }
            }
        }
        return op;
    }
    void setVisible(bool value) {
        shown = value;
    }
    bool visible() {
        return shown;
    }
    void publish(View value) {
        std::lock_guard< std::mutex > guard(mutex);
        snapshot = std::move(value);
    }
    View view() {
        std::lock_guard< std::mutex > guard(mutex);
        return snapshot;
    }
    void requestKeep(std::vector< RowId > rows) {
        std::lock_guard< std::mutex > guard(mutex);
        pendingKeep = std::move(rows);
    }
    std::vector< RowId > takeKeep() {
        std::lock_guard< std::mutex > guard(mutex);
        auto rows = std::move(pendingKeep);
        pendingKeep.clear();
        return rows;
    }
}  // namespace PetariEditor::Playtest
