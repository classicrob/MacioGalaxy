# The level editor (docs/dev/level-editor/TECH.md). Shared headers in include/petari/editor;
# runtime/ is the EditModel, ops, SceneSync, reload and save; ui/ is the panels, camera and gizmo.
#
# Root integration: add_subdirectory(native/editor), after native/home_menu (for petari_native_config).
cmake_minimum_required(VERSION 3.25)

get_filename_component(PETARI_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# The session core behind edit_api.hpp. No game or SDK dependency, so it is unit-tested alone.
add_library(petari_editor_core STATIC runtime/edit_api_core.cpp)
target_include_directories(petari_editor_core PUBLIC include PRIVATE runtime)
target_link_libraries(petari_editor_core PUBLIC petari_native_config)
target_compile_features(petari_editor_core PUBLIC cxx_std_17)

if(BUILD_TESTING)
    add_executable(petari_editor_api_tests ${PETARI_REPO_ROOT}/native/tests/editor_api_tests.cpp)
    target_include_directories(petari_editor_api_tests PRIVATE runtime)
    target_link_libraries(petari_editor_api_tests PRIVATE petari_editor_core)
    add_test(NAME native_editor_api COMMAND petari_editor_api_tests)
endif()

# The panels, camera and gizmo (Dear ImGui; needs aurora's imgui target and the Home menu's fonts).
add_subdirectory(ui)
