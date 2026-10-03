#pragma once
#include "level_internal.hpp"

namespace PetariEditor::LevelApi {
    Json::Value freezeBase(const OpenOptions& options);
    FileSource frozenSource(const OpenOptions& options, const Json::Value& base);
    Json::Value sourceWarnings(const OpenOptions& options, const Json::Value& base);
    void recordSavedSource(Json::Value& base, const std::filesystem::path& root);
    std::string normalizedCheckKey(const EditModel& model, const Check& check);
}  // namespace PetariEditor::LevelApi
