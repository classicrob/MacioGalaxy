#include "Game/System/FileHolder.hpp"
#include "Game/Util.hpp"

#define FILE_HOLDER_STATE_NONE 0
#define FILE_HOLDER_STATE_READING 1
#define FILE_HOLDER_STATE_DONE 2

FileHolderFileEntry::FileHolderFileEntry(const char* pFilePath, JKRHeap* pHeap, void* pContext)
    : mEntryNum(DVDConvertPathToEntrynum(pFilePath)), mContext(nullptr), mHeap(pHeap), mState(FILE_HOLDER_STATE_NONE), mContextSet(true) {
    OSInitMessageQueue(&mQueue, &mMessage, 1);
#ifdef PETARI_NATIVE
    mNativePath = nullptr;
    if (mEntryNum < 0 && pFilePath != nullptr) {
        const s32 length = strlen(pFilePath) + 1;
        mNativePath = new (pHeap, 0) char[length];
        MR::copyString(mNativePath, pFilePath, length);
    }
#endif

    if (pContext) {
        mContextSet = false;
        mContext = pContext;
    }
}

FileHolderFileEntry::~FileHolderFileEntry() {
    if (mContextSet && (mContext != nullptr)) {
        delete (u8*)mContext;
    }
#ifdef PETARI_NATIVE
    delete[] mNativePath;
#endif
}

void FileHolderFileEntry::waitReadDone() {
    if (mState != FILE_HOLDER_STATE_DONE) {
        OSMessage msg;
        OSReceiveMessage(&mQueue, &msg, OS_MESSAGE_BLOCK);
        mState = FILE_HOLDER_STATE_DONE;
    }
}

void FileHolderFileEntry::setContext(void* pContext, JKRHeap* pHeap) {
    mContext = pContext;
    mHeap = pHeap;

    OSSendMessage(&mQueue, nullptr, OS_MESSAGE_NOBLOCK);
    mState = FILE_HOLDER_STATE_READING;
}

FileHolder::FileHolder() {
    mEntries.init(0x180);
}

FileHolderFileEntry* FileHolder::add(const char* pFilePath, JKRHeap* pHeap, void* pContext) {
    FileHolderFileEntry* pEntry = new (pHeap, 0) FileHolderFileEntry(pFilePath, pHeap, pContext);
    mEntries.push_back(pEntry);
    return pEntry;
}

bool FileHolder::isExist(const char* pFilePath) const {
    return findEntry(pFilePath);
}

void* FileHolder::getContext(const char* pFilePath) const {
    return findEntry(pFilePath)->mContext;
}

void FileHolder::removeIfIsEqualHeap(JKRHeap* pHeap) {
    if (pHeap == nullptr) {
        return;
    }

    for (FileHolderFileEntry** i = mEntries.begin(); i != mEntries.end();) {
        if ((*i)->mHeap == pHeap || MR::getHeapNapa((*i)->mHeap) == pHeap || MR::getHeapGDDR3((*i)->mHeap) == pHeap) {
            delete *i;
            mEntries.erase(i);
        } else {
            i++;
        }
    }
}

FileHolderFileEntry** FileHolder::removeFile(const char* pFilePath) {
    FileHolderFileEntry** p;
    FileHolderFileEntry* pEntry = findEntry(pFilePath);

    for (p = mEntries.begin(); p != mEntries.end() && *p != pEntry; p++)
        ;
    delete *p;

    return mEntries.erase(p);
}

FileHolderFileEntry* FileHolder::findEntry(const char* pFilePath) const {
    s32 entryNum = DVDConvertPathToEntrynum(pFilePath);
    for (FileHolderFileEntry* const* pEntry = mEntries.begin(); pEntry != mEntries.end(); pEntry++) {
#ifdef PETARI_NATIVE
        // Every path that is not on the disc has entry number -1: match those by name, or the
        // first such file would answer for all of them (TECH.md 2.5).
        if (entryNum < 0) {
            if ((*pEntry)->mEntryNum < 0 && (*pEntry)->mNativePath != nullptr && MR::isEqualStringCase((*pEntry)->mNativePath, pFilePath)) {
                return *pEntry;
            }
            continue;
        }
#endif
        if (entryNum == (*pEntry)->mEntryNum) {
            return *pEntry;
        }
    }

    return nullptr;
}
