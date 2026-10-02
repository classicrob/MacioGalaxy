# The level editor (docs/dev/level-editor/TECH.md). Shared headers in include/petari/editor;
# runtime/ is the EditModel, ops, SceneSync, reload and save; ui/ is the panels, camera and gizmo.
#
# Root integration: add_subdirectory(native/editor), after native/home_menu (for petari_native_config).
cmake_minimum_required(VERSION 3.25)

get_filename_component(PETARI_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# Disc formats (Yaz0, RARC, BCSV): pure C++, shared by the runtime, the mod loader and tests.
add_library(petari_editor_formats STATIC formats/formats.cpp)
target_include_directories(petari_editor_formats PUBLIC include)
target_compile_features(petari_editor_formats PUBLIC cxx_std_17)

# The EditModel: one galaxy's tables, ops and undo, changed archives. Pure C++ (tested alone).
add_library(petari_editor_model STATIC runtime/edit_model.cpp)
target_include_directories(petari_editor_model PUBLIC include PRIVATE runtime)
target_link_libraries(petari_editor_model PUBLIC petari_editor_formats)
target_compile_features(petari_editor_model PUBLIC cxx_std_17)
if(APPLE)
    target_link_libraries(petari_editor_model PRIVATE iconv)  # Shift-JIS <-> UTF-8
endif()

# The session core behind edit_api.hpp. No game or SDK dependency, so it is unit-tested alone.
add_library(petari_editor_core STATIC runtime/edit_api_core.cpp runtime/runtime_hooks.cpp)
target_include_directories(petari_editor_core PUBLIC include PRIVATE runtime)
target_link_libraries(petari_editor_core PUBLIC petari_native_config)
target_compile_features(petari_editor_core PUBLIC cxx_std_17)

if(BUILD_TESTING)
    add_executable(petari_editor_api_tests ${PETARI_REPO_ROOT}/native/tests/editor_api_tests.cpp)
    target_include_directories(petari_editor_api_tests PRIVATE runtime)
    target_link_libraries(petari_editor_api_tests PRIVATE petari_editor_core)
    add_test(NAME native_editor_api COMMAND petari_editor_api_tests)

    # With the disc in build/game-data, every stage archive and table must rebuild byte for byte.
    add_executable(petari_editor_formats_tests ${PETARI_REPO_ROOT}/native/tests/editor_formats_tests.cpp)
    target_compile_definitions(petari_editor_formats_tests PRIVATE
        PETARI_TEST_DISC_FILES="${PETARI_REPO_ROOT}/build/game-data/RMGE01/files")
    target_link_libraries(petari_editor_formats_tests PRIVATE petari_editor_formats)
    add_test(NAME native_editor_formats COMMAND petari_editor_formats_tests)

    add_executable(petari_editor_model_tests ${PETARI_REPO_ROOT}/native/tests/editor_model_tests.cpp)
    target_compile_definitions(petari_editor_model_tests PRIVATE
        PETARI_TEST_DISC_FILES="${PETARI_REPO_ROOT}/build/game-data/RMGE01/files")
    target_include_directories(petari_editor_model_tests PRIVATE runtime)
    target_link_libraries(petari_editor_model_tests PRIVATE petari_editor_model petari_editor_core)
    add_test(NAME native_editor_model COMMAND petari_editor_model_tests)
endif()

# The panels, camera and gizmo (Dear ImGui; needs aurora's imgui target and the Home menu's fonts).
add_subdirectory(ui)
