#pragma once
#include <petari/editor/debug_draw.hpp>
#include <petari/editor/paths.hpp>

namespace PetariEditor::Arrays {
    constexpr int kMaxCopies = 256;
    enum class Pattern { Line, Arc, Ring, Grid, Path };
    enum class Facing { Preserve, Along, Outward, Inward };
    struct Parameters {
        Pattern pattern = Pattern::Line;
        Facing facing = Facing::Preserve;
        int count = 6, rows = 2, columns = 3;
        float spacing = 150, radius = 400, arcDegrees = 180, startDistance = 0;
        bool groundSnap = true, pathSpacing = false, markerCenter = false;
        RowId rail;
    };
    struct Sample {
        Gizmo::V3 position, forward;
    };
    struct Polyline {
        std::vector< Gizmo::V3 > points;
        std::vector< float > distances;
        bool closed = false;
        float length() const {
            return distances.empty() ? 0 : distances.back();
        }
    };
    Parameters bounded(Parameters parameters);
    Polyline railPolyline(const Paths::Rail& rail, const Mtx34& zone);
    Sample atDistance(const Polyline& line, float distance);
    // Positions are world-space. Count is the number of new placements; the source remains.
    std::vector< Sample > layout(const Parameters& parameters, Gizmo::V3 origin, const Gizmo::Basis& frame, const Polyline* rail = nullptr);
    struct Copy {
        RowId source;
        Gizmo::V3 position;
        Gizmo::Basis basis;
        // Original pose, used to draw the source's model at the preview pose without modifying the actor.
        Gizmo::V3 originalPosition;
        Gizmo::Basis originalBasis;
        float radius = 60;
    };
    Op makeOp(const ModelView& model, const std::vector< Copy >& copies, const std::vector< RowId >& ids, std::string* error);
    struct View {
        bool active = false;
        std::vector< Copy > copies;
        std::string message;
        float pathLength = 0;
        bool handleVisible = false;
        float handleX = 0, handleY = 0;
    };
    struct Input {
        bool begin = false, apply = false, cancel = false;
        bool dragBegin = false, dragMove = false, dragEnd = false;
        float x = 0, y = 0;
    };
    std::uint64_t revision();
    const Parameters& parameters();
    void configure(Parameters parameters);
    const View& view();
    void publish(View view);
    void send(Input input);
    Input take();
}  // namespace PetariEditor::Arrays
