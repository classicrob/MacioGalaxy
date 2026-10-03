#include <petari/editor/debug_draw.hpp>
#include <cassert>
#include <cmath>
using namespace PetariEditor;
int main() {
    DebugDraw::Camera c{{2,3,4},{0,0,-1},{0,1,0},60,1.8f};
    for(float x:{-0.9f,0.0f,0.9f})for(float y:{-0.9f,0.0f,0.9f}) {
        const auto ray=c.ray(x,y);
        const auto p=DebugDraw::point(DebugDraw::vector(c.eye)+DebugDraw::vector(ray)*100);
        float sx=0,sy=0;
        assert(c.project(p,&sx,&sy));
        assert(std::fabs(sx-x)<1e-5f && std::fabs(sy-y)<1e-5f);
    }
    float x,y;
    assert(!c.project({2,3,5},&x,&y));
    Vec3 drag;
    assert(c.dragPlane(0.5f,0.2f,{0,0,-100},&drag));
    assert(std::fabs(drag.z+100)<1e-4f);
    Mtx34 zone{{{0,0,1,40},{0,1,0,20},{-1,0,0,60}}};
    auto p=DebugDraw::inverseTransform(zone,DebugDraw::transform(zone,{7,11,13}));
    assert(p.x==7 && p.y==11 && p.z==13);
    DebugDraw::Mesh mesh;
    DebugDraw::arrow(mesh,{0,0,0},{100,0,0},{0,1,0},0xffffffff);
    assert(mesh.lines.size()==3);
}
