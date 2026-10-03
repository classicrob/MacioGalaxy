// Tests for the level editor's UI <-> Runtime contract (petari/editor/edit_api.hpp): the
// column hash matches the game's, commands drain in order, the mode handshake, the UI seam
// hook, the row <-> actor binding and error reporting. No game, no retail data.

#include "runtime_core.hpp"
#include <petari/editor/runtime_hooks.hpp>

#include <cstdio>
#include <cstdlib>

namespace E = PetariEditor;

namespace {
int checks = 0;
void check(bool condition, const char* label) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        std::exit(1);
    }
}

int hookCalls = 0;
void hook() {
    ++hookCalls;
}

void testColumnHash() {
    // Values from native/tools/stage_edit.py jmap_hash (the game's JGadget::getHashCode).
    check(E::columnHash("name") == 0x337a8bu, "hash name");
    check(E::columnHash("pos_x") == 0x65e794du, "hash pos_x");
    check(E::columnHash("Obj_arg0") == 0x8e9c302u, "hash Obj_arg0");
    check(E::columnHash("l_id") == 0x3289ceu, "hash l_id");
    check(E::columnHash("") == 0u, "hash empty");
}

void testQueue() {
    check(E::model() == nullptr, "no model before a session");
    E::Command first{E::Command::Kind::Apply};
    first.op.label = "Move Goomba";
    first.op.sets.push_back({E::RowId{7}, E::columnHash("pos_x"), E::Cell{E::CellType::Float, 0, 12.5f, {}}});
    E::enqueue(std::move(first));
    E::enqueue(E::Command{E::Command::Kind::Undo});
    E::Command save{E::Command::Kind::SaveMod};
    save.modName = "CoinRing";
    E::enqueue(std::move(save));

    std::vector< E::Command > drained = E::Core::drainCommands();
    check(drained.size() == 3, "three commands drained");
    check(drained[0].kind == E::Command::Kind::Apply && drained[0].op.label == "Move Goomba", "first in order");
    check(drained[0].op.sets.size() == 1 && drained[0].op.sets[0].row == E::RowId{7}, "op payload kept");
    check(drained[1].kind == E::Command::Kind::Undo, "second in order");
    check(drained[2].modName == "CoinRing", "third payload");
    check(E::Core::drainCommands().empty(), "queue empty after drain");
}

void testMode() {
    check(E::mode() == E::Mode::Off, "starts off");
    E::notifyEditEntered();
    check(E::mode() == E::Mode::Editing, "entered edit");
    E::Core::setMode(E::Mode::Reloading);
    E::notifyEditLeft();
    check(E::mode() == E::Mode::Reloading, "leaving edit does not cancel a reload");
    E::Core::setMode(E::Mode::Playing);
    E::notifyEditEntered();
    check(E::mode() == E::Mode::Editing, "edit from play");
    E::notifyEditLeft();
    check(E::mode() == E::Mode::Off, "left edit");
}

void testHooksAndBinding() {
    E::Core::runUiSeamHook();
    check(hookCalls == 0, "no hook yet");
    E::setUiSeamHook(&hook);
    E::Core::runUiSeamHook();
    check(hookCalls == 1, "hook runs");

    LiveActor* a = reinterpret_cast< LiveActor* >(0x1000);
    LiveActor* b = reinterpret_cast< LiveActor* >(0x2000);
    E::Core::bindActor(E::RowId{1}, a);
    E::Core::bindActor(E::RowId{2}, b);
    check(E::actorForRow(E::RowId{1}) == a && E::rowForActor(b) == E::RowId{2}, "bound both ways");
    E::Core::bindActor(E::RowId{1}, b);  // re-created: the row now has a new actor
    check(E::actorForRow(E::RowId{1}) == b && E::rowForActor(b) == E::RowId{1}, "rebind moves the actor");
    check(E::rowForActor(a) == E::RowId{}, "old actor unbound");
    int count = 0;
    E::forEachBoundRow([&](E::RowId, LiveActor*) { ++count; });
    check(count == 1 && E::actorForRow(E::RowId{2}) == nullptr, "an actor belongs to one row");
    E::Core::clearBindings();
    check(E::actorForRow(E::RowId{1}) == nullptr, "cleared");
}

void testRailHooks() {
    const bool before = E::Hooks::enabled();
    const auto* info = reinterpret_cast< const JMapInfo* >(0x1234);
    E::Hooks::setRailTables(3, {info, {info}});
    E::Hooks::setEnabled(false);
    check(E::Hooks::railTables(3) == nullptr, "rail overrides inert with editor off");
    E::Hooks::setEnabled(true);
    check(E::Hooks::railTables(3)->info == info && E::Hooks::railTables(2) == nullptr, "rail overrides scoped to zone");
    E::Hooks::sceneDestroyed();
    check(E::Hooks::railTables(3) == nullptr, "rail overrides cleared before scene data expires");
    E::Hooks::setEnabled(before);
}

void testErrors() {
    std::uint64_t serial = E::status().errorSerial;
    E::Core::reportError("Can't add: gravity limit 128");
    check(E::status().lastError == "Can't add: gravity limit 128", "error text");
    check(E::status().errorSerial == serial + 1, "error serial bumps");
}
}  // namespace

int main() {
    testColumnHash();
    testQueue();
    testMode();
    testHooksAndBinding();
    testErrors();
    testRailHooks();
    std::printf("editor api: %d checks passed\n", checks);
    return 0;
}
