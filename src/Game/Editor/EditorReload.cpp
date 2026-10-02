#ifdef PETARI_NATIVE
// The level editor's reload from the model (TECH.md 2.6): edited stage archives replace the
// cached ones in the next scene build, through a same-stage restart without the miss.
#include "Game/Editor/EditorScene.hpp"
#include "Game/System/ArchiveHolder.hpp"
#include "Game/System/FileLoader.hpp"
#include "Game/System/GalaxyMoveArgument.hpp"
#include "Game/System/GameSequenceFunction.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/Util/JMapIdInfo.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/SystemUtil.hpp"
#include <JSystem/JKernel/JKRSolidHeap.hpp>
#include <petari/editor/runtime_hooks.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace EditorReload {
    void dropOverriddenArchives() {
        const std::vector< std::string > paths = PetariEditor::Hooks::archiveOverridePaths();
        if (paths.empty()) {
            return;
        }
        FileLoader* pLoader = SingletonHolder< FileLoader >::get();
        for (const std::string& path : paths) {
            // The archive first (it points into the file's bytes), then the file entry.
            pLoader->mArchiveHolder->nativeRemove(path.c_str());
            if (pLoader->isLoaded(path.c_str())) {
                pLoader->removeFile(path.c_str());
            }
        }
    }

    bool injectOverriddenArchive(const char* pPath) {
        const std::vector< u8 >* pBytes = PetariEditor::Hooks::archiveOverride(pPath);
        FileLoader* pLoader = SingletonHolder< FileLoader >::get();
        if (pBytes == nullptr || pLoader->isLoaded(pPath)) {
            return false;
        }
        // ArchiveHolder holds 0x180 and adds unchecked (Good Egg mounts about 350).
        const s32 mounted = pLoader->mArchiveHolder->mEntries.size();
        if (mounted >= pLoader->mArchiveHolder->mEntries.capacity()) {
            std::fprintf(stderr, "[editor] cannot serve %s: the game's %d mounted archives are in use\n", pPath, mounted);
            return false;
        }
        // The file, then its archive, on the scene heap (both go with the scene). A path that is
        // not on the disc (a new archive) is told apart by name in FileHolder::findEntry.
        JKRHeap* pHeap = MR::getSceneHeapGDDR3();
        u8* pData = new (pHeap, 32) u8[pBytes->size()];
        std::memcpy(pData, pBytes->data(), pBytes->size());
        OSLockMutex(&pLoader->mMutex);
        pLoader->mFileHolder->add(pPath, pHeap, pData);
        OSUnlockMutex(&pLoader->mMutex);
        pLoader->createAndAddArchive(pData, pHeap, pPath);
        std::fprintf(stderr, "[editor] %s from the editor (%u bytes; %d of %d archives mounted)\n", pPath,
                     static_cast< unsigned >(pBytes->size()), mounted + 1, pLoader->mArchiveHolder->mEntries.capacity());
        return true;
    }

    void injectOverriddenArchives(const char* const* ppFiles, int count) {
        for (int i = 0; i < count; i++) {
            injectOverriddenArchive(ppFiles[i]);
        }
    }

    void requestReload(bool fromStart) {
        GameSystemSceneController* pController = SingletonHolder< GameSystem >::get()->mSceneController;
        JMapIdInfo info = fromStart ? MR::getInitializeStartIdInfo() : *MR::getPlayerRestartIdInfo();
        GalaxyMoveArgument argument(5, pController->mCurrSceneControlInfo.mStage, pController->getCurrentScenarioNo(), &info);
        argument._C = pController->getCurrentSelectedScenarioNo();
        GameSequenceFunction::requestGalaxyMove(argument);
    }

    void teleportPlayer(f32 x, f32 y, f32 z) {
        MR::setPlayerPosAndWait(TVec3f(x, y, z));
    }
}  // namespace EditorReload
#endif
