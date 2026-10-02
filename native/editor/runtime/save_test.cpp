// Live check of saving an edit as a mod (native/tools/editor_save_smoke.py): with
// PETARI_EDITOR=1, PETARI_MODS=LevelEditor and PETARI_EDITOR_SAVE_TEST=<mod name>, once the stage
// smoke's checks are done it enters Edit through the UI's Tab path, moves a placed Octoomba 300
// up and saves the mod. Logs "[editor-save] edited <zone> l_id <n> pos_y <before> -> <after>",
// then "[editor-save] PASS" or "[editor-save] FAIL: <reason>".
#include "runtime_core.hpp"

#include <petari/host_allocation.hpp>

#include <petari/editor_settings.hpp>
#include <petari/smoke_phase.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace PetariEditor::Runtime {
namespace {
    struct SaveState {
        int frame = -1, phaseFrame = 0, phase = 0;
        bool done = false;
        std::uint64_t notices = 0;
    };
    SaveState gSave;

    void finishSave(const std::string& result) {
        std::fprintf(stderr, "[editor-save] %s\n", result.c_str());
        gSave.done = true;
    }
}  // namespace

void saveTestTick() {
    PetariNative::HostAllocationScope host;  // test bookkeeping (the actors it checks are only read)
    static const char* const sModName = std::getenv("PETARI_EDITOR_SAVE_TEST");
    SaveState& s = gSave;
    if (sModName == nullptr || sModName[0] == '\0' || s.done) return;
    if (s.frame < 0) {
        if (!PetariNative::SmokePhase::stageChecksDone.load()) return;
        s.frame = 0;
        PetariNative::EditorSettings::toggle();
        return;
    }
    s.phaseFrame++;
    const ModelView* m = model();
    if (s.phase == 0) {
        if (mode() != Mode::Editing || m == nullptr) {
            if (s.phaseFrame > 600) finishSave("FAIL: Edit mode never started");
            return;
        }
        RowId takobo;
        int zone = -1;
        for (int z = 0; z < m->zoneCount() && !takobo.valid(); z++) {
            if (std::string(m->zoneName(z)) != "SoramameZone") continue;
            for (int t = 0; t < m->tableCount(z) && !takobo.valid(); t++) {
                if (std::string(m->tablePath(z, t)) != "jmp/placement/layera/objinfo") continue;
                m->forEachRow(z, t, [&](RowId id) {
                    const char* pName = m->objectName(id);
                    if (!takobo.valid() && pName && std::string(pName) == "Takobo") takobo = id;
                });
                zone = z;
            }
        }
        if (!takobo.valid()) return finishSave("FAIL: no Takobo row");
        const float y = m->transform(takobo).pos.y;
        std::fprintf(stderr, "[editor-save] edited %s l_id %d pos_y %.1f -> %.1f\n", m->zoneName(zone), m->cell(takobo, columnHash("l_id"))->i,
                     y, y + 300.0f);
        Command move{Command::Kind::Apply};
        move.op.label = "Move Octoomba";
        move.op.sets.push_back(SetCell{takobo, columnHash("pos_y"), Cell{CellType::Float, 0, y + 300.0f, {}}});
        enqueue(std::move(move));
        Command save{Command::Kind::SaveMod};
        save.modName = sModName;
        enqueue(std::move(save));
        s.notices = status().noticeSerial;
        s.phase = 1;
        s.phaseFrame = 0;
        return;
    }
    if (s.phase == 1 && s.phaseFrame > 5) {
        if (status().noticeSerial == s.notices) return finishSave("FAIL: no save notice; error: " + status().lastError);
        if (status().unsaved) return finishSave("FAIL: still unsaved after saving");
        std::fprintf(stderr, "[editor-save] %s\n", status().notice.c_str());
        finishSave("PASS");
    }
}

}  // namespace PetariEditor::Runtime
