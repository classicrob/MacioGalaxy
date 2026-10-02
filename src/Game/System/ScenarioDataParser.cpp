#include "Game/System/ScenarioDataParser.hpp"
#include "Game/System/GalaxyNameSortTable.hpp"
#include "Game/System/GalaxyStatusAccessor.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/JMapInfo.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/StringUtil.hpp"
#include <JSystem/JKernel/JKRMemArchive.hpp>
#include <algorithm>
#ifdef PETARI_NATIVE
#include "Game/System/GameDataConst.hpp"
#include <cstdio>
#include <petari/platform/mod_galaxy.hpp>
#endif

template const bool JMapInfo::getValue< s32 >(int, const char*, s32*) const NO_INLINE;

namespace {
    inline const char* getGalaxyName(const ScenarioData* pData) {
        return pData->mGalaxyName;
    }

    struct GalaxyNameSortLt {
        bool operator()(ScenarioData* pLhs, ScenarioData* pRhs) {
            return static_cast< u32 >(GalaxyNameSortTable::getGalaxySortIndex(getGalaxyName(pLhs))) <
                   static_cast< u32 >(GalaxyNameSortTable::getGalaxySortIndex(getGalaxyName(pRhs)));
        }
    };

    const ScenarioData* getCurrentScenarioData() {
        return ScenarioDataFunction::getScenarioDataParser()->getScenarioData(MR::getCurrentStageName());
    }
};  // namespace

ScenarioData::ScenarioData(const char* pFilePath) : mScenarioData(), mGalaxyName(), mZoneList() {
    char fileName[64];
    MR::removeExtensionString(fileName, sizeof(fileName), MR::getBasename(pFilePath));

    const char* pSuffix = "Scenario";
    char* pGalaxyName = strstr(fileName, pSuffix);

    if (pGalaxyName != nullptr) {
        pGalaxyName[0] = '\0';
    }

    int galaxyNameSize = strlen(fileName) + 1;

    mGalaxyName = new char[galaxyNameSize];
    MR::copyString(mGalaxyName, fileName, galaxyNameSize);

    JKRMemArchive* pArchive = static_cast< JKRMemArchive* >(MR::receiveArchive(pFilePath));

    mScenarioData = new JMapInfo();
    mScenarioData->attach(pArchive->getResource("/ScenarioData.bcsv"));

    mZoneList = new JMapInfo();
    mZoneList->attach(pArchive->getResource("/ZoneList.bcsv"));
}

s32 ScenarioData::getScenarioNum() const {
    s32 count = 0;
    for (s32 scenarioNo = 1; scenarioNo <= mScenarioData->getNumEntries(); scenarioNo++) {
        bool hidden = false;
        getValueBool("IsHidden", scenarioNo, &hidden);
        if (!hidden) {
            count++;
        }
    }

    return count;
}

s32 ScenarioData::getPowerStarNum() const {
    s32 count = 0;
    for (s32 scenarioNo = 1; scenarioNo <= mScenarioData->getNumEntries(); scenarioNo++) {
        u32 powerStarId = 0;
        getValueU32("PowerStarId", scenarioNo, &powerStarId);
        if (powerStarId != 0) {
            count++;
        }
    }

    return count;
}

bool ScenarioData::getValueString(const char* pKey, s32 scenarioNo, const char** ppOut) const {
    JMapInfoIter iter = getScenarioDataIter(scenarioNo);
    bool isExist = iter.getValue(pKey, ppOut);

    if (isExist) {
        if (MR::isEqualString(*ppOut, "")) {
            *ppOut = nullptr;
        }
    }

    return isExist;
}

const char* ScenarioData::getZoneName(s32 zoneId) const {
    const char* pZoneName = nullptr;
    mZoneList->getValue< const char* >(zoneId, "ZoneName", &pZoneName);

    return pZoneName;
}

JMapInfoIter ScenarioData::getScenarioDataIter(s32 scenarioNo) const {
    JMapInfoIter iter = mScenarioData->findElement< s32 >("ScenarioNo", scenarioNo, 0);
    return iter;
}

bool ScenarioData::getValueU32(const char* pKey, s32 scenarioNo, u32* pOut) const {
    JMapInfoIter iter = getScenarioDataIter(scenarioNo);
    return iter.getValue(pKey, pOut);
}

bool ScenarioData::getValueBool(const char* pKey, s32 scenarioNo, bool* pOut) const {
    JMapInfoIter iter = getScenarioDataIter(scenarioNo);
    return iter.getValue(pKey, pOut);
}

s32 ScenarioData::getZoneNum() const {
    if (mZoneList->mData != nullptr) {
        return mZoneList->mData->mNumEntries;
    }

    return 0;
}

s32 ScenarioData::getZoneId(const char* pZoneName) const {
    for (s32 i = 0; i < getZoneNum(); i++) {
        const char* pName = nullptr;
        mZoneList->getValue< const char* >(i, "ZoneName", &pName);

        if (MR::isEqualStringCase(pName, pZoneName)) {
            return i;
        }
    }

    return 0;
}

ScenarioDataIter::ScenarioDataIter(const ScenarioDataParser* pParser, int param2) : mParser(pParser), mCur(param2) {
}

bool ScenarioDataIter::isEnd() const {
    return mParser->mScenarioData.size() <= mCur;
}

void ScenarioDataIter::goNext() {
    if (!isEnd()) {
        mCur++;
    }
}

GalaxyStatusAccessor ScenarioDataIter::makeAccessor() const {
    return GalaxyStatusAccessor(mParser->getScenarioData(mCur));
}

#ifdef PETARI_NATIVE
namespace {
    // The disc's galaxies: GalaxyID's rows, and FileSelect (the only disc Scenario archive
    // without a row), so a vanilla disc parses exactly as before.
    bool isDiscGalaxy(const char* pName) {
        if (MR::isEqualString(pName, "FileSelect")) {
            return true;
        }
        JMapInfo info;
        info.attach(&GalaxyIDBCSV);
        return info.findElement< const char* >("name", pName, 0).isValid();
    }
}  // namespace

bool ScenarioDataParser::nativeAcceptGalaxy(const char* pName) {
    namespace ModGalaxy = PetariNative::Platform::ModGalaxy;
    if (isDiscGalaxy(pName)) {
        return true;
    }
    if (ModGalaxy::find(pName) == nullptr) {
        std::fprintf(stderr, "[mods] galaxy %s skipped: not a disc galaxy and not in any enabled mod's galaxy.json\n", pName);
        return false;
    }
    // The save keys galaxies by a 16-bit hash of the name: a mod galaxy sharing a disc galaxy's
    // key would read and overwrite that galaxy's record.
    JMapInfo info;
    info.attach(&GalaxyIDBCSV);
    for (s32 i = 0; i < info.getNumEntries(); i++) {
        const char* pDisc = nullptr;
        if (info.getValue< const char* >(i, "name", &pDisc) && pDisc != nullptr && ModGalaxy::saveKey(pDisc) == ModGalaxy::saveKey(pName)) {
            char reason[128];
            std::snprintf(reason, sizeof(reason), "its save key collides with %s's; rename it", pDisc);
            ModGalaxy::reject(pName, reason);
            return false;
        }
    }
    return true;
}
#endif

#pragma push
#pragma opt_propagation off
ScenarioDataParser::ScenarioDataParser(const char* pName) : NameObj(pName), mScenarioData() {
    DVDDir dir;
    DVDDirEntry dirent;
    DVDOpenDir("/StageData", &dir);

    while (DVDReadDir(&dir, &dirent)) {
        if (!dirent.isDir) {
            continue;
        }

        char fileName[256];
        MR::makeScenarioArchiveFileName(fileName, sizeof(fileName), dirent.name);

        if (!MR::isFileExist(fileName, false)) {
            continue;
        }

#ifdef PETARI_NATIVE
        // TECH.md 5.7: a galaxy the executable has no data for (not in the compiled-in GalaxyID
        // table) breaks every player once parsed: event flags, names and the next stage after any
        // Power Star are looked up for each parsed galaxy. Only the mod galaxy registry's are
        // taken; others (e.g. a Whitehole mod's new galaxy without galaxy.json) are skipped.
        if (!nativeAcceptGalaxy(dirent.name)) {
            continue;
        }
        if (mScenarioData.mCount >= 64) {  // FixedArray< ScenarioData*, 64 >, unchecked on the Wii
            std::fprintf(stderr, "[mods] galaxy %s skipped: the game holds at most 64 galaxies\n", dirent.name);
            continue;
        }
#endif
        ScenarioData* pData = new ScenarioData(fileName);

        u32 index = mScenarioData.mCount;
        u32 nextCount = index;
        nextCount++;

        mScenarioData.mArray[index] = pData;
        mScenarioData.mCount = nextCount;
    }

    DVDCloseDir(&dir);

    std::sort(mScenarioData.begin(), mScenarioData.end(), GalaxyNameSortLt());
}
#pragma pop

const ScenarioData* ScenarioDataParser::getScenarioData(const char* pGalaxyName) const {
    for (s32 i = 0; i < mScenarioData.size(); i++) {
        const ScenarioData* pScenarioData = mScenarioData[i];

        if (MR::isEqualStringCase(pScenarioData->mGalaxyName, pGalaxyName)) {
            return pScenarioData;
        }
    }

    return nullptr;
}

const ScenarioData* ScenarioDataParser::getScenarioData(s32 index) const {
    return mScenarioData[index];
}

GalaxyStatusAccessor ScenarioDataParser::makeAccessor(const char* pGalaxyName) const {
    return GalaxyStatusAccessor(getScenarioData(pGalaxyName));
}

namespace ScenarioDataFunction {
    ScenarioDataParser* getScenarioDataParser() NO_INLINE {
        return SingletonHolder< GameSystem >::get()->mSceneController->mScenarioParser;
    }

    u32 getCurrentCommonLayers(const char* pParam1) {
        ::getCurrentScenarioData();

        return 1;
    }

    u32 getCurrentScenarioLayers(const char* pParam1, s32 param2) {
        u32 layerNum;

        if (::getCurrentScenarioData()->getValueU32(pParam1, param2, &layerNum)) {
            return layerNum * 2;
        }

        return 0;
    }
};  // namespace ScenarioDataFunction

namespace MR {
    ScenarioDataIter makeBeginScenarioDataIter() {
        return ScenarioDataIter(ScenarioDataFunction::getScenarioDataParser(), 0);
    }
};  // namespace MR
