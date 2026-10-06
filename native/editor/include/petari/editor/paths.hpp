#pragma once
#include <petari/editor/catalog.hpp>
#include <petari/editor/edit_api.hpp>

namespace PetariEditor::Paths {
    struct Point {
        RowId row;
        Vec3 position, incoming, outgoing;  // pnt0, pnt1, pnt2; all zone-local, absolute positions.
    };
    struct Rail {
        RowId row;
        int zone = 0, id = -1;
        std::string table;
        bool closed = false;
        std::vector< Point > points;
    };
    std::vector< Rail > collect(const ModelView& model);
    // Point and rail IDs must be fresh; positions and handles are zone-local.
    Op addRail(const ModelView& model, int zone, const std::vector< Point >& points, bool closed, RowId freshRail, std::string* error,
               RowId rider = {});
    Vec3 evaluate(const Point& a, const Point& b, float t);
    // de Casteljau split preserves the entire curve, including its tangents.
    Point split(Point* a, Point* b, float t);
    Op insertPoint(const Rail& rail, int segment, float t, RowId freshPoint, std::string* error);
    Op deletePoint(const Rail& rail, RowId point, std::string* error);
    Op movePoint(const Point& point, Vec3 position);
    Op moveHandle(const Point& point, bool outgoing, Vec3 position);
    Op linkPath(const ModelView& model, RowId object, const Rail& rail, std::string* error);
    Op unlink(RowId object, const std::string& field);

    struct Socket {
        RowId row;
        int zone = 0, id = -1;
        std::string field;
        Catalog::SwitchRole role = Catalog::SwitchRole::Read;
    };
    std::vector< Socket > sockets(const ModelView& model, RowId row);
    std::vector< Socket > allSockets(const ModelView& model);
    struct Wire {
        Socket from, to;
    };
    std::vector< Wire > wires(const ModelView& model);
    Op linkSwitchOp(const ModelView& model, const Socket& source, const Socket& target, std::string* error);
    std::vector< Check > computeChecks(const ModelView& model);
    std::uint32_t switchColor(int id);
    std::uint32_t switchColor(const std::string& field);
}  // namespace PetariEditor::Paths
