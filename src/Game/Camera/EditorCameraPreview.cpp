#ifdef PETARI_NATIVE
#include "Game/Camera/EditorCameraPreview.hpp"
#include "Game/Camera/Camera.hpp"
#include "Game/Camera/CameraDirector.hpp"
#include "Game/Camera/CameraHeightArrange.hpp"
#include "Game/Camera/CameraHolder.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraMan.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
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
#include "Game/System/MainLoopFramework.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/LightUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
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
        alignas(32) unsigned char copy[width * height * 4], scratch[width * height * 4];
        std::uint32_t scene = 0;
        std::uint64_t lastSerial = 0;
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
        bool pose(const ModelView& m, const P::Request& request, P::View& out, CameraPoseParam* result) {
            const auto area = Areas::describe(m, request.area, Areas::preview(request.area));
            if (!area || !area->valid) {
                out.message = "Select a camera area to preview.";
                return false;
            }
            const auto param = Areas::cameraRow(m, *area);
            if (!param.valid()) {
                out.message = "This area has no matching camera parameters.";
                return false;
            }
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
            Reader reader(m, param);
            chunk->initiate();
            *chunk->mGeneralParam = CameraGeneralParam();
            chunk->load(&reader, director->mHolder);
            const int index = chunk->mCameraTypeIndex;
            const char* type = director->mHolder->getNameStrOf(index);
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
            const auto world = m.zoneWorldMatrix(area->zone);
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 4; ++j)
                    camera->mZoneMatrix[i][j] = world.m[i][j];
            Vec3 source;
            if (request.source == P::Source::Mario) {
                source = point(*MR::getPlayerPos());
                out.message = "From Mario";
            } else if (Runtime::playMarker(&source) && Areas::contains(*area, source))
                out.message = "From play marker";
            else {
                source = Areas::center(*area);
                out.message = "From area centre (marker outside area)";
            }
            target->mPosition = vector(source);
            target->mLastMove.zero();
            target->mCameraArea = nullptr;
            target->mUp.set(area->axes.y.x, area->axes.y.y, area->axes.y.z);
            target->mFront.set(area->axes.z.x, area->axes.z.y, area->axes.z.z);
            MR::calcGravityVectorOrZero(nullptr, target->mPosition, &target->mGravityVector, nullptr, 0);
            if (target->mGravityVector.squared() < 0.0001f)
                target->mGravityVector = -target->mUp;
            else
                target->mUp = -target->mGravityVector;
            if (request.source == P::Source::Mario) {
                MR::getPlayerUpVec(&target->mUp);
                MR::getPlayerFrontVec(&target->mFront);
            }
            target->mSide = target->mUp.cross(target->mFront);
            if (target->mSide.squared() < 0.0001f)
                target->mSide.set(area->axes.x.x, area->axes.x.y, area->axes.x.z);
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
            return true;
        }
    }  // namespace
    void render() {
        if (!EditorCamera::isActive())
            return;
        const auto request = P::current();
        const auto* m = model();
        if (!m || !request.area.valid() || request.serial == lastSerial || P::awaitingReadback())
            return;
        lastSerial = request.serial;
        P::View out;
        out.area = request.area;
        out.serial = request.serial;
        CameraPoseParam cameraPose;
        if (!pose(*m, request, out, &cameraPose)) {
            publishView(std::move(out));
            return;
        }
        Mtx view, saved;
        TPos3f cameraWorld;
        MR::getCameraDirector()->calcViewMtxFromPoseParam(&cameraWorld, &cameraPose);
        PSMTXInverse(cameraWorld.toMtxPtr(), view);
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
                models.push_back(model);
                cost += EditorDraw::modelCost(model);
            }
        }
        if (cost > UINT32_MAX || !EditorDraw::spendBytes(static_cast< std::uint32_t >(cost))) {
            out.message = "Showing the last still: this view exceeds the preview draw budget.";
            publishView(std::move(out));
            return;
        }
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
        GXCopyTex(copy, GX_TRUE);
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
        petari_present_request_readback(copy);
        out.copy = copy;
        publishView(std::move(out));
    }
}  // namespace EditorCameraPreview
#endif
