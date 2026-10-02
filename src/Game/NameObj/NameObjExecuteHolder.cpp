#include "Game/NameObj/NameObjExecuteHolder.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/NameObj/NameObjListExecutor.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#ifdef PETARI_NATIVE
#include "Game/System/DrawBuffer.hpp"
#include "Game/System/DrawBufferExecuter.hpp"
#include "Game/System/DrawBufferGroup.hpp"
#include "Game/System/DrawBufferHolder.hpp"
#endif

namespace {
    NameObjExecuteHolder* getNameObjExecuteHolder() {
        return MR::getSceneObj< NameObjExecuteHolder >(SceneObj_NameObjExecuteHolder);
    }
};  // namespace

NameObjExecuteInfo::NameObjExecuteInfo()
    : mExecutedObj(), _4(), _5(), mMovementType(-1), mCalcAnimType(-1), mDrawType(-1), mDrawBufferType(-1), _A(-1) {
}

void NameObjExecuteInfo::setConnectInfo(NameObj* pObj, int movementType, int calcAnimType, int drawBufferType, int drawType) {
    _4 = 2;
    mExecutedObj = pObj;
    _5 = 2;
    mMovementType = movementType;
    mCalcAnimType = calcAnimType;
    mDrawBufferType = drawBufferType;
    mDrawType = drawType;
    NameObjListExecutor* pListExecutor = SingletonHolder< GameSystem >::get()->mSceneController->getNameObjListExecutor();

    if (drawBufferType != -1) {
        _A = pListExecutor->registerDrawBuffer(static_cast< LiveActor* >(pObj), drawBufferType);
    }

    if (movementType != -1) {
        pListExecutor->incrementCheckMovement(pObj, movementType);
    }

    if (calcAnimType != -1) {
        pListExecutor->incrementCheckCalcAnim(pObj, calcAnimType);
    }

    if (drawType != -1) {
        pListExecutor->incrementCheckDraw(pObj, drawType);
    }
}

void NameObjExecuteInfo::initConnectting() {
    if (_4 == 1) {
        _4 = 4;
        connectToScene();
    } else {
        _4 = 5;
    }

    if (_5 == 1) {
        _5 = 4;
        connectToDraw();
    } else {
        _5 = 5;
    }
}

void NameObjExecuteInfo::requestConnect(u8* pData) {
    switch (*pData) {
    case 2:
        *pData = 1;
        break;
    case 5:
        *pData = 4;
        break;
    case 6:
    case 7:
        *pData = 3;
        break;
    case 1:
        return;
    }
}

void NameObjExecuteInfo::requestDisconnect(u8* pData, bool a2) {
    u8 val;

    switch (*pData) {
    case 1:
        *pData = 2;
        break;
    case 3:
        val = 6;
        if (a2) {
            val = 7;
        }

        *pData = val;
        break;
    case 5:
    case 6:
    case 7:
        break;
    case 4:
        *pData = 5;
        break;
    case 2:
        return;
    }
}

void NameObjExecuteInfo::executeRequirementConnectMovement() {
    if (_4 == 4) {
        connectToScene();
    }
}

void NameObjExecuteInfo::executeRequirementDisconnectMovement() {
    if (_4 == 6) {
        disconnectToScene();
    }
}

void NameObjExecuteInfo::executeRequirementConnectDraw() {
    if (_5 == 4) {
        connectToDraw();
    }
}

void NameObjExecuteInfo::executeRequirementDisconnectDraw() {
    if (_5 == 6) {
        disconnectToDraw();
    }
}

void NameObjExecuteInfo::executeRequirementDisconnectDrawDelay() {
    if (_5 == 7) {
        disconnectToDraw();
    }
}

void NameObjExecuteInfo::requestMovementOn(int a1) {
    if (mMovementType != static_cast< s8 >(a1)) {
        return;
    }

    MR::requestMovementOn(mExecutedObj);
}

void NameObjExecuteInfo::requestMovementOff(int a1) {
    if (mMovementType != static_cast< s8 >(a1)) {
        return;
    }

    MR::requestMovementOff(mExecutedObj);
}

void NameObjExecuteInfo::findLightInfo() const {
    SingletonHolder< GameSystem >::get()->mSceneController->getNameObjListExecutor()->findLightInfo(static_cast< LiveActor* >(mExecutedObj),
                                                                                                    mDrawBufferType, _A);
}

void NameObjExecuteInfo::connectToScene() {
    _4 = 3;
    NameObjListExecutor* pListExecutor = SingletonHolder< GameSystem >::get()->mSceneController->getNameObjListExecutor();

    if (mMovementType != -1) {
        pListExecutor->addToMovement(mExecutedObj, mMovementType);
    }

    if (mCalcAnimType != -1) {
        pListExecutor->addToCalcAnim(mExecutedObj, mCalcAnimType);
    }
}

void NameObjExecuteInfo::disconnectToScene() {
    _4 = 5;
    NameObjListExecutor* pListExecutor = SingletonHolder< GameSystem >::get()->mSceneController->getNameObjListExecutor();

    if (mMovementType != -1) {
        pListExecutor->removeToMovement(mExecutedObj, mMovementType);
    }

    if (mCalcAnimType != -1) {
        pListExecutor->removeToCalcAnim(mExecutedObj, mCalcAnimType);
    }
}

void NameObjExecuteInfo::connectToDraw() {
    _5 = 3;
    NameObjListExecutor* pListExecutor = SingletonHolder< GameSystem >::get()->mSceneController->getNameObjListExecutor();

    if (mDrawType != -1) {
        pListExecutor->addToDraw(mExecutedObj, mDrawType);
    }

    if (mDrawBufferType != -1) {
        pListExecutor->addToDrawBuffer(static_cast< LiveActor* >(mExecutedObj), mDrawBufferType, _A);
    }
}

void NameObjExecuteInfo::disconnectToDraw() {
    _5 = 5;
    NameObjListExecutor* pListExecutor = SingletonHolder< GameSystem >::get()->mSceneController->getNameObjListExecutor();

    if (mDrawType != -1) {
        pListExecutor->removeToDraw(mExecutedObj, mDrawType);
    }

    if (mDrawBufferType != -1) {
        pListExecutor->removeToDrawBuffer(static_cast< LiveActor* >(mExecutedObj), mDrawBufferType, _A);
    }
}

void NameObjExecuteHolder::registerActor(NameObj* pObj, int movementType, int calcAnimType, int drawBufferType, int drawType) {
    mExecuteArray[mExecuteArraySize].setConnectInfo(pObj, movementType, calcAnimType, drawBufferType, drawType);
    pObj->mExecutorIdx = mExecuteArraySize;
    mExecuteArraySize++;
}

void NameObjExecuteHolder::initConnectting() {
    for (s32 i = 0; i < mExecuteArraySize; i++) {
        mExecuteArray[i].initConnectting();
    }
}

void NameObjExecuteHolder::connectToScene(NameObj* pObj) {
    NameObjExecuteInfo* pInfo = getConnectToSceneInfo(pObj);

    if (pInfo == nullptr) {
        return;
    }

    pInfo->requestConnect(reinterpret_cast< u8* >(&pInfo->_4));

    _18 = true;
}

void NameObjExecuteHolder::connectToDraw(NameObj* pObj) {
    NameObjExecuteInfo* pInfo = getConnectToSceneInfo(pObj);

    if (pInfo == nullptr) {
        return;
    }

    pInfo->requestConnect(reinterpret_cast< u8* >(&pInfo->_5));

    _1A = true;
}

void NameObjExecuteHolder::disconnectToScene(NameObj* pObj) {
    NameObjExecuteInfo* pInfo = getConnectToSceneInfo(pObj);

    if (pInfo == nullptr) {
        return;
    }

    pInfo->requestDisconnect(reinterpret_cast< u8* >(&pInfo->_4), false);

    _19 = true;
}

void NameObjExecuteHolder::disconnectToDraw(NameObj* pObj) {
    NameObjExecuteInfo* pInfo = getConnectToSceneInfo(pObj);

    if (pInfo == nullptr) {
        return;
    }

    pInfo->requestDisconnect(reinterpret_cast< u8* >(&pInfo->_5), false);

    _1B = true;
}

bool NameObjExecuteHolder::isConnectToDraw(const NameObj* pObj) const {
    NameObjExecuteInfo* pInfo = getConnectToSceneInfo(pObj);
    bool ret = pInfo != nullptr && pInfo->_5 == 3;

    return ret;
}

void NameObjExecuteHolder::executeRequirementConnectMovement() {
    if (!_18) {
        return;
    }

    for (s32 i = 0; i < mExecuteArraySize; i++) {
        mExecuteArray[i].executeRequirementConnectMovement();
    }

    _18 = false;
}

void NameObjExecuteHolder::executeRequirementDisconnectMovement() {
    if (!_19) {
        return;
    }

    for (s32 i = 0; i < mExecuteArraySize; i++) {
        mExecuteArray[i].executeRequirementDisconnectMovement();
    }

    _19 = false;
}

void NameObjExecuteHolder::executeRequirementConnectDraw() {
    if (!_1A) {
        return;
    }

    for (s32 i = 0; i < mExecuteArraySize; i++) {
        mExecuteArray[i].executeRequirementConnectDraw();
    }

    _1A = false;
}

void NameObjExecuteHolder::executeRequirementDisconnectDraw() {
    if (!_1B) {
        return;
    }

    for (s32 i = 0; i < mExecuteArraySize; i++) {
        mExecuteArray[i].executeRequirementDisconnectDraw();
    }

    _1B = false;
}

void NameObjExecuteHolder::executeRequirementDisconnectDrawDelay() {
    if (!_1C) {
        return;
    }

    for (s32 i = 0; i < mExecuteArraySize; i++) {
        mExecuteArray[i].executeRequirementDisconnectDrawDelay();
    }

    _1C = false;
}

void NameObjExecuteHolder::requestMovementOn(int idx) {
    for (s32 i = 0; i < mExecuteArraySize; i++) {
        mExecuteArray[i].requestMovementOn(idx);
    }
}

void NameObjExecuteHolder::requestMovementOff(int idx) {
    for (s32 i = 0; i < mExecuteArraySize; i++) {
        mExecuteArray[i].requestMovementOff(idx);
    }
}

NameObjExecuteInfo* NameObjExecuteHolder::getConnectToSceneInfo(const NameObj* pObj) const {
    if (pObj->mExecutorIdx >= 0) {
        return &mExecuteArray[pObj->mExecutorIdx];
    }

    return nullptr;
}

namespace MR {
    void registerNameObjToExecuteHolder(NameObj* pObj, int movementType, int calcAnimType, int drawBufferType, int drawType) {
        ::getNameObjExecuteHolder()->registerActor(pObj, movementType, calcAnimType, drawBufferType, drawType);
    }

    void initConnectting() {
        ::getNameObjExecuteHolder()->initConnectting();
    }

    void connectToSceneTemporarily(NameObj* pObj) {
        ::getNameObjExecuteHolder()->connectToScene(pObj);
    }

    void disconnectToSceneTemporarily(NameObj* pObj) {
        ::getNameObjExecuteHolder()->disconnectToScene(pObj);
    }

    void connectToDrawTemporarily(NameObj* pObj) {
        ::getNameObjExecuteHolder()->connectToDraw(pObj);
    }

    void disconnectToDrawTemporarily(NameObj* pObj) {
        ::getNameObjExecuteHolder()->disconnectToDraw(pObj);
    }

    bool isConnectToDrawTemporarily(const NameObj* pObj) {
        return ::getNameObjExecuteHolder()->isConnectToDraw(pObj);
    }

    void executeRequirementConnectMovement() {
        ::getNameObjExecuteHolder()->executeRequirementConnectMovement();
    }

    void executeRequirementDisconnectMovement() {
        ::getNameObjExecuteHolder()->executeRequirementDisconnectMovement();
    }

    void executeRequirementConnectDraw() {
        ::getNameObjExecuteHolder()->executeRequirementConnectDraw();
    }

    void executeRequirementDisconnectDraw() {
        ::getNameObjExecuteHolder()->executeRequirementDisconnectDraw();
    }

    void executeRequirementDisconnectDrawDelay() {
        ::getNameObjExecuteHolder()->executeRequirementDisconnectDrawDelay();
    }

    void requestMovementOnWithCategory(int category) {
        ::getNameObjExecuteHolder()->requestMovementOn(category);
    }

    void requestMovementOffWithCategory(int category) {
        ::getNameObjExecuteHolder()->requestMovementOff(category);
    }

    void findActorLightInfo(const LiveActor* pActor) {
        ::getNameObjExecuteHolder()->getConnectToSceneInfo(pActor)->findLightInfo();
    }
};  // namespace MR

NameObjExecuteHolder::NameObjExecuteHolder(int size)
    : NameObj("connectToScene情報保持"), mExecuteArray(), mExecuteArrayMaxSize(size), mExecuteArraySize(), _18(), _19(), _1A(), _1B(), _1C() {
    mExecuteArray = new NameObjExecuteInfo[mExecuteArrayMaxSize];
}

#ifdef PETARI_NATIVE
namespace {
    // Room for every object counted into a category. The old array stays in the solid scene heap.
    void growCategory(NameObjCategoryList* pList, int category) {
        NameObjCategoryList::CategoryInfo& rInfo = pList->mCategoryInfo[category];
        MR::AssignableArray< NameObj* >& rArray = rInfo.mNameObjArr.mArray;
        s32 need = rInfo.mNameObjArr.size() + 1;
        if (static_cast< s32 >(rInfo.mCheck) > need) {
            need = rInfo.mCheck;
        }
        if (need <= rArray.mMaxSize) {
            return;
        }
        const s32 newMax = need + need / 2 + 16;
        NameObj** pNew = new NameObj*[newMax];
        for (s32 i = 0; i < rInfo.mNameObjArr.size(); i++) {
            pNew[i] = rArray.mArr[i];
        }
        rArray.mArr = pNew;
        rArray.mMaxSize = newMax;
    }

    // A draw executor created after load has no lists; one that outgrew its headroom grows.
    void growExecuter(DrawBufferExecuter* pExec) {
        if (pExec->mActors == nullptr) {
            pExec->allocateActorListBuffer();
            return;
        }
        if (pExec->mDrawBufferCount <= pExec->mMaxNumActors) {
            return;
        }
        const s32 newMax = pExec->mDrawBufferCount + pExec->mDrawBufferCount / 2 + 4;
        LiveActor** pActors = new LiveActor*[newMax];
        for (s32 i = 0; i < pExec->mNumActors; i++) {
            pActors[i] = pExec->mActors[i];
        }
        pExec->mActors = pActors;
        pExec->mMaxNumActors = newMax;
        DrawBuffer* pBuffer = pExec->mDrawBuffer;
        pBuffer->_8 = newMax;
        for (s32 d = 0; d < pBuffer->mNumShapeDrawers; d++) {
            DrawBufferShapeDrawer* pDrawer = pBuffer->mShapeDrawers[d];
            const s32 newPackets = newMax * pDrawer->_8;
            PacketInfo** pPackets = new PacketInfo*[newPackets];
            for (s32 i = 0; i < newPackets; i++) {
                if (i < pDrawer->mMaxPackets) {
                    pPackets[i] = pDrawer->mPackets[i];
                } else {
                    pPackets[i] = new PacketInfo();
                    pPackets[i]->mShapePacket = nullptr;
                    pPackets[i]->mLightLoader = nullptr;
                    pPackets[i]->mLightCtrl = nullptr;
                }
            }
            pDrawer->mPackets = pPackets;
            pDrawer->mMaxPackets = newPackets;
        }
    }
}  // namespace

void NameObjExecuteHolder::nativeLateConnect(s32 from) {
    NameObjListExecutor* pListExecutor = SingletonHolder< GameSystem >::get()->mSceneController->getNameObjListExecutor();
    for (s32 i = from; i < mExecuteArraySize; i++) {
        NameObjExecuteInfo& rInfo = mExecuteArray[i];
        if (rInfo.mMovementType != -1) {
            growCategory(pListExecutor->mMovementList, rInfo.mMovementType);
        }
        if (rInfo.mCalcAnimType != -1) {
            growCategory(pListExecutor->mCalcAnimList, rInfo.mCalcAnimType);
        }
        if (rInfo.mDrawType != -1) {
            growCategory(pListExecutor->mDrawList, rInfo.mDrawType);
        }
        if (rInfo.mDrawBufferType != -1) {
            growExecuter(pListExecutor->mBufferHolder->mBufferGroups[rInfo.mDrawBufferType].mExecutors[rInfo._A]);
        }
        rInfo.initConnectting();
    }
}
#endif
