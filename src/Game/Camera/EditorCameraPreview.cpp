#ifdef PETARI_NATIVE
#include "Game/Camera/EditorCameraPreview.hpp"
#include "Game/Camera/Camera.hpp"
#include "Game/Camera/CameraDirector.hpp"
#include "Game/Camera/CameraHeightArrange.hpp"
#include "Game/Camera/CameraHolder.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraMan.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraParamChunkHolder.hpp"
#include "Game/Camera/CameraParamChunkID.hpp"
#include "Game/Camera/CameraPoseParam.hpp"
#include "Game/Camera/CameraTargetHolder.hpp"
#include "Game/Camera/CameraTargetMtx.hpp"
#include "Game/Camera/DotCamParams.hpp"
#include "Game/Camera/EditorCamera.hpp"
#include "Game/Camera/EditorDraw.hpp"
#include "Game/Camera/EditorPicking.hpp"
#include "Game/Editor/EditorScene.hpp"
#include "Game/Editor/EditorTerrain.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/System/MainLoopFramework.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/LightUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include <JSystem/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3DGraphAnimator/J3DModelData.hpp>
#include <JSystem/J3DGraphBase/J3DShape.hpp>
#include <JSystem/J3DGraphBase/J3DSys.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <petari/editor/areas.hpp>
#include <petari/editor/camera_preview.hpp>
#include <petari/editor/runtime.hpp>
#include <petari/editor/runtime_hooks.hpp>
#include <petari/host_allocation.hpp>
#include <revolution/mtx.h>
#include <vector>

extern "C" void petari_present_request_readback(const void*);
extern "C" void GXCreateFrameBuffer(u32, u32);
extern "C" void GXRestoreFrameBuffer();
namespace EditorCameraPreview {
    namespace {
        using namespace PetariEditor;
        namespace P = CameraPreview;
        constexpr unsigned width = 512, height = 288;
        alignas(32) unsigned char copy[width * height * 4], playCopy[width * height * 4], scratch[width * height * 4];
        std::uint32_t scene = 0;
        std::uint64_t lastSerial = 0, lastPlaySerial = 0;
        CameraMan* man = nullptr;
        CameraTargetMtx* target = nullptr;
        CameraParamChunkGame* chunk = nullptr;
        std::array< Camera*, 64 > cameras{};
        std::array< CamTranslatorBase*, 64 > translators{};
        class Reader : public DotCamReader {
        public:
            Reader(const ModelView& m, RowId row) : m(m), row(row) {
            }
            const Cell* value(const char* key) const {
                return m.cell(row, columnHash(key));
            }
            u32 getVersion() const override {
                const auto* c = value("version");
                return c ? c->i : 0x3000;
            }
            bool hasMoreChunk() const override {
                return true;
            }
            void nextToChunk() override {
            }
            bool getValueInt(const char* key, s32* out) override {
                const auto* c = value(key);
                if (!c)
                    return false;
                *out = c->i;
                return true;
            }
            bool getValueFloat(const char* key, f32* out) override {
                const auto* c = value(key);
                if (!c)
                    return false;
                *out = c->type == CellType::Float ? c->f : static_cast< float >(c->i);
                return true;
            }
            bool getValueString(const char* key, const char** out) override {
                const auto* c = value(key);
                if (!c)
                    return false;
                *out = c->s.c_str();
                return true;
            }
            bool getValueVec(const char* key, TVec3f* out) override {
                float xyz[3];
                char field[128];
                for (int i = 0; i < 3; ++i) {
                    std::snprintf(field, sizeof(field), "%s.%c", key, 'X' + i);
                    if (!getValueFloat(field, &xyz[i]))
                        return false;
                }
                out->set(xyz[0], xyz[1], xyz[2]);
                return true;
            }
            const ModelView& m;
            RowId row;
        };
        bool supports(const char* type) {
            // These camera classes calculate from the target and parameters, without boss,
            // actor-register or camera-animation dependencies.
            for (const char* name :
                 {"CAM_TYPE_XZ_PARA", "CAM_TYPE_TOWER", "CAM_TYPE_FOLLOW", "CAM_TYPE_WONDER_PLANET", "CAM_TYPE_POINT_FIX", "CAM_TYPE_EYEPOS_FIX",
                  "CAM_TYPE_SLIDER", "CAM_TYPE_INWARD_TOWER", "CAM_TYPE_INWARD_SPHERE", "CAM_TYPE_TRIPOD_PLANET", "CAM_TYPE_GROUND",
                  "CAM_TYPE_2D_SLIDE", "CAM_TYPE_WATER_FOLLOW", "CAM_TYPE_WATER_PLANET"})
                if (std::strcmp(type, name) == 0)
                    return true;
            return false;
        }
        TVec3f vector(Vec3 v) {
            return TVec3f(v.x, v.y, v.z);
        }
        Vec3 point(const TVec3f& v) {
            return {v.x, v.y, v.z};
        }
        bool finite(const TVec3f& v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }
        void publishView(P::View view) {
            PetariNative::HostAllocationScope host;
            P::publish(view);
        }
        // Where the camera's parameters come from: a cameraparam row in the layout (so edits show), the loaded
        // scene's chunk, or neither (the default camera, as the game's null camera).
        struct Choice {
            RowId row;
            const CameraParamChunk* live = nullptr;
            int zone = 0;  // the model zone, for the camera's zone matrix
            Vec3 source;
            TVec3f up, front, side;  // the target's frame; side: the fallback when up and front are parallel
        };
        bool evaluate(const ModelView& m, const Choice& choice, P::View& out, CameraPoseParam* result) {
            auto* director = MR::getCameraDirector();
            if (!director || !director->mHolder)
                return false;
            if (scene != Hooks::sceneSerial()) {
                scene = Hooks::sceneSerial();
                man = nullptr;
                target = nullptr;
                chunk = nullptr;
                cameras.fill(nullptr);
                translators.fill(nullptr);
            }
            if (!man) {
                man = new CameraMan("Editor camera preview");
                man->mDirector = director;
                target = new CameraTargetMtx("Editor camera target");
                CameraParamChunkID_Tmp id;
                id.createCubeID(0, 0);
                chunk = new CameraParamChunkGame(director->mHolder, id);
            }
            chunk->initiate();
            *chunk->mGeneralParam = CameraGeneralParam();
            if (choice.row.valid()) {
                Reader reader(m, choice.row);
                chunk->load(&reader, director->mHolder);
            } else if (choice.live)
                chunk->copy(choice.live);
            else
                chunk->mCameraTypeIndex = director->mHolder->getIndexOfDefault();
            const int index = chunk->mCameraTypeIndex;
            const char* type = director->mHolder->getNameStrOf(index);
            out.cameraType = type;
            if (!supports(type)) {
                out.message = std::string(type) + " needs its live gameplay context; use Reload and Play to inspect it.";
                return false;
            }
            auto*& camera = cameras[index];
            auto*& translator = translators[index];
            if (!camera) {
                camera = director->mHolder->createPreviewCamera(index);
                translator = camera->createTranslator();
            }
            translator->setParam(chunk);
            camera->mCameraMan = man;
            const auto world = m.zoneWorldMatrix(choice.zone);
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 4; ++j)
                    camera->mZoneMatrix[i][j] = world.m[i][j];
            target->mPosition = vector(choice.source);
            target->mLastMove.zero();
            target->mCameraArea = nullptr;
            target->mUp = choice.up;
            target->mFront = choice.front;
            MR::calcGravityVectorOrZero(nullptr, target->mPosition, &target->mGravityVector, nullptr, 0);
            if (target->mGravityVector.squared() < 0.0001f)
                target->mGravityVector = -target->mUp;
            else
                target->mUp = -target->mGravityVector;
            target->mSide = target->mUp.cross(target->mFront);
            if (target->mSide.squared() < 0.0001f)
                target->mSide = choice.side;
            target->mSide.normalize();
            target->mFront = target->mSide.cross(target->mUp);
            *man->mPoseParam = CameraPoseParam();
            man->mPoseParam->mWatchPos = target->mPosition;
            man->mPoseParam->mPos = target->mPosition - target->mFront * 1500.0f + target->mUp * 500.0f;
            man->mPoseParam->mUpVec = target->mUp;
            man->mPoseParam->mWatchUpVec = target->mUp;
            camera->mPoseParam->copyFrom(*man->mPoseParam);
            camera->mPoseParam->mGlobalOffset = chunk->mExParam.mWOffset;
            camera->mPoseParam->mFrontOffset = chunk->mExParam.mLOffset;
            camera->mPoseParam->mUpperOffset = chunk->mExParam.mLOffsetV;
            camera->mPoseParam->mRoll = chunk->mExParam.mRoll;
            camera->mPoseParam->mFovy = chunk->isOnUseFovy() ? chunk->mExParam.mFovy : director->getDefaultFovy();
            camera->mIsLOfsErpOff = chunk->isLOfsErpOff();
            if (camera->mVPan) {
                auto* pan = camera->mVPan;
                pan->resetParameter();
                const auto& param = chunk->mExParam;
                pan->mFocalScaleUpper = param.mUpper;
                pan->mFocalScaleLower = param.mLower;
                pan->mGndInt = param.mGndInt;
                pan->mPosOffsetMinRiseLag = param.mUPlay;
                pan->mPosOffsetMinDropLag = param.mLPlay;
                pan->mRiseDelay = param.mPushDelay;
                pan->mDropDelay = param.mPushDelayLow;
                pan->mMaxRiseEaseTime = param.mUDown;
                pan->mVPanUse = param.mVPanUse != 0;
                chunk->getVPanAxis(&pan->mVPanAxis);
                pan->mUpdateGlobalAxis = true;
            }

            struct Restore {
                CameraDirector* director;
                CameraTargetObj* target;
                CameraTargetObj* used;
                ~Restore() {
                    director->mTargetHolder->mTarget = target;
                    director->mTargetObj = used;
                }
            } restore{director, director->mTargetHolder->mTarget, director->mTargetObj};
            director->mTargetHolder->mTarget = target;
            camera->reset();
            for (int i = 0; i < 30; ++i)
                camera->calc();
            result->copyFrom(*camera->mPoseParam);
            if (!finite(result->mPos) || !finite(result->mWatchPos) || !finite(result->mUpVec) || (result->mPos - result->mWatchPos).squared() < 1 ||
                !std::isfinite(result->mFovy) || result->mFovy <= 1 || result->mFovy >= 179) {
                out.message = "Camera parameters do not produce a valid view.";
                return false;
            }
            out.eye = point(result->mPos);
            out.target = point(result->mWatchPos);
            out.fovy = result->mFovy;
            out.roll = result->mRoll;
            return true;
        }
        // The selected camera area's camera.
        bool pose(const ModelView& m, const P::Request& request, P::View& out, CameraPoseParam* result) {
            const auto area = Areas::describe(m, request.area, Areas::preview(request.area));
            if (!area || !area->valid) {
                out.message = "Select a camera area to preview.";
                return false;
            }
            Choice choice;
            choice.row = Areas::cameraRow(m, *area);
            if (!choice.row.valid()) {
                out.message = "This area has no matching camera parameters.";
                return false;
            }
            choice.zone = area->zone;
            if (request.source == P::Source::Mario) {
                choice.source = point(*MR::getPlayerPos());
                out.message = "From Mario";
            } else if (Runtime::playMarker(&choice.source) && Areas::contains(*area, choice.source))
                out.message = "From play marker";
            else {
                choice.source = Areas::center(*area);
                out.message = "From area centre (marker outside area)";
            }
            choice.up.set(area->axes.y.x, area->axes.y.y, area->axes.y.z);
            choice.front.set(area->axes.z.x, area->axes.z.y, area->axes.z.z);
            choice.side.set(area->axes.x.x, area->axes.x.y, area->axes.x.z);
            if (request.source == P::Source::Mario) {
                MR::getPlayerUpVec(&choice.up);
                MR::getPlayerFrontVec(&choice.front);
            }
            return evaluate(m, choice, out, result);
        }
        // A cameraparam row by its ID ("c:0003", "g:Planet:2:0") in a zone's camera tables.
        RowId cameraParamRow(const ModelView& m, int zone, const std::string& id) {
            RowId result;
            if (zone < 0 || zone >= m.zoneCount())
                return result;
            for (int t = 0; t < m.tableCount(zone); ++t)
                if (std::string(m.tablePath(zone, t)).find("cameraparam.bcam") != std::string::npos)
                    m.forEachRow(zone, t, [&](RowId row) {
                        const auto* cell = m.cell(row, columnHash("id"));
                        if (m.state(row) != RowState::Deleted && cell && cell->s == id)
                            result = row;
                    });
            return result;
        }
        // The model zone of a loaded zone (by name: the scene's zone IDs and the layout's indices may differ).
        int modelZone(const ModelView& m, s32 gameZone) {
            const char* name = MR::getZoneNameFromZoneId(gameZone);
            for (int z = 0; name && z < m.zoneCount(); ++z)
                if (std::strcmp(m.zoneName(z), name) == 0)
                    return z;
            return -1;
        }
        // Play's camera at the play marker (CameraManGame::updateNormal): the highest-priority normal camera area
        // containing it, else the ground's collision camera, else the default camera.
        bool playPose(const ModelView& m, P::View& out, CameraPoseParam* result) {
            Choice choice;
            std::string from;
            if (Runtime::playMarker(&choice.source))
                from = "From the play marker";
            else {
                choice.source = point(*MR::getPlayerPos());
                from = "From Mario (no play marker set)";
            }
            MR::getPlayerUpVec(&choice.up);
            MR::getPlayerFrontVec(&choice.front);
            TVec3f gravity;
            MR::calcGravityVectorOrZero(nullptr, vector(choice.source), &gravity, nullptr, 0);
            if (gravity.squared() > 0.0001f)
                choice.up = -gravity;
            choice.side = choice.up.cross(std::fabs(choice.up.x) < 0.9f ? TVec3f(1, 0, 0) : TVec3f(0, 0, 1));
            const int mission = m.scenario();
            const unsigned mask = mission >= 1 && mission <= 32 ? 1u << (mission - 1) : ~0u;
            std::optional< Areas::Volume > best;
            for (const auto& v : Areas::collect(m)) {
                if (v.definition->kind != Areas::Kind::Camera || !v.valid || !(v.missions & mask) || !(v.categories & 1))
                    continue;
                const auto* validity = m.cell(v.row, columnHash("Validity"));
                if ((validity && validity->s == "Invalid") || !Areas::contains(v, choice.source))
                    continue;
                if (!best || v.priority >= best->priority)  // the game sorts by priority and takes the last match
                    best = v;
            }
            char id[0x120];
            auto* director = MR::getCameraDirector();
            if (best) {
                std::snprintf(id, sizeof(id), "c:%04x", best->cameraId & 0xFFFF);
                out.area = best->row;
                out.camera = "Camera area";
                out.cameraId = id;
                choice.row = Areas::cameraRow(m, *best);
                choice.zone = best->zone;
                if (!choice.row.valid()) {
                    out.message = "The camera area here has no matching camera parameters.";
                    return false;
                }
            } else {
                Triangle ground;
                TVec3f hit;
                const TVec3f start = vector(choice.source) + choice.up * 200.0f;
                if (MR::getFirstPolyOnLineToMap(&hit, &ground, start, choice.up * -2000.0f) && ground.isValid() && MR::getCameraID(&ground) < 0xFF) {
                    const s32 gameZone = ground.getHostPlacementZoneID();
                    std::snprintf(id, sizeof(id), "g:%s:%d:0", ground.getHostName(), static_cast< int >(MR::getCameraID(&ground)));
                    out.camera = "Ground camera";
                    out.cameraId = id;
                    choice.zone = std::max(0, modelZone(m, gameZone));
                    choice.row = cameraParamRow(m, modelZone(m, gameZone), id);
                    if (!choice.row.valid() && director && director->mChunkHolder)
                        choice.live = director->mChunkHolder->findChunk(gameZone, id);
                    if (!choice.row.valid() && !choice.live) {
                        out.message = "The ground here names a camera that is not loaded.";
                        return false;
                    }
                } else {
                    out.camera = "Default camera";
                    from += ". No camera area or ground camera here: Play keeps the camera it had, else this one";
                }
            }
            out.message = from;
            return evaluate(m, choice, out, result);
        }
        // Draws the scene from a camera pose into a copy buffer, nearest models first while the draw budget lasts;
        // false when none fit. *partial: some visible models were left out.
        bool draw(const CameraPoseParam& cameraPose, unsigned char* destination, bool* partial) {
            Mtx view, saved;
            TPos3f cameraWorld;
            MR::getCameraDirector()->calcViewMtxFromPoseParam(&cameraWorld, &cameraPose);
            PSMTXInverse(cameraWorld.toMtxPtr(), view);
            struct Candidate {
                float near;  // the nearest its bounds come to the eye
                J3DModel* model;
                std::uint32_t cost;
            };
            std::vector< Candidate > visible;
            std::vector< J3DModel* > models;
            std::uint64_t cost = 0;
            {
                PetariNative::HostAllocationScope host;
                const float tanY = std::tan(cameraPose.mFovy * 0.5f * Gizmo::kDeg);
                const float tanX = tanY * static_cast< float >(width) / height;
                for (int i = 0; i < EditorScene::liveActorCount(); ++i) {
                    auto* actor = EditorScene::actorAt(i);
                    if (!actor || MR::isDead(actor) || MR::isHiddenModel(actor)) {
                        continue;
                    }
                    auto* model = MR::getJ3DModel(actor);
                    if (!model) {
                        model = EditorTerrain::partModel(actor);
                    }
                    if (!model) {
                        continue;
                    }
                    float center[3], radius;
                    EditorPicking::bounds(actor, center, &radius);
                    Vec world{center[0], center[1], center[2]}, local;
                    PSMTXMultVec(view, &world, &local);
                    const float depth = -local.z;
                    if (depth + radius < 10 || depth - radius > 1000000 || std::fabs(local.x) > depth * tanX + radius * std::sqrt(1 + tanX * tanX) ||
                        std::fabs(local.y) > depth * tanY + radius * std::sqrt(1 + tanY * tanY)) {
                        continue;
                    }
                    visible.push_back({depth - radius, model, EditorDraw::modelCost(model)});
                }
                // The ground under the camera and what's close matter most; far models go first when over budget.
                std::sort(visible.begin(), visible.end(), [](const Candidate& a, const Candidate& b) { return a.near < b.near; });
                const std::uint64_t allowance = EditorDraw::remaining();
                for (const auto& candidate : visible) {
                    if (cost + candidate.cost > allowance)
                        continue;
                    cost += candidate.cost;
                    models.push_back(candidate.model);
                }
            }
            *partial = models.size() < visible.size();
            if (models.empty() && !visible.empty())
                return false;
            if (!EditorDraw::spendBytes(static_cast< std::uint32_t >(cost)))
                return false;
            Mtx44 projection;
            C_MTXPerspective(projection, cameraPose.mFovy, static_cast< float >(width) / height, 10, 1000000);
            PSMTXCopy(j3dSys.mViewMtx, saved);
            PSMTXCopy(view, j3dSys.mViewMtx);
            GXCreateFrameBuffer(width, height);
            GXSetColorUpdate(GX_TRUE);
            GXSetAlphaUpdate(GX_TRUE);
            GXSetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
            GXSetCopyClear(GXColor{24, 29, 40, 255}, 0xFFFFFF);
            GXSetTexCopySrc(0, 0, width, height);
            GXSetTexCopyDst(width, height, GX_TF_RGBA8, GX_FALSE);
            GXCopyTex(scratch, GX_TRUE);
            const auto* frame = MainLoopFramework::getManager();
            GXSetCopyClear(frame->mClearColor, frame->mClearZ);
            GXSetViewport(0, 0, width, height, 0, 1);
            GXSetScissor(0, 0, width, height);
            GXSetProjection(projection, GX_PERSPECTIVE);
            MR::loadLightPlayer();
            GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
            for (auto* model : models) {
                model->viewCalc();
                j3dSys.setVtxPos(nullptr);
                const auto* data = model->getModelData();
                for (u16 material = 0; material < data->getMaterialNum(); ++material)
                    MR::simpleDraw(model, data->getMaterialNodePointer(material));
            }
            GXSetTexCopySrc(0, 0, width, height);
            GXSetTexCopyDst(width, height, GX_TF_RGBA8, GX_FALSE);
            GXCopyTex(destination, GX_TRUE);
            GXRestoreFrameBuffer();
            PSMTXCopy(saved, j3dSys.mViewMtx);
            for (auto* model : models) {
                model->viewCalc();
            }
            MR::loadViewMtx();
            MR::loadProjectionMtx();
            MR::setDefaultViewportAndScissor();
            J3DShape::resetVcdVatCache();
            j3dSys.reinitGX();
            petari_present_request_readback(destination);
            return true;
        }
        // The selected area's preview; true when it drew this frame.
        bool renderArea(const ModelView& m) {
            const auto request = P::current();
            if (!request.area.valid() || request.serial == lastSerial || P::awaitingReadback())
                return false;
            lastSerial = request.serial;
            P::View out;
            out.area = request.area;
            out.serial = request.serial;
            CameraPoseParam cameraPose;
            if (!pose(m, request, out, &cameraPose)) {
                publishView(std::move(out));
                return false;
            }
            bool partial = false;
            if (!draw(cameraPose, copy, &partial)) {
                out.message = "Showing the last still: this view exceeds the preview draw budget.";
                publishView(std::move(out));
                return false;
            }
            if (partial)
                out.message += " (nearest objects only)";
            out.copy = copy;
            publishView(std::move(out));
            return true;
        }
        void publishPlayView(P::View view) {
            PetariNative::HostAllocationScope host;
            P::publishPlay(view);
        }
        void renderPlay(const ModelView& m) {
            const auto request = P::currentPlay();
            if (!request.enabled || request.serial == lastPlaySerial || P::awaitingPlayReadback())
                return;
            lastPlaySerial = request.serial;
            P::View out;
            out.serial = request.serial;
            CameraPoseParam cameraPose;
            if (!playPose(m, out, &cameraPose)) {
                publishPlayView(std::move(out));
                return;
            }
            bool partial = false;
            if (!draw(cameraPose, playCopy, &partial)) {
                out.message = "Showing the last still: this view exceeds the preview draw budget.";
                publishPlayView(std::move(out));
                return;
            }
            if (partial)
                out.message += " (nearest objects only)";
            out.copy = playCopy;
            publishPlayView(std::move(out));
        }
    }  // namespace
    void render() {
        if (!EditorCamera::isActive())
            return;
        const auto* m = model();
        if (!m)
            return;
        if (!renderArea(*m))  // one offscreen pass a frame at most
            renderPlay(*m);
    }
}  // namespace EditorCameraPreview
#endif
