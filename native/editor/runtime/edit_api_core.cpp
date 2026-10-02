// The session state behind edit_api.hpp: the command queue, mode, UI hooks, status and the
// row <-> actor binding. The EditModel and SceneSync plug in through runtime_core.hpp.
#include "runtime_core.hpp"

#include <unordered_map>

namespace PetariEditor {
namespace {
    struct Session {
        const ModelView* model = nullptr;
        std::vector< Command > commands;
        Mode mode = Mode::Off;
        void (*uiSeamHook)() = nullptr;
        UiCallbacks callbacks;
        Status status;
        std::unordered_map< RowId, LiveActor*, RowIdHash > actorOfRow;
        std::unordered_map< const LiveActor*, RowId > rowOfActor;
    };

    Session& session() {
        static Session sSession;
        return sSession;
    }
}  // namespace

std::uint32_t columnHash(const char* pName) {
    std::uint32_t hash = 0;
    for (; *pName != '\0'; pName++) {
        hash = static_cast< std::uint32_t >(static_cast< signed char >(*pName)) + hash * 31;
    }
    return hash;
}

const ModelView* model() {
    return session().model;
}

void enqueue(Command&& command) {
    session().commands.push_back(std::move(command));
}

void setUiSeamHook(void (*hook)()) {
    session().uiSeamHook = hook;
}

LiveActor* actorForRow(RowId row) {
    auto it = session().actorOfRow.find(row);
    return it != session().actorOfRow.end() ? it->second : nullptr;
}

RowId rowForActor(const LiveActor* pActor) {
    auto it = session().rowOfActor.find(pActor);
    return it != session().rowOfActor.end() ? it->second : RowId{};
}

void forEachBoundRow(const std::function< void(RowId, LiveActor*) >& fn) {
    for (const auto& [row, actor] : session().actorOfRow) {
        fn(row, actor);
    }
}

Mode mode() {
    return session().mode;
}

void setUiCallbacks(const UiCallbacks& callbacks) {
    session().callbacks = callbacks;
}

void notifyEditEntered() {
    if (session().mode == Mode::Off || session().mode == Mode::Playing) {
        session().mode = Mode::Editing;
    }
}

void notifyEditLeft() {
    if (session().mode == Mode::Editing) {
        session().mode = Mode::Off;
    }
}

const Status& status() {
    return session().status;
}

namespace Core {
    void setModel(const ModelView* pModel) {
        session().model = pModel;
    }

    std::vector< Command > drainCommands() {
        std::vector< Command > out;
        out.swap(session().commands);
        return out;
    }

    void setMode(Mode mode) {
        session().mode = mode;
    }

    Status& mutableStatus() {
        return session().status;
    }

    void reportError(const std::string& message) {
        session().status.lastError = message;
        session().status.errorSerial++;
    }

    void runUiSeamHook() {
        if (session().uiSeamHook != nullptr) {
            session().uiSeamHook();
        }
    }

    const UiCallbacks& uiCallbacks() {
        return session().callbacks;
    }

    void bindActor(RowId row, LiveActor* pActor) {
        unbindRow(row);
        auto previous = session().rowOfActor.find(pActor);
        if (previous != session().rowOfActor.end()) {
            session().actorOfRow.erase(previous->second);
        }
        session().actorOfRow[row] = pActor;
        session().rowOfActor[pActor] = row;
    }

    void unbindRow(RowId row) {
        auto it = session().actorOfRow.find(row);
        if (it != session().actorOfRow.end()) {
            session().rowOfActor.erase(it->second);
            session().actorOfRow.erase(it);
        }
    }

    void clearBindings() {
        session().actorOfRow.clear();
        session().rowOfActor.clear();
    }
}  // namespace Core

}  // namespace PetariEditor
