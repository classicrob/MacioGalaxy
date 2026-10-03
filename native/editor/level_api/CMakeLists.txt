# The agent level-authoring API (docs/dev/level-editor/AGENT_API_X.md): the editor core driven by JSON
# commands, headless (no game, no renderer). petari-level-x is its CLI.
add_library(petari_level_api_x STATIC level.cpp query.cpp ops.cpp op_json.cpp commands.cpp world.cpp)
target_include_directories(petari_level_api_x PUBLIC include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../runtime)
target_link_libraries(petari_level_api_x PUBLIC petari_editor_save petari_editor_model petari_editor_core petari_editor_patch petari_editor_terrain)
target_compile_features(petari_level_api_x PUBLIC cxx_std_17)

add_executable(petari-level-x cli.cpp)
target_link_libraries(petari-level-x PRIVATE petari_level_api_x)

if(BUILD_TESTING)
    add_executable(petari_level_api_x_tests ${PETARI_REPO_ROOT}/native/tests/level_api_x_tests.cpp)
    target_compile_definitions(petari_level_api_x_tests PRIVATE PETARI_TEST_DISC_FILES="${PETARI_REPO_ROOT}/build/game-data/RMGE01/files")
    target_include_directories(petari_level_api_x_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../runtime)
    target_link_libraries(petari_level_api_x_tests PRIVATE petari_level_api_x)
    add_test(NAME native_level_api_x COMMAND petari_level_api_x_tests)
endif()
