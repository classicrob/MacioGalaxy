#ifdef PETARI_NATIVE
// Live check of the level editor's engine hooks (native/tools/editor_hooks_smoke.py):
// PETARI_EDITOR=1 PETARI_EDITOR_HOOKS_TEST=1 on the Good Egg stage smoke. Once the smoke's own
// checks are done it creates actors from synthesized rows (one cloned from the stage, one whose
// archive is not resident, 50 Coins), retires and re-creates one, and logs
// "[editor-hooks] PASS" or "[editor-hooks] FAIL: <reason>". Test code only: the editor runtime
// creates its rows from the EditModel.
#include "Game/Editor/EditorScene.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Scene/StageDataHolder.hpp"
#include "Game/Util/JMapUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include <JSystem/JGadget/hashcode.hpp>
#include <petari/editor/runtime_hooks.hpp>
#include <petari/smoke_phase.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>


namespace {
    struct Row {
        u8* mBytes = nullptr;
        u32 mSize = 0;
        JMapInfo mInfo;
    };

    void put32(u8* p, u32 v) {
        p[0] = v >> 24;
        p[1] = v >> 16;
        p[2] = v >> 8;
        p[3] = v;
    }

    const JMapItem* item(const JMapInfo& rInfo, const char* pKey) {
        s32 i = rInfo.searchItemInfo(pKey);
        return i < 0 ? nullptr : &rInfo.mData->mItems[i];
    }

    u8* cell(Row* pRow, const JMapItem* pItem) {
        return pRow->mBytes + static_cast< s32 >(pRow->mInfo.mData->mDataOffset) + static_cast< u16 >(pItem->mOffsData);
    }

    void setFloat(Row* pRow, const char* pKey, f32 value) {
        const JMapItem* pItem = item(pRow->mInfo, pKey);
        if (pItem != nullptr && pItem->mType == JMAP_VALUE_TYPE_FLOAT) {
            u32 bits;
            std::memcpy(&bits, &value, 4);
            put32(cell(pRow, pItem), bits);
        }
    }

    void setInt(Row* pRow, const char* pKey, s32 value) {
        const JMapItem* pItem = item(pRow->mInfo, pKey);
        if (pItem == nullptr) {
            return;
        }
        u8* p = cell(pRow, pItem);
        const u32 mask = pItem->mMask, shifted = (static_cast< u32 >(value) << pItem->mShift) & mask;
        if (pItem->mType == JMAP_VALUE_TYPE_SHORT) {
            u16 old = static_cast< u16 >(p[0] << 8 | p[1]);
            u16 v = static_cast< u16 >((old & ~mask) | shifted);
            p[0] = v >> 8;
            p[1] = v;
        } else if (pItem->mType == JMAP_VALUE_TYPE_BYTE) {
            p[0] = static_cast< u8 >((p[0] & ~mask) | shifted);
        } else if (pItem->mType == JMAP_VALUE_TYPE_LONG || pItem->mType == JMAP_VALUE_TYPE_LONG_2) {
            u32 old = static_cast< u32 >(p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]);
            put32(p, (old & ~mask) | shifted);
        }
    }

    // A one-row table with rSource's schema and row `index` (strings re-pooled; `pName` replaces
    // the name), registered for `zone`.
    Row* makeRow(const JMapInfo& rSource, s32 index, const char* pName, s32 zone) {
        const JMapData* pData = rSource.mData;
        const s32 dataOffset = pData->mDataOffset;
        const u32 entrySize = pData->mEntrySize;
        char pool[512];
        u32 poolSize = 0;
        Row* pRow = new Row();
        pRow->mSize = ((dataOffset + entrySize + sizeof(pool)) + 31) & ~31u;
        pRow->mBytes = new u8[pRow->mSize];
        std::memset(pRow->mBytes, '@', pRow->mSize);
        std::memcpy(pRow->mBytes, pData, dataOffset);
        std::memcpy(pRow->mBytes + dataOffset, getEntryAddress(pData, dataOffset, index), entrySize);
        put32(pRow->mBytes, 1);
        for (s32 i = 0; i < pData->mNumFields; i++) {
            const JMapItem& rItem = pData->mItems[i];
            if (rItem.mType != JMAP_VALUE_TYPE_STRING_PTR) {
                continue;
            }
            const char* pValue = "";
            rSource.getValueFast(index, i, &pValue);
            if (pName != nullptr && rItem.mHash == JGadget::getHashCode("name")) {
                pValue = pName;
            }
            put32(pRow->mBytes + dataOffset + static_cast< u16 >(rItem.mOffsData), poolSize);
            const u32 length = static_cast< u32 >(std::strlen(pValue)) + 1;
            if (poolSize + length <= sizeof(pool)) {
                std::memcpy(pool + poolSize, pValue, length);
                poolSize += length;
            }
        }
        std::memcpy(pRow->mBytes + dataOffset + entrySize, pool, poolSize);
        pRow->mInfo.attach(pRow->mBytes);
        pRow->mInfo.setName("ObjInfo");
        PetariEditor::Hooks::registerRows(pRow->mBytes, pRow->mSize, zone);
        return pRow;
    }

    bool findTemplate(const StageDataHolder* pHolder, const char* pName, const JMapInfo** ppInfo, s32* pIndex) {
        for (const JMapInfo* pInfo = pHolder->mPlacementObjs.begin(); pInfo != pHolder->mPlacementObjs.end(); pInfo++) {
            if (strcasecmp(pInfo->getName(), "ObjInfo") != 0 || !pInfo->dataExists()) {
                continue;
            }
            for (s32 i = 0; i < pInfo->getNumEntries(); i++) {
                const char* pRowName = "";
                MR::getObjectName(&pRowName, JMapInfoIter(pInfo, i));
                if (pName == nullptr || strcasecmp(pRowName, pName) == 0) {
                    *ppInfo = pInfo;
                    *pIndex = i;
                    return true;
                }
            }
        }
        for (s32 i = 0; i < pHolder->mStageDataHolderCount; i++) {
            if (findTemplate(pHolder->mStageDataArray[i], pName, ppInfo, pIndex)) {
                return true;
            }
        }
        return false;
    }

    // Clone `pTemplateName`'s row (or any ObjInfo row renamed to pName) at a world position in zone 0.
    bool spawn(const char* pTemplateName, const char* pName, const TVec3f& rPos, EditorScene::Created* pOut, const char** ppError) {
        const JMapInfo* pInfo = nullptr;
        s32 index = -1;
        if (!findTemplate(MR::getStageDataHolder(), pTemplateName, &pInfo, &index)) {
            *ppError = "no template row";
            return false;
        }
        Row* pRow = makeRow(*pInfo, index, pName, 0);
        static const char* const sLinks[] = {"SW_APPEAR", "SW_DEAD", "SW_A", "SW_B", "SW_SLEEP", "CastId", "GroupId", "DemoGroupId",
                                             "ClippingGroupId", "ViewGroupId", "MessageId", "CameraSetId", "MapParts_ID", "CommonPath_ID"};
        for (const char* pKey : sLinks) {
            setInt(pRow, pKey, -1);
        }
        if (pName != nullptr) {
            for (int i = 0; i < 8; i++) {
                char key[12];
                std::snprintf(key, sizeof(key), "Obj_arg%d", i);
                setInt(pRow, key, -1);
            }
            setInt(pRow, "ShapeModelNo", -1);
        }
        setInt(pRow, "l_id", 9000);
        setFloat(pRow, "pos_x", rPos.x);
        setFloat(pRow, "pos_y", rPos.y);
        setFloat(pRow, "pos_z", rPos.z);
        return EditorScene::createFromRow(JMapInfoIter(&pRow->mInfo, 0), pOut, ppError);
    }

    s32 gFrame = -1;
    EditorScene::Created gTakobo, gTeresa, gRecreated;
    TVec3f gTakoboStart;
    const char* gFailure = nullptr;

    void fail(const char* pReason) {
        if (gFailure == nullptr) {
            gFailure = pReason;
            std::fprintf(stderr, "[editor-hooks] FAIL: %s\n", pReason);
        }
    }
}  // namespace

namespace EditorHooksTest {
    void update(bool actionNerve) {
        static const bool sOn = [] {
            const char* p = std::getenv("PETARI_EDITOR_HOOKS_TEST");
            return p != nullptr && p[0] == '1' && PetariEditor::Hooks::enabled();
        }();
        if (!sOn || gFailure != nullptr) {
            return;
        }
        if (gFrame < 0) {
            if (!actionNerve || !PetariNative::SmokePhase::stageChecksDone.load()) {
                return;
            }
            gFrame = 0;
        }
        const s32 f = gFrame++;
        TVec3f mario = *MR::getPlayerPos(), up(-MR::getPlayerGravity()->x, -MR::getPlayerGravity()->y, -MR::getPlayerGravity()->z), front;
        MR::getPlayerFrontVec(&front);
        const char* pError = "";
        if (f == 10) {
            TVec3f pos(mario.x + front.x * 400.0f + up.x * 250.0f, mario.y + front.y * 400.0f + up.y * 250.0f,
                       mario.z + front.z * 400.0f + up.z * 250.0f);
            if (!spawn("Takobo", nullptr, pos, &gTakobo, &pError) || gTakobo.mFirstActor == nullptr) {
                fail(pError[0] != '\0' ? pError : "Takobo created no actor");
                return;
            }
            gTakoboStart = gTakobo.mFirstActor->mPosition;
            std::fprintf(stderr, "[editor-hooks] created Takobo (%d actors) at (%.0f, %.0f, %.0f)\n", gTakobo.mActorCount, gTakoboStart.x,
                         gTakoboStart.y, gTakoboStart.z);
            TVec3f back(mario.x - front.x * 500.0f + up.x * 250.0f, mario.y - front.y * 500.0f + up.y * 250.0f,
                        mario.z - front.z * 500.0f + up.z * 250.0f);
            if (!spawn(nullptr, "Teresa", back, &gTeresa, &pError) || gTeresa.mFirstActor == nullptr) {
                fail(pError[0] != '\0' ? pError : "Teresa created no actor");
                return;
            }
            std::fprintf(stderr, "[editor-hooks] created Teresa (archive loaded at runtime)\n");
            for (int i = 0; i < 50; i++) {
                EditorScene::Created coin;
                TVec3f p(mario.x + front.x * (300.0f + 60.0f * (i / 10)), mario.y + 150.0f * up.y + 60.0f * (i % 10) * up.y, mario.z);
                if (!spawn("Coin", nullptr, p, &coin, &pError)) {
                    fail(pError);
                    return;
                }
            }
            std::fprintf(stderr, "[editor-hooks] created 50 Coins; live actors %d of %d\n", EditorScene::liveActorCount(),
                         EditorScene::liveActorLimit());
        }
        if (f == 150) {
            LiveActor* a = gTakobo.mFirstActor;
            const f32 fell = PSVECDistance(&gTakoboStart, &a->mPosition);
            std::fprintf(stderr, "[editor-hooks] Takobo after 140 frames: moved %.0f, dead %d, hidden %d\n", fell, MR::isDead(a),
                         MR::isHiddenModel(a));
            if (MR::isDead(a) || fell < 50.0f) {
                fail("the created Takobo did not move (not connected to movement?)");
                return;
            }
            if (MR::isDead(gTeresa.mFirstActor)) {
                fail("the created Teresa is dead");
                return;
            }
            EditorScene::retire(a);
            if (!MR::isDead(a) || !MR::isHiddenModel(a)) {
                fail("retire left the actor alive or shown");
                return;
            }
            if (!spawn("Takobo", nullptr, gTakoboStart, &gRecreated, &pError) || gRecreated.mFirstActor == nullptr) {
                fail(pError[0] != '\0' ? pError : "re-create failed");
                return;
            }
        }
        if (f == 300) {
            if (MR::isDead(gRecreated.mFirstActor) || !MR::isDead(gTakobo.mFirstActor)) {
                fail("the re-created Takobo is dead, or the retired one came back");
                return;
            }
            std::fprintf(stderr, "[editor-hooks] PASS\n");
            gFailure = "done";
        }
    }
}  // namespace EditorHooksTest
#endif
