# The level editor's panels (docs/dev/level-editor/TECH.md 4.2): Dear ImGui with docking.
# Root integration: add_subdirectory(native/editor/ui) after native/home_menu (aurora's imgui
# target and the Galaxy fonts); link petari_editor_ui into the app host.
if(NOT TARGET imgui OR NOT TARGET petari_home_menu_imgui)
    message(STATUS "petari_editor_ui: no imgui target; the editor panels are not built")
    return()
endif()

add_library(petari_editor_ui STATIC ui.cpp edit_mode.cpp panels.cpp browser.cpp sheets.cpp gravity.cpp paths.cpp array.cpp playtest.cpp areas.cpp host.cpp import.cpp)
target_include_directories(petari_editor_ui PUBLIC include)
# Deliberately not petari_native_config: nothing here sees the SDK headers.
# petari_editor_core: edit_api.hpp (Runtime); petari_input: the Edit mode gate and camera feed.
target_link_libraries(petari_editor_ui PUBLIC imgui petari_home_menu_imgui petari_editor_core petari_editor_import_catalog petari_input)
target_compile_features(petari_editor_ui PUBLIC cxx_std_17)

if(BUILD_TESTING)
    # The dock layout and the game's viewport, headless (native/tests/editor_ui_tests.cpp).
    add_executable(petari_editor_ui_tests ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/editor_ui_tests.cpp)
    target_link_libraries(petari_editor_ui_tests PRIVATE petari_editor_ui)
    target_include_directories(petari_editor_ui_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})  # panels.hpp
    add_test(NAME native_editor_ui COMMAND petari_editor_ui_tests)
endif()

if(BUILD_TESTING)
    # The transform math the gizmo uses (petari/editor/gizmo_math.hpp): Euler round trips, gravity
    # frames, drags, snapping.
    add_executable(petari_editor_gizmo_tests ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/editor_gizmo_tests.cpp)
    target_include_directories(petari_editor_gizmo_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
    add_test(NAME native_editor_gizmo COMMAND petari_editor_gizmo_tests)
endif()
