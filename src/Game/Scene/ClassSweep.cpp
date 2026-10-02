#ifdef PETARI_NATIVE
#include "Game/Scene/ClassSweep.hpp"
#include "Game/LiveActor/AllLiveActorGroup.hpp"
#include "Game/LiveActor/ClippingActorHolder.hpp"
#include "Game/LiveActor/ClippingDirector.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ViewGroupCtrl.hpp"
#include "Game/NameObj/ModelChangableObjFactory.hpp"
#include "Game/NameObj/NameObjArchiveListCollector.hpp"
#include "Game/NameObj/NameObjExecuteHolder.hpp"
#include "Game/NameObj/NameObjFactory.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Scene/StageDataHolder.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/JMapInfo.hpp"
#include "Game/Util/JMapUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include <JSystem/JGadget/hashcode.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <strings.h>
#include <string>
#include <unistd.h>
#include <vector>

extern "C" int petari_class_sweep_go = 0;

// Output protocol (stderr, one line each; native/tools/class_sweep.py parses it):
//   [class-sweep] BEGIN <i> <name>          before anything of the item runs
//   [class-sweep] CREATED <i> <name>        creation returned (a crash after this is in the run, not the create)
//   [class-sweep] RESULT <i> <name> key=value ...
//   [class-sweep] KILLED <i> <name>         the item's actors were killed
//   [class-sweep] BATCH-DONE <n> | ABORT <reason>
namespace {
    struct V {
        f32 x, y, z;
    };
    V add(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    V mul(V a, f32 s) { return {a.x * s, a.y * s, a.z * s}; }
    f32 len(V a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
    V norm(V a, V fallback) {
        f32 l = len(a);
        return l > 1e-4f ? mul(a, 1.0f / l) : fallback;
    }

    struct SyntheticRow {
        std::vector< u8 > bytes;
        JMapInfo* info;
        s32 zone;
    };

    struct Job {
        std::string name;
        std::string mode;  // "empty": every link field -1 (template args kept); "keep": the template row's links as they are
        std::vector< std::pair< std::string, std::string > > overrides;  // field=value (integer, or float if it has a '.')
    };

    std::vector< SyntheticRow* > gRows;  // never freed: actors may keep their JMapInfoIter
    std::vector< Job > gJobs;
    bool gChecked = false;
    bool gEnabled = false;
    s32 gFrame = -1;
    size_t gNext = 0;
    s32 gObserveFrames = 24;
    s32 gNextLid = 20000;

    enum Step { Idle, Observing, Settling };
    Step gStep = Idle;
    s32 gStepFrame = 0;
    s32 gActorsBefore = 0;
    s32 gActorsAfter = 0;
    V gSpawnPos = {0, 0, 0};
    s32 gCurrent = -1;
    std::string gCurrentName;
    s32 gNerveLostFrames = 0;

    double nowMs() {
        return std::chrono::duration< double, std::milli >(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // ---- BCSV row synthesis (from the S1 spike) -------------------------------------------

    void writeBE32(u8* p, u32 v) {
        p[0] = v >> 24;
        p[1] = v >> 16;
        p[2] = v >> 8;
        p[3] = v;
    }

    const JMapItem* findItem(const JMapInfo& rInfo, const char* pKey) {
        s32 i = rInfo.searchItemInfo(pKey);
        return i < 0 ? nullptr : &rInfo.mData->mItems[i];
    }

    bool setInt(SyntheticRow* pRow, const char* pKey, s32 value) {
        const JMapItem* pItem = findItem(*pRow->info, pKey);
        if (pItem == nullptr) {
            return false;
        }
        u8* pCell = pRow->bytes.data() + static_cast< s32 >(pRow->info->mData->mDataOffset) + static_cast< u16 >(pItem->mOffsData);
        u32 mask = pItem->mMask;
        u32 shifted = (static_cast< u32 >(value) << pItem->mShift) & mask;
        switch (pItem->mType) {
        case JMAP_VALUE_TYPE_LONG:
        case JMAP_VALUE_TYPE_LONG_2: {
            u32 old = PetariNative::readU32BE(reinterpret_cast< const char* >(pCell));
            writeBE32(pCell, (old & ~mask) | shifted);
            return true;
        }
        case JMAP_VALUE_TYPE_SHORT: {
            u16 old = PetariNative::readU16BE(reinterpret_cast< const char* >(pCell));
            u16 v = static_cast< u16 >((old & ~mask) | shifted);
            pCell[0] = v >> 8;
            pCell[1] = v;
            return true;
        }
        case JMAP_VALUE_TYPE_BYTE:
            pCell[0] = static_cast< u8 >((pCell[0] & ~mask) | shifted);
            return true;
        default:
            return false;
        }
    }

    bool setFloat(SyntheticRow* pRow, const char* pKey, f32 value) {
        const JMapItem* pItem = findItem(*pRow->info, pKey);
        if (pItem == nullptr || pItem->mType != JMAP_VALUE_TYPE_FLOAT) {
            return false;
        }
        u32 bits;
        std::memcpy(&bits, &value, 4);
        writeBE32(pRow->bytes.data() + static_cast< s32 >(pRow->info->mData->mDataOffset) + static_cast< u16 >(pItem->mOffsData), bits);
        return true;
    }

    // A one-row table with rSource's schema and row `index`'s values, every string re-pooled;
    // `pName` (if set) replaces the name column.
    SyntheticRow* makeRow(const JMapInfo& rSource, s32 index, const char* pName, s32 zone) {
        const JMapData* pData = rSource.mData;
        const s32 dataOffset = pData->mDataOffset;
        const u32 entrySize = pData->mEntrySize;
        const s32 numFields = pData->mNumFields;

        SyntheticRow* pRow = new SyntheticRow();
        pRow->zone = zone;
        std::vector< u8 >& b = pRow->bytes;
        b.assign(reinterpret_cast< const u8* >(pData), reinterpret_cast< const u8* >(pData) + dataOffset);
        const u8* pSrcRow = reinterpret_cast< const u8* >(getEntryAddress(pData, dataOffset, index));
        b.insert(b.end(), pSrcRow, pSrcRow + entrySize);
        writeBE32(b.data(), 1);  // mNumEntries

        std::string pool;
        for (s32 i = 0; i < numFields; i++) {
            const JMapItem& rItem = pData->mItems[i];
            if (rItem.mType != JMAP_VALUE_TYPE_STRING_PTR) {
                continue;
            }
            const char* pValue = "";
            rSource.getValueFast(index, i, &pValue);
            if (pName != nullptr && rItem.mHash == JGadget::getHashCode("name")) {
                pValue = pName;
            }
            writeBE32(b.data() + dataOffset + static_cast< u16 >(rItem.mOffsData), static_cast< u32 >(pool.size()));
            pool.append(pValue);
            pool.push_back('\0');
        }
        b.insert(b.end(), pool.begin(), pool.end());
        while (b.size() % 32 != 0) {
            b.push_back('@');
        }

        pRow->info = new JMapInfo();
        pRow->info->attach(b.data());
        pRow->info->setName("ObjInfo");
        gRows.push_back(pRow);
        return pRow;
    }

    bool findTemplate(const StageDataHolder* pHolder, const char* pName, const JMapInfo** ppInfo, s32* pIndex, const JMapInfo** ppAnyObjInfo) {
        for (const JMapInfo* pInfo = pHolder->mPlacementObjs.begin(); pInfo != pHolder->mPlacementObjs.end(); pInfo++) {
            if (strcasecmp(pInfo->getName(), "ObjInfo") != 0 || !pInfo->dataExists()) {
                continue;
            }
            if (*ppAnyObjInfo == nullptr && pInfo->getNumEntries() > 0) {
                *ppAnyObjInfo = pInfo;
            }
            for (s32 i = 0; i < pInfo->getNumEntries(); i++) {
                const char* pRowName = "";
                MR::getObjectName(&pRowName, JMapInfoIter(pInfo, i));
                if (strcasecmp(pRowName, pName) == 0) {
                    *ppInfo = pInfo;
                    *pIndex = i;
                    return true;
                }
            }
        }
        for (s32 i = 0; i < pHolder->mStageDataHolderCount; i++) {
            if (findTemplate(pHolder->mStageDataArray[i], pName, ppInfo, pIndex, ppAnyObjInfo)) {
                return true;
            }
        }
        return false;
    }

    // Every field of the ObjInfo schema that points at something else (rails, switches, groups, camera,
    // demo, message, map parts). ShapeModelNo is a model selector, not a link.
    const char* const sLinkFields[] = {"CameraSetId", "SW_APPEAR",    "SW_DEAD",      "SW_A",        "SW_B",       "SW_SLEEP",   "MessageId",
                                       "CastId",      "ViewGroupId",  "CommonPath_ID", "ClippingGroupId", "GroupId", "DemoGroupId", "MapParts_ID"};

    // ---- one item -------------------------------------------------------------------------

    struct Created {
        bool created;
        bool cloned;
        s32 zone;
        std::string tplLinks;     // a cloned template's non-empty link fields, "Field:value,..."
        std::string archives;     // the creator's archive list
        std::string nonResident;  // those not resident before this item mounted them
        double mountMs;
        double createMs;
    };

    Created spawn(const Job& rJob, V pos) {
        Created out = {false, false, -1, "", "", "", 0.0, 0.0};
        const char* pName = rJob.name.c_str();
        StageDataHolder* pRoot = MR::getStageDataHolder();
        const JMapInfo* pInfo = nullptr;
        const JMapInfo* pAny = nullptr;
        s32 index = -1;
        bool cloned = findTemplate(pRoot, pName, &pInfo, &index, &pAny);
        if (!cloned) {
            if (pAny == nullptr) {
                std::fprintf(stderr, "[class-sweep] NOTE %s: no ObjInfo schema to copy\n", pName);
                return out;
            }
            pInfo = pAny;
            index = 0;
        }
        out.cloned = cloned;

        // Same zone as the template (its rail and zone-local data resolve there); world -> zone-local.
        const s32 zone = MR::getPlacedZoneId(JMapInfoIter(pInfo, index));
        out.zone = zone;
        const TPos3f* pMtx = MR::getZonePlacementMtx(JMapInfoIter(pInfo, index));
        V d = {pos.x - pMtx->mMtx[0][3], pos.y - pMtx->mMtx[1][3], pos.z - pMtx->mMtx[2][3]};
        pos = {pMtx->mMtx[0][0] * d.x + pMtx->mMtx[1][0] * d.y + pMtx->mMtx[2][0] * d.z,
               pMtx->mMtx[0][1] * d.x + pMtx->mMtx[1][1] * d.y + pMtx->mMtx[2][1] * d.z,
               pMtx->mMtx[0][2] * d.x + pMtx->mMtx[1][2] * d.y + pMtx->mMtx[2][2] * d.z};
        SyntheticRow* pRow = makeRow(*pInfo, index, cloned ? nullptr : pName, zone);
        if (cloned) {
            for (const char* pKey : sLinkFields) {
                s32 value = -1;
                JMapInfoIter(pInfo, index).getValue< s32 >(pKey, &value);
                if (value != -1) {
                    out.tplLinks += (out.tplLinks.empty() ? "" : ",") + std::string(pKey) + ":" + std::to_string(value);
                }
            }
        }
        // "empty": every link field -1, except fields an override marks "keep"; "keep": the row's links as they are.
        if (rJob.mode != "keep") {
            for (const char* pKey : sLinkFields) {
                bool kept = false;
                for (const auto& rOverride : rJob.overrides) {
                    kept = kept || (rOverride.first == pKey && rOverride.second == "keep");
                }
                if (!kept) {
                    setInt(pRow, pKey, -1);
                }
            }
        }
        setInt(pRow, "l_id", gNextLid++);
        setFloat(pRow, "pos_x", pos.x);
        setFloat(pRow, "pos_y", pos.y);
        setFloat(pRow, "pos_z", pos.z);
        if (!cloned) {
            for (int i = 0; i < 8; i++) {
                char key[16];
                std::snprintf(key, sizeof(key), "Obj_arg%d", i);
                setInt(pRow, key, -1);
            }
            setInt(pRow, "ShapeModelNo", -1);
            setFloat(pRow, "dir_x", 0);
            setFloat(pRow, "dir_y", 0);
            setFloat(pRow, "dir_z", 0);
            setFloat(pRow, "scale_x", 1);
            setFloat(pRow, "scale_y", 1);
            setFloat(pRow, "scale_z", 1);
        }
        for (const auto& rOverride : rJob.overrides) {
            if (rOverride.second == "keep") {
                continue;
            }
            if (rOverride.second.find('.') != std::string::npos) {
                setFloat(pRow, rOverride.first.c_str(), static_cast< f32 >(std::atof(rOverride.second.c_str())));
            } else {
                setInt(pRow, rOverride.first.c_str(), static_cast< s32 >(std::atoi(rOverride.second.c_str())));
            }
        }
        JMapInfoIter iter(pRow->info, 0);

        // The game's own archive list for this class: mounted now and waited for (game thread).
        {
            NameObjArchiveListCollector collector;
            NameObjFactory::getMountObjectArchiveList(&collector, pName, iter);
            for (s32 i = 0; i < collector.getArchiveNum(); i++) {
                const char* pArchive = collector.getArchive(i);
                out.archives += (i ? "," : "") + std::string(pArchive);
                if (!MR::isLoadedObjectOrLayoutArchive(pArchive)) {
                    out.nonResident += (out.nonResident.empty() ? "" : ",") + std::string(pArchive);
                }
            }
        }
        const double m0 = nowMs();
        NameObjFactory::requestMountObjectArchives(pName, iter);
        MR::receiveAllRequestedFile();
        out.mountMs = nowMs() - m0;

        s32 shapeId = -1;
        MR::getJMapInfoShapeIdWithInit(iter, &shapeId);
        CreatorFuncPtr creator = shapeId != -1 ? MR::getModelChangableObjCreator(pName) : NameObjFactory::getCreator(pName);
        if (creator == nullptr) {
            std::fprintf(stderr, "[class-sweep] NOTE %s: no creator\n", pName);
            return out;
        }

        NameObjExecuteHolder* pExecHolder = MR::getSceneObj< NameObjExecuteHolder >(SceneObj_NameObjExecuteHolder);
        const s32 execBefore = pExecHolder->nativeExecuteCount();
        const double t1 = nowMs();
        MR::setCurrentPlacementZoneId(MR::getPlacedZoneId(iter));
        MR::startInitLiveActorSystemInfo();
        NameObj* pObj = creator(MR::getJapaneseObjectName(pName));
        pObj->init(iter);
        MR::initLiveActorSystemInfo(iter);
        pObj->initAfterPlacement();
        MR::clearCurrentPlacementZoneId();
        pExecHolder->nativeLateConnect(execBefore);
        ClippingActorHolder* pClip = MR::getClippingDirector()->mActorHolder;
        pClip->mViewGroupCtrl->initViewGroup(pClip->_10);
        pClip->mViewGroupCtrl->initViewGroup(pClip->_18);
        pClip->mViewGroupCtrl->initViewGroup(pClip->_1C);
        pClip->mViewGroupCtrl->initViewGroup(pClip->_14);
        out.createMs = nowMs() - t1;
        out.created = true;
        return out;
    }

    std::string gCreatedText;  // the current item's Created fields, for its RESULT line

    void parseJobs(const char* pPath) {
        std::ifstream in(pPath);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }
            std::vector< std::string > cols;
            size_t from = 0;
            for (;;) {
                size_t tab = line.find('\t', from);
                cols.push_back(line.substr(from, tab == std::string::npos ? std::string::npos : tab - from));
                if (tab == std::string::npos) {
                    break;
                }
                from = tab + 1;
            }
            Job job;
            job.name = cols[0];
            job.mode = cols.size() > 1 && !cols[1].empty() ? cols[1] : "empty";
            if (cols.size() > 2) {
                size_t pos = 0;
                while (pos < cols[2].size()) {
                    size_t comma = cols[2].find(',', pos);
                    std::string item = cols[2].substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                    size_t eq = item.find('=');
                    if (eq != std::string::npos) {
                        job.overrides.emplace_back(item.substr(0, eq), item.substr(eq + 1));
                    }
                    if (comma == std::string::npos) {
                        break;
                    }
                    pos = comma + 1;
                }
            }
            gJobs.push_back(job);
        }
    }

    V playerPos() {
        TVec3f* p = MR::getPlayerPos();
        return {p->x, p->y, p->z};
    }
    V playerUp() {
        const TVec3f* g = MR::getPlayerGravity();
        return norm({-g->x, -g->y, -g->z}, V{0, 1, 0});
    }
    V playerFront() {
        TVec3f f;
        MR::getPlayerFrontVec(&f);
        return norm({f.x, f.y, f.z}, V{0, 0, 1});
    }

    [[noreturn]] void finishBatch(const char* pLine) {
        std::fprintf(stderr, "[class-sweep] %s\n", pLine);
        std::fflush(stderr);
        _exit(0);
    }

    void observe(const Job& rJob) {
        AllLiveActorGroup* pGroup = MR::getAllLiveActorGroup();
        s32 actors = pGroup->getObjNum() - gActorsBefore;
        s32 alive = 0, hidden = 0, clipped = 0, validDraw = 0, model = 0, drawn = 0;
        double maxMove = 0;
        bool finite = true;
        std::string firstName;
        for (s32 i = gActorsBefore; i < gActorsBefore + actors; i++) {
            LiveActor* a = pGroup->getActor(i);
            if (firstName.empty()) {
                firstName = a->mName != nullptr ? a->mName : "?";
            }
            const bool dead = MR::isDead(a);
            const bool hid = !dead && MR::isHiddenModel(a);
            const bool clip = !dead && MR::isClipped(a);
            const bool vd = !dead && MR::isValidDraw(a);
            const bool hasModel = a->mModelManager != nullptr;
            alive += dead ? 0 : 1;
            hidden += hid;
            clipped += clip;
            validDraw += vd;
            model += hasModel;
            drawn += (!dead && hasModel && !hid && !clip && vd) ? 1 : 0;
            finite = finite && std::isfinite(a->mPosition.x) && std::isfinite(a->mPosition.y) && std::isfinite(a->mPosition.z);
            V p = {a->mPosition.x, a->mPosition.y, a->mPosition.z};
            maxMove = std::fmax(maxMove, len(add(p, mul(gSpawnPos, -1.0f))));
        }
        const char* status;
        if (actors == 0) {
            status = "nonactor";
        } else if (!finite) {
            status = "nan";
        } else if (alive == 0) {
            status = "dead";
        } else if (drawn > 0) {
            status = "ok";
        } else if (model == 0) {
            status = "ok-nomodel";
        } else {
            status = "invisible";
        }
        std::fprintf(stderr,
                     "[class-sweep] RESULT %d %s status=%s actors=%d alive=%d model=%d drawn=%d hidden=%d clipped=%d validdraw=%d move=%.0f "
                     "actor=%s %s\n",
                     gCurrent, rJob.name.c_str(), status, actors, alive, model, drawn, hidden, clipped, validDraw, maxMove, firstName.c_str(),
                     gCreatedText.c_str());
    }

    void killItem(const Job& rJob) {
        AllLiveActorGroup* pGroup = MR::getAllLiveActorGroup();
        const s32 n = pGroup->getObjNum();
        for (s32 i = gActorsBefore; i < n; i++) {
            LiveActor* a = pGroup->getActor(i);
            if (!MR::isDead(a)) {
                a->kill();
            }
        }
        std::fprintf(stderr, "[class-sweep] KILLED %d %s\n", gCurrent, rJob.name.c_str());
    }
}  // namespace

namespace ClassSweep {
    s32 findSyntheticZone(uintptr_t rowAddress) {
        for (SyntheticRow* pRow : gRows) {
            uintptr_t begin = reinterpret_cast< uintptr_t >(pRow->bytes.data());
            if (begin <= rowAddress && rowAddress < begin + pRow->bytes.size()) {
                return pRow->zone;
            }
        }
        return -1;
    }

    void update(bool actionNerve) {
        if (!gChecked) {
            gChecked = true;
            const char* pJobs = std::getenv("PETARI_CLASS_SWEEP_JOBS");
            gEnabled = pJobs != nullptr && pJobs[0] != '\0';
            if (const char* p = std::getenv("PETARI_CLASS_SWEEP_OBSERVE")) {
                gObserveFrames = std::atoi(p);
            }
        }
        if (!gEnabled) {
            return;
        }
        if (gFrame < 0) {
            if (!actionNerve || petari_class_sweep_go == 0) {
                return;
            }
            gFrame = 0;
            // Parsed here, not at the first update call: that is an earlier scene whose heap is gone by now.
            parseJobs(std::getenv("PETARI_CLASS_SWEEP_JOBS"));
            std::fprintf(stderr, "[class-sweep] start: first normal-play frame, %d jobs\n", static_cast< int >(gJobs.size()));
        }
        const s32 f = gFrame++;
        if (f < 90) {
            return;  // let Mario settle
        }
        if (MR::isPlayerDead()) {
            finishBatch("ABORT mario-died");
        }
        if (!actionNerve) {
            if (++gNerveLostFrames > 600) {
                finishBatch("ABORT scene-left-normal-play");
            }
            return;
        }
        gNerveLostFrames = 0;

        const Job* pJob = gCurrent >= 0 && gCurrent < static_cast< s32 >(gJobs.size()) ? &gJobs[gCurrent] : nullptr;
        switch (gStep) {
        case Idle: {
            if (gNext >= gJobs.size()) {
                char line[64];
                std::snprintf(line, sizeof(line), "BATCH-DONE %d", static_cast< int >(gJobs.size()));
                finishBatch(line);
            }
            gCurrent = static_cast< s32 >(gNext++);
            const Job& rJob = gJobs[gCurrent];
            std::fprintf(stderr, "[class-sweep] BEGIN %d %s\n", gCurrent, rJob.name.c_str());
            const V mario = playerPos();
            const V up = playerUp();
            const V front = playerFront();
            gSpawnPos = add(add(mario, mul(front, 400.0f)), mul(up, 250.0f));
            gActorsBefore = MR::getAllLiveActorGroup()->getObjNum();
            Created c = spawn(rJob, gSpawnPos);
            char text[1024];
            std::snprintf(text, sizeof(text), "created=%d row=%s zone=%d mount_ms=%.1f create_ms=%.2f archives=%s nonresident=%s tpl_links=%s", c.created,
                          c.cloned ? "cloned" : "synth", static_cast< int >(c.zone), c.mountMs, c.createMs,
                          c.archives.empty() ? "-" : c.archives.c_str(), c.nonResident.empty() ? "-" : c.nonResident.c_str(),
                          c.tplLinks.empty() ? "-" : c.tplLinks.c_str());
            gCreatedText = text;
            std::fprintf(stderr, "[class-sweep] CREATED %d %s created=%d\n", gCurrent, rJob.name.c_str(), c.created);
            {
                // Where did the new actors land against the requested spot (checks the zone conversion).
                AllLiveActorGroup* pGroup = MR::getAllLiveActorGroup();
                double nearest = -1;
                for (s32 i = gActorsBefore; i < pGroup->getObjNum(); i++) {
                    LiveActor* a = pGroup->getActor(i);
                    double d = len(add({a->mPosition.x, a->mPosition.y, a->mPosition.z}, mul(gSpawnPos, -1.0f)));
                    nearest = nearest < 0 ? d : std::fmin(nearest, d);
                }
                std::fprintf(stderr, "[class-sweep] NOTE spawn_dist=%.0f\n", nearest);
            }
            gStep = Observing;
            gStepFrame = 0;
            break;
        }
        case Observing:
            if (++gStepFrame >= gObserveFrames) {
                observe(*pJob);
                killItem(*pJob);
                gStep = Settling;
                gStepFrame = 0;
            }
            break;
        case Settling:
            if (++gStepFrame >= 4) {
                gStep = Idle;
            }
            break;
        }
    }
}  // namespace ClassSweep
#endif
