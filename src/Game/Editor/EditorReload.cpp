#ifdef PETARI_NATIVE
// The level editor's reload from the model (TECH.md 2.6): edited stage archives replace the
// cached ones in the next scene build, through a same-stage restart without the miss.
#include "Game/Editor/EditorScene.hpp"
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

    void injectOverriddenArchives(const char* const* ppFiles, int count) {
        FileLoader* pLoader = SingletonHolder< FileLoader >::get();
        JKRHeap* pHeap = MR::getSceneHeapGDDR3();
        for (int i = 0; i < count; i++) {
            const std::vector< u8 >* pBytes = PetariEditor::Hooks::archiveOverride(ppFiles[i]);
            if (pBytes == nullptr || pLoader->isLoaded(ppFiles[i])) {
                continue;
            }
            u8* pData = new (pHeap, 32) u8[pBytes->size()];
            std::memcpy(pData, pBytes->data(), pBytes->size());
            OSLockMutex(&pLoader->mMutex);
            pLoader->mFileHolder->add(ppFiles[i], pHeap, pData);
            OSUnlockMutex(&pLoader->mMutex);
            pLoader->createAndAddArchive(pData, pHeap, ppFiles[i]);
            std::fprintf(stderr, "[editor] reload: %s from the edit (%u bytes)\n", ppFiles[i], static_cast< unsigned >(pBytes->size()));
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
