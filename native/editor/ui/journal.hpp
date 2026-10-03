#pragma once
#include <cstdint>
#include <string>

namespace PetariEditor::Ui {
std::string journalQuestion(int edits, std::int64_t saved, std::int64_t now);
void drawJournalSheet();
}
