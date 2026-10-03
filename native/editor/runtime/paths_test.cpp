#include "runtime_core.hpp"
#include <petari/editor/paths.hpp>
#include <petari/editor/paths_view.hpp>
#include <petari/editor/runtime_hooks.hpp>
#include <petari/editor_settings.hpp>
#include <petari/editor_pick.hpp>
#include <petari/host_allocation.hpp>
#include <petari/smoke_phase.hpp>
#include <petari/screenshot.hpp>
#include <petari/efb_dump_mark.hpp>
#include "Game/Editor/EditorScene.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/RailRider.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>

namespace PetariEditor::Runtime {
void pathsTestTick() {
    static const char* output=std::getenv("PETARI_EDITOR_PATHS_TEST");
    static int frame=-1,wait=0,cursor=0;
    static bool done=false;
    static RowId rider,point,railRow,other,inserted;
    static std::string writerField,readerField;
    static int switchId=-1;
    static LiveActor* old=nullptr;
    static LiveActor* moved=nullptr;
    static Vec3 original;
    static std::uint32_t scene=0;
    auto finish=[&](const std::string& text){std::fprintf(stderr,"[editor-paths] %s\n",text.c_str());done=true;PetariNative::EditorSettings::requestLeave();};
    if(!output||done)return;
    PetariNative::HostAllocationScope host;
    if(frame<0) {
        if(!PetariNative::SmokePhase::stageChecksDone.load())return;
        PetariNative::EditorSettings::toggle();frame=0;return;
    }
    if(frame==0 && mode()!=Mode::Editing) {if(++wait>600)finish("FAIL: Edit never started");return;}
    const int f=++frame;
    const auto* m=model();
    if(f==5) {
        if(!m)return finish("FAIL: no model");
        forEachBoundRow([&](RowId row,LiveActor* actor){
            const char* name=m->objectName(row);
            if(name&&std::string(name)=="BroadBeanMoveStepA") {rider=row;old=actor;}
            if(name&&std::string(name)=="BroadBeanMoveStepB")other=row;
        });
        if(!rider.valid()||!old||!old->mRailRider)return finish("FAIL: no BroadBeanMoveStepA rider");
        int zone=0;m->locate(rider,&zone,nullptr);
        const auto* link=m->cell(rider,columnHash("CommonPath_ID"));
        for(const auto& rail:Paths::collect(*m))if(rail.zone==zone && link && rail.id==link->i && !rail.points.empty()) {
            railRow=rail.row;point=rail.points.front().row;original=rail.points.front().position;
        }
        if(!point.valid())return finish("FAIL: no rail point");
        scene=Hooks::sceneSerial();cursor=m->historyCursor();
        PetariNative::EditorPick::selectRow(rider.v,PetariNative::EditorPick::Click::Replace);
        PetariNative::EditorSettings::frame();
        PathsView::setVisibility(PathsView::Visibility::Selection);
    }
    if(f==8)PetariNative::EditorPick::selectRow(railRow.v,PetariNative::EditorPick::Click::Replace);
    if(f==12)PetariNative::EditorSettings::frame();
    if(f==16)PetariNative::EditorSettings::dolly(-5);
    if(f==25) {
        Command command{Command::Kind::Apply};command.op.label="Move rail in live check";
        int zone=0;m->locate(rider,&zone,nullptr);
        for(const auto& rail:Paths::collect(*m))for(const auto& p:rail.points)if(p.row==point)
            command.op=Paths::movePoint(p,{p.position.x,p.position.y+300,p.position.z});
        enqueue(std::move(command));
    }
    if(f==35) {
        moved=actorForRow(rider);
        if(!moved||moved==old||!MR::isDead(old)||EditorScene::isConnectedToDraw(old))return finish("FAIL: rider not replaced/retired");
        if(!moved->mRailRider)return finish("FAIL: recreated actor lacks rail");
        TVec3f p;moved->mRailRider->copyPointPos(&p,0);
        int zone=0;m->locate(rider,&zone,nullptr);
        const Vec3 expected=DebugDraw::transform(m->zoneWorldMatrix(zone),{original.x,original.y+300,original.z});
        if(std::fabs(p.x-expected.x)+std::fabs(p.y-expected.y)+std::fabs(p.z-expected.z)>1)return finish("FAIL: recreated rail has wrong zone-space point");
        if(Hooks::sceneSerial()!=scene||m->historyCursor()!=cursor+1||status().pendingCount!=0)return finish("FAIL: reload/pending/history changed");
        std::fprintf(stderr,"[editor-paths] live recreation: child-zone point correct, old actor retired, scene unchanged\n");
        PetariNative::EditorPick::selectRow(point.v,PetariNative::EditorPick::Click::Replace);
    }
    if(f==40) {
        const auto selected=PetariNative::EditorPick::selection();
        if(selected.empty()||selected.front().row!=point.v||PathsView::view().curves.empty())return finish("FAIL: actorless point selection or curve projection");
        PetariNative::EfbDump::mark("paths-edited");
    }
    if(f==50) {
        const std::string file=std::string(output)+"/rail-edited.png";
        PetariNative::Screenshot::request(file.c_str());
        enqueue(Command{Command::Kind::Undo});
    }
    if(f==60) {
        auto* back=actorForRow(rider);
        if(!back||back==moved||!MR::isDead(moved)||!back->mRailRider)return finish("FAIL: undo did not recreate rider");
        TVec3f p;back->mRailRider->copyPointPos(&p,0);
        int zone=0;m->locate(rider,&zone,nullptr);
        const Vec3 expected=DebugDraw::transform(m->zoneWorldMatrix(zone),original);
        if(std::fabs(p.x-expected.x)+std::fabs(p.y-expected.y)+std::fabs(p.z-expected.z)>1)return finish("FAIL: undo rail geometry");
        if(Hooks::sceneSerial()!=scene||m->historyCursor()!=cursor)return finish("FAIL: undo reloaded or history wrong");
        std::fprintf(stderr,"[editor-paths] undo: rail and actor restored without scene reload\n");
    }
    if(f==65) {
        if(!other.valid())return finish("FAIL: no second map part for switch link");
        Paths::Socket from,to;
        for(const auto& socket:Paths::sockets(*m,rider))if(socket.role==Catalog::SwitchRole::Write && socket.field=="SW_DEAD")from=socket;
        for(const auto& socket:Paths::sockets(*m,other))if(socket.role==Catalog::SwitchRole::Read && socket.field=="SW_A")to=socket;
        std::string error;
        Command command{Command::Kind::Apply};command.op=Paths::linkSwitchOp(*m,from,to,&error);
        if(command.op.sets.empty())return finish("FAIL: switch link: "+error);
        writerField=from.field;readerField=to.field;enqueue(std::move(command));
    }
    if(f==75) {
        const auto* a=m->cell(rider,columnHash(writerField.c_str()));
        const auto* b=m->cell(other,columnHash(readerField.c_str()));
        if(!a||!b||a->i<0||a->i!=b->i||status().pendingCount)return finish("FAIL: switch endpoints not linked live");
        switchId=a->i;
        PetariNative::EditorPick::selectRow(rider.v,PetariNative::EditorPick::Click::Replace);
        PetariNative::EditorPick::selectRow(other.v,PetariNative::EditorPick::Click::Add);
    }
    if(f==78)PetariNative::EditorSettings::frame();
    if(f==90) {
        if(PathsView::view().wires.empty())return finish("FAIL: no wire snapshot");
        PetariNative::EfbDump::mark("paths-wires");
        const std::string file=std::string(output)+"/switch-wires.png";PetariNative::Screenshot::request(file.c_str());
    }
    if(f==100) {Command command{Command::Kind::Apply};command.op=Paths::unlink(other,readerField);enqueue(std::move(command));}
    if(f==110) {
        if(m->cell(other,columnHash(readerField.c_str()))->i!=-1)return finish("FAIL: unlink failed");
        enqueue(Command{Command::Kind::Undo});
    }
    if(f==120) {
        if(m->cell(other,columnHash(readerField.c_str()))->i!=switchId || Hooks::sceneSerial()!=scene)return finish("FAIL: unlink undo or unexpected reload");
        std::fprintf(stderr,"[editor-paths] switch link/unlink undo: endpoints restored, scene unchanged\n");
    }
    if(f==125) {
        inserted=newRowId();
        for(const auto& rail:Paths::collect(*m))if(rail.row==railRow) {
            std::string error;Command command{Command::Kind::Apply};
            command.op=Paths::insertPoint(rail,0,0.5f,inserted,&error);
            if(command.op.adds.empty())return finish("FAIL: insert point: "+error);
            enqueue(std::move(command));
        }
    }
    if(f==135) {
        auto* actor=actorForRow(rider);
        if(!actor || !actor->mRailRider || actor->mRailRider->getPointNum()!=3 || m->railPoints(railRow)[1]!=inserted)
            return finish("FAIL: ordered point insertion did not reach live rider");
        enqueue(Command{Command::Kind::Undo});
    }
    if(f==145) {
        auto* actor=actorForRow(rider);
        if(!actor || !actor->mRailRider || actor->mRailRider->getPointNum()!=2 || Hooks::sceneSerial()!=scene)
            return finish("FAIL: insertion undo or unexpected reload");
        std::fprintf(stderr,"[editor-paths] point insertion/undo: live rider point count restored without reload\n");
        finish("PASS");
    }
}
}
