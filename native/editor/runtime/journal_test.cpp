// Live check of the journal (crash recovery; native/tools/editor_journal_smoke.py): with
// PETARI_EDITOR=1 and PETARI_EDITOR_JOURNAL_TEST=<phase>, once the stage smoke's checks are done it
// enters Edit on Good Egg through the UI's Tab path, then:
//   kill     moves a placed Octoomba 300 up, waits for <user>/editor/journal/EggStarGalaxy.json to
//            hold it, and kills the process (SIGKILL: no exit handlers, as a crash).
//   restore  the offer is 1 edit; RestoreJournal reloads with the Octoomba's actor 300 up and the
//            session unsaved; a second move makes the journal 2 edits; Save removes it.
//   discard  the offer is there; DiscardJournal removes the file and the Octoomba stays where the
//            disc has it, nothing unsaved.
// Logs "[editor-journal] PASS" or "[editor-journal] FAIL: <reason>" (kill logs "killing" instead).
#include "runtime_core.hpp"

#include <petari/host_allocation.hpp>

#include "Game/LiveActor/LiveActor.hpp"
#include <petari/editor_settings.hpp>
#include <petari/platform/mod_folder.hpp>
#include <petari/smoke_phase.hpp>

#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace PetariEditor::Runtime {
namespace {
    struct JournalState {
        int frame = -1, phaseFrame = 0, phase = 0;
        bool done = false;
        std::uint64_t notices = 0;
        float actorY = 0;
    };
    JournalState gJournal;

    void finishJournal(const std::string& result) {
        std::fprintf(stderr, "[editor-journal] %s\n", result.c_str());
        gJournal.done = true;
    }

    std::filesystem::path journalFile() {
        return PetariNative::Platform::ModFolder::userDirectory() / "editor" / "journal" / "EggStarGalaxy.json";
    }

    std::string readJournalText() {
        std::ifstream in(journalFile(), std::ios::binary);
        return std::string(std::istreambuf_iterator< char >(in), {});
    }

    RowId findTakobo() {
        const ModelView* m = model();
        RowId found;
        for (int z = 0; z < m->zoneCount() && !found.valid(); z++) {
            if (std::string(m->zoneName(z)) != "SoramameZone") continue;
            for (int t = 0; t < m->tableCount(z) && !found.valid(); t++) {
                if (std::string(m->tablePath(z, t)) != "jmp/placement/layera/objinfo") continue;
                m->forEachRow(z, t, [&](RowId id) {
                    const char* pName = m->objectName(id);
                    if (!found.valid() && pName && std::string(pName) == "Takobo") found = id;
                });
            }
        }
        return found;
    }

    void move(RowId takobo, float dy) {
        Command c{Command::Kind::Apply};
        c.op.label = "Move Octoomba";
        c.op.sets.push_back(SetCell{takobo, columnHash("pos_y"), Cell{CellType::Float, 0, model()->transform(takobo).pos.y + dy, {}}});
        enqueue(std::move(c));
    }
}  // namespace

void journalTestTick() {
    PetariNative::HostAllocationScope host;  // test bookkeeping
    static const char* const sPhase = std::getenv("PETARI_EDITOR_JOURNAL_TEST");
    JournalState& s = gJournal;
    if (sPhase == nullptr || sPhase[0] == '\0' || s.done) return;
    const std::string which = sPhase;
    if (s.frame < 0) {
        if (!PetariNative::SmokePhase::stageChecksDone.load()) return;
        s.frame = 0;
        PetariNative::EditorSettings::toggle();
        return;
    }
    s.phaseFrame++;
    if (s.phase == 0) {
        if (mode() != Mode::Editing || model() == nullptr) {
            if (s.phaseFrame > 600) finishJournal("FAIL: Edit mode never started");
            return;
        }
        const RowId takobo = findTakobo();
        if (!takobo.valid() || actorForRow(takobo) == nullptr) return finishJournal("FAIL: no bound Takobo");
        s.actorY = actorForRow(takobo)->mPosition.y;
        const Status& st = status();
        if (which == "kill") {
            if (st.journalEdits != 0) return finishJournal("FAIL: an offer before any crash");
            move(takobo, 300.0f);
        } else {
            const std::int64_t age = static_cast< std::int64_t >(std::time(nullptr)) - st.journalTime;
            std::fprintf(stderr, "[editor-journal] offer: %d edits, %lld s old\n", st.journalEdits, static_cast< long long >(age));
            if (st.journalEdits != 1) return finishJournal("FAIL: the offer is " + std::to_string(st.journalEdits) + " edits, not 1");
            if (age < 0 || age > 3600) return finishJournal("FAIL: the offer's time is off");
            if (st.unsaved) return finishJournal("FAIL: unsaved before answering");
            enqueue(Command{which == "restore" ? Command::Kind::RestoreJournal : Command::Kind::DiscardJournal});
        }
        s.notices = st.noticeSerial;
        s.phase = 1;
        s.phaseFrame = 0;
        return;
    }
    if (s.phase == 1) {
        if (which == "kill") {
            const std::string text = readJournalText();
            if (text.find("\"edits\":1") == std::string::npos && text.find("\"edits\": 1") == std::string::npos) {
                if (s.phaseFrame > 120) finishJournal("FAIL: the journal never held the edit: " + text);
                return;
            }
            std::fprintf(stderr, "[editor-journal] journaled (%zu bytes); killing\n", text.size());
            std::fflush(stderr);
            std::raise(SIGKILL);
            return;
        }
        if (which == "discard") {
            if (s.phaseFrame < 5) return;
            if (std::filesystem::exists(journalFile())) return finishJournal("FAIL: the journal is still there after Discard");
            if (status().journalEdits != 0) return finishJournal("FAIL: still offered after Discard");
            if (status().unsaved) return finishJournal("FAIL: unsaved after Discard");
            if (std::fabs(actorForRow(findTakobo())->mPosition.y - s.actorY) > 1.0f) return finishJournal("FAIL: the Octoomba moved");
            return finishJournal("PASS");
        }
        // restore: a reload back into Edit
        if (status().noticeSerial == s.notices || mode() != Mode::Editing) {
            if (s.phaseFrame > 900) finishJournal("FAIL: no restore; error: " + status().lastError);
            return;
        }
        const RowId takobo = findTakobo();
        LiveActor* pActor = takobo.valid() ? actorForRow(takobo) : nullptr;
        if (pActor == nullptr) {
            if (s.phaseFrame > 900) finishJournal("FAIL: no Takobo actor after the restore");
            return;
        }
        const float dy = pActor->mPosition.y - s.actorY;
        std::fprintf(stderr, "[editor-journal] %s; Takobo actor y %+.1f\n", status().notice.c_str(), dy);
        if (std::fabs(dy - 300.0f) > 1.0f) return finishJournal("FAIL: the restored Octoomba isn't 300 up");
        if (!status().unsaved) return finishJournal("FAIL: restored edits show as saved");
        if (status().journalEdits != 0) return finishJournal("FAIL: still offered after Restore");
        if (!std::filesystem::exists(journalFile())) return finishJournal("FAIL: the journal went before a save");
        move(takobo, 100.0f);
        s.phase = 2;
        s.phaseFrame = 0;
        return;
    }
    if (s.phase == 2) {
        const std::string text = readJournalText();
        if (text.find("\"edits\":2") == std::string::npos && text.find("\"edits\": 2") == std::string::npos) {
            if (s.phaseFrame > 120) finishJournal("FAIL: the next edit wasn't journaled as the 2nd: " + text);
            return;
        }
        Command save{Command::Kind::SaveMod};
        save.modName = "JournalTest";
        enqueue(std::move(save));
        s.notices = status().noticeSerial;
        s.phase = 3;
        s.phaseFrame = 0;
        return;
    }
    if (s.phase == 3 && s.phaseFrame > 5) {
        if (status().noticeSerial == s.notices) return finishJournal("FAIL: no save notice; error: " + status().lastError);
        if (status().unsaved) return finishJournal("FAIL: unsaved after saving");
        if (std::filesystem::exists(journalFile())) return finishJournal("FAIL: the journal is still there after Save");
        std::fprintf(stderr, "[editor-journal] %s\n", status().notice.c_str());
        finishJournal("PASS");
    }
}

}  // namespace PetariEditor::Runtime
