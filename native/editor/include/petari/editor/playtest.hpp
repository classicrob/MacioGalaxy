#pragma once
#include <array>
#include <petari/editor/debug_draw.hpp>

namespace PetariEditor::Playtest {
    enum class Motion : std::uint8_t { Ground, Air, Dive, CapBounce };
    struct Sample {
        Vec3 position, up{0, 1, 0};
        Motion motion = Motion::Ground;
        bool peak = false, connected = false;
        std::uint64_t frame = 0;
    };
    class Trail {
    public:
        static constexpr std::size_t capacity = 3600;
        void begin();
        void resume();
        void pause();
        void end();
        void record(Vec3 position, Vec3 velocity, Vec3 gravity, Motion motion);
        std::vector< Sample > samples() const;
        std::uint64_t frames() const {
            return frames_;
        }
        bool active() const {
            return active_;
        }
        bool running() const {
            return running_;
        }

    private:
        std::array< Sample, capacity > ring_{};
        std::size_t next_ = 0, size_ = 0;
        std::uint64_t frames_ = 0;
        bool active_ = false, running_ = false, connect_ = false;
        float vertical_ = 0;
    };
    std::uint32_t motionColor(Motion motion);
    DebugDraw::Mesh geometry(const std::vector< Sample >& samples);
    std::string elapsed(std::uint64_t frames);
    std::string keepReason(const ModelView& model, RowId row);
    RowTransform inZone(const Mtx34& zone, const RowTransform& world);
    struct LiveTransform {
        RowId row;
        RowTransform world;
    };
    // Validates every row before constructing the single undo step; failure never keeps a subset.
    Op keepOp(const ModelView& model, const std::vector< LiveTransform >& live, std::string* error);
    struct View {
        bool active = false, running = false, cleared = false;
        std::uint64_t frames = 0;
        std::size_t samples = 0, peaks = 0;
        std::vector< RowId > keepRows;
        std::string keepReason, keepResult;
    };
    void setVisible(bool visible);
    bool visible();
    void publish(View view);
    View view();
    void requestKeep(std::vector< RowId > rows);
    std::vector< RowId > takeKeep();
}  // namespace PetariEditor::Playtest
