# The level editor (docs/dev/level-editor/TECH.md). Shared headers in include/petari/editor;
# runtime/ is the EditModel, ops, SceneSync, reload and save; ui/ is the panels, camera and gizmo.
#
# Root integration: add_subdirectory(native/editor), after native/home_menu (for petari_native_config).
cmake_minimum_required(VERSION 3.25)

get_filename_component(PETARI_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# Disc formats (Yaz0, RARC, BCSV): pure C++, shared by the runtime, the mod loader and tests.
add_library(petari_editor_formats STATIC formats/formats.cpp runtime/json.cpp)
target_include_directories(petari_editor_formats PUBLIC include)
target_compile_features(petari_editor_formats PUBLIC cxx_std_17)
if(APPLE)
    target_link_libraries(petari_editor_formats PRIVATE iconv)  # Shift-JIS <-> UTF-8
endif()

# Patch mods (TECH.md 5.1, 5.3): edits as row ops against the player's disc, built into archives
# by the mod loader at start-up. Pure C++.
add_library(petari_editor_patch STATIC patch/patch.cpp)
target_include_directories(petari_editor_patch PUBLIC include)
target_link_libraries(petari_editor_patch PUBLIC petari_editor_formats)
target_compile_features(petari_editor_patch PUBLIC cxx_std_17)

# Sharing mods as zips and installing them (UX 17). Pure C++ with zlib.
find_package(ZLIB REQUIRED)
add_library(petari_editor_share STATIC share/share.cpp)
target_include_directories(petari_editor_share PUBLIC include)
target_link_libraries(petari_editor_share PUBLIC petari_editor_formats PRIVATE ZLIB::ZLIB)
target_compile_features(petari_editor_share PUBLIC cxx_std_17)

# The EditModel: one galaxy's tables, ops and undo, changed archives. Pure C++ (tested alone).
add_library(petari_editor_model STATIC runtime/edit_model.cpp runtime/checks.cpp)
target_include_directories(petari_editor_model PUBLIC include PRIVATE runtime)
target_link_libraries(petari_editor_model PUBLIC petari_editor_formats petari_editor_patch petari_editor_core)
target_compile_features(petari_editor_model PUBLIC cxx_std_17)

# The object catalog's shipped files, embedded (TECH.md 2.11; native/editor/catalog).
set(_petari_catalog_dir ${CMAKE_CURRENT_SOURCE_DIR}/catalog)
set(_petari_catalog_cpp ${CMAKE_CURRENT_BINARY_DIR}/catalog_data.cpp)
add_custom_command(OUTPUT ${_petari_catalog_cpp}
    COMMAND ${CMAKE_COMMAND} -DGENERATED=${_petari_catalog_dir}/generated.json -DCURATED=${_petari_catalog_dir}/curated.json
            -DOUT=${_petari_catalog_cpp} -P ${_petari_catalog_dir}/embed_catalog.cmake
    DEPENDS ${_petari_catalog_dir}/generated.json ${_petari_catalog_dir}/curated.json ${_petari_catalog_dir}/embed_catalog.cmake
    COMMENT "Embedding the editor catalog")

# Terrain (TECH.md section 6): meshes and brushes, the KCL + .pa writer, the J3D model writer. Pure C++ (no game code).
add_library(petari_editor_terrain STATIC terrain/mesh.cpp terrain/brush.cpp terrain/kcl_writer.cpp terrain/kcl_reader.cpp terrain/model_writer.cpp terrain/bake.cpp)
target_include_directories(petari_editor_terrain PUBLIC include)
target_link_libraries(petari_editor_terrain PUBLIC petari_editor_formats petari_editor_patch)
target_compile_features(petari_editor_terrain PUBLIC cxx_std_17)
# New galaxies from templates (petari/editor/galaxy/new_galaxy.hpp): pure C++ over the formats
# library; the editor's New Galaxy command and the headless tools use it.
add_library(petari_editor_galaxy STATIC galaxy/new_galaxy.cpp)
target_include_directories(petari_editor_galaxy PUBLIC include)
target_link_libraries(petari_editor_galaxy PUBLIC petari_editor_formats petari_editor_terrain)
target_compile_features(petari_editor_galaxy PUBLIC cxx_std_17)

# The brush class and its Checks from rows (no runtime, no game): the in-game editor and headless tools.
add_library(petari_editor_terrain_catalog STATIC terrain/brush_catalog.cpp)
target_link_libraries(petari_editor_terrain_catalog PUBLIC petari_editor_terrain petari_editor_core petari_editor_model)
# Bakes brushes into a Full-archive mod (planet archives, PlanetMapDataTable, placement rows).
add_executable(petari_terrain_bake tools/terrain_bake.cpp)
target_link_libraries(petari_terrain_bake PRIVATE petari_editor_terrain)

# The session core behind edit_api.hpp. No game or SDK dependency, so it is unit-tested alone.
add_library(petari_editor_core STATIC playtest/playtest.cpp areas/areas.cpp areas/camera_preview.cpp gravity/gravity.cpp paths/paths.cpp paths/paths_view.cpp runtime/edit_api_core.cpp runtime/runtime_hooks.cpp runtime/catalog.cpp runtime/share_mods.cpp
    ${_petari_catalog_cpp})
target_include_directories(petari_editor_core PUBLIC include PRIVATE runtime)
target_link_libraries(petari_editor_core PUBLIC petari_editor_formats petari_editor_share petari_native_config petari_host_runtime)
if(TARGET petari_platform_dvd)
    target_link_libraries(petari_editor_core PRIVATE petari_platform_dvd)  # ModFolder: the user's mods folder
endif()
target_compile_features(petari_editor_core PUBLIC cxx_std_17)

# The runtime in the game: the session, the binding, the command drain and SceneSync. It calls
# game code (src/Game/Editor), so only the app and the link check link it.
# Loading for editing and saving as a mod, without the game (runtime/model_io.hpp): the Runtime and
# headless tools (petari-level) share it. Needs the native DVD layer (the disc and the mod overlay).
if(TARGET petari_platform_dvd)
    add_library(petari_editor_save STATIC runtime/model_io.cpp)
    target_include_directories(petari_editor_save PUBLIC include runtime)
    target_link_libraries(petari_editor_save PUBLIC petari_editor_model petari_editor_core petari_editor_terrain petari_editor_terrain_catalog
        petari_platform_dvd
        petari_native_config)
endif()

add_library(petari_editor_runtime STATIC runtime/session.cpp runtime/new_galaxy_session.cpp runtime/new_galaxy_test.cpp runtime/terrain_sync.cpp runtime/save_mod.cpp runtime/sync_test.cpp runtime/reload_test.cpp runtime/save_test.cpp
    runtime/redraw_test.cpp runtime/ensure_test.cpp runtime/scratch_test.cpp runtime/brush_test.cpp runtime/paths_test.cpp runtime/areas_test.cpp)
target_include_directories(petari_editor_runtime PUBLIC include PRIVATE runtime)
target_link_libraries(petari_editor_runtime PUBLIC petari_editor_core petari_editor_model petari_editor_terrain petari_editor_terrain_catalog petari_editor_galaxy petari_native_config)
if(TARGET petari_platform_dvd)
    target_link_libraries(petari_editor_runtime PRIVATE petari_platform_dvd PUBLIC petari_editor_save)
endif()

if(BUILD_TESTING)
    add_executable(petari_editor_playtest_tests ${PETARI_REPO_ROOT}/native/tests/editor_playtest_tests.cpp)
    target_link_libraries(petari_editor_playtest_tests PRIVATE petari_editor_core petari_editor_model)
    target_include_directories(petari_editor_playtest_tests PRIVATE runtime)
    add_test(NAME native_editor_playtest COMMAND petari_editor_playtest_tests)
    add_executable(petari_editor_areas_tests ${PETARI_REPO_ROOT}/native/tests/editor_areas_tests.cpp)
    target_include_directories(petari_editor_areas_tests PRIVATE runtime)
    target_link_libraries(petari_editor_areas_tests PRIVATE petari_editor_model petari_editor_core)
    add_test(NAME native_editor_areas COMMAND petari_editor_areas_tests)
    add_executable(petari_editor_gravity_tests ${PETARI_REPO_ROOT}/native/tests/editor_gravity_tests.cpp)
    target_link_libraries(petari_editor_gravity_tests PRIVATE petari_editor_core petari_editor_model)
    target_include_directories(petari_editor_gravity_tests PRIVATE runtime)
    add_test(NAME native_editor_gravity COMMAND petari_editor_gravity_tests)
    add_executable(petari_editor_paths_tests ${PETARI_REPO_ROOT}/native/tests/editor_paths_tests.cpp)
    target_include_directories(petari_editor_paths_tests PRIVATE runtime)
    target_link_libraries(petari_editor_paths_tests PRIVATE petari_editor_model petari_editor_core)
    add_test(NAME native_editor_paths COMMAND petari_editor_paths_tests)
    add_executable(petari_editor_debug_draw_tests ${PETARI_REPO_ROOT}/native/tests/editor_debug_draw_tests.cpp)
    target_include_directories(petari_editor_debug_draw_tests PRIVATE include)
    target_compile_features(petari_editor_debug_draw_tests PRIVATE cxx_std_17)
    target_compile_options(petari_editor_debug_draw_tests PRIVATE -UNDEBUG)
    add_test(NAME native_editor_debug_draw COMMAND petari_editor_debug_draw_tests)
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

    add_executable(petari_editor_catalog_tests ${PETARI_REPO_ROOT}/native/tests/editor_catalog_tests.cpp)
    target_link_libraries(petari_editor_catalog_tests PRIVATE petari_editor_core petari_editor_terrain_catalog)
    add_test(NAME native_editor_catalog COMMAND petari_editor_catalog_tests)

    if(TARGET petari_editor_save)
        add_executable(petari_editor_save_tests ${PETARI_REPO_ROOT}/native/tests/editor_save_tests.cpp)
        target_compile_definitions(petari_editor_save_tests PRIVATE
            PETARI_TEST_DISC_FILES="${PETARI_REPO_ROOT}/build/game-data/RMGE01/files")
        target_link_libraries(petari_editor_save_tests PRIVATE petari_editor_save)
        add_test(NAME native_editor_save COMMAND petari_editor_save_tests)
    endif()

    add_executable(petari_editor_share_tests ${PETARI_REPO_ROOT}/native/tests/editor_share_tests.cpp)
    target_link_libraries(petari_editor_share_tests PRIVATE petari_editor_share)
    add_test(NAME native_editor_share COMMAND petari_editor_share_tests)

    add_executable(petari_editor_model_tests ${PETARI_REPO_ROOT}/native/tests/editor_model_tests.cpp)
    target_compile_definitions(petari_editor_model_tests PRIVATE
        PETARI_TEST_DISC_FILES="${PETARI_REPO_ROOT}/build/game-data/RMGE01/files")
    target_include_directories(petari_editor_model_tests PRIVATE runtime)
    target_link_libraries(petari_editor_model_tests PRIVATE petari_editor_model petari_editor_core)
    add_test(NAME native_editor_model COMMAND petari_editor_model_tests)

    # The KCL writer round-tripped through the game's own collision server (KCollision.cpp).
    if(TARGET petari_j3d)
        add_executable(petari_editor_terrain_kcl_tests
            ${PETARI_REPO_ROOT}/native/tests/editor_terrain_kcl_tests.cpp
            ${PETARI_REPO_ROOT}/native/tests/heap_diagnostics.cpp
            ${PETARI_REPO_ROOT}/src/Game/Map/KCollision.cpp
            ${PETARI_REPO_ROOT}/src/Game/Map/KCollisionPlus.cpp
            ${PETARI_REPO_ROOT}/src/Game/Util/JMapInfo.cpp
            ${PETARI_REPO_ROOT}/src/Game/Util/MathUtil.cpp
            ${PETARI_REPO_ROOT}/src/JSystem/JGadget/hashcode.cpp)
        petari_encode_target_sources(petari_editor_terrain_kcl_tests)
        target_compile_definitions(petari_editor_terrain_kcl_tests PRIVATE
            PETARI_TEST_DISC_FILES="${PETARI_REPO_ROOT}/build/game-data/RMGE01/files")
        target_link_libraries(petari_editor_terrain_kcl_tests PRIVATE petari_j3d petari_editor_terrain)
        target_link_options(petari_editor_terrain_kcl_tests PRIVATE -Wl,-dead_strip)
        add_test(NAME native_editor_terrain_kcl COMMAND petari_editor_terrain_kcl_tests)

        # The model writer's output loaded by the game's J3D loader (donor materials from the disc).
        add_executable(petari_editor_terrain_model_tests
            ${PETARI_REPO_ROOT}/native/tests/editor_terrain_model_tests.cpp
            ${PETARI_REPO_ROOT}/native/tests/heap_diagnostics.cpp
            ${PETARI_REPO_ROOT}/native/gx/legacy_commands.cpp)
        target_compile_definitions(petari_editor_terrain_model_tests PRIVATE
            PETARI_TEST_DISC_FILES="${PETARI_REPO_ROOT}/build/game-data/RMGE01/files")
        target_link_libraries(petari_editor_terrain_model_tests PRIVATE petari_j3d petari_editor_terrain)
        target_link_options(petari_editor_terrain_model_tests PRIVATE -Wl,-dead_strip)
        add_test(NAME native_editor_terrain_model COMMAND petari_editor_terrain_model_tests)

        # New galaxies from each template, read back and loaded by the mod galaxy registry.
        if(TARGET petari_platform)
            add_executable(petari_editor_new_galaxy_tests ${PETARI_REPO_ROOT}/native/tests/editor_new_galaxy_tests.cpp)
            target_compile_definitions(petari_editor_new_galaxy_tests PRIVATE
                PETARI_TEST_DISC_FILES="${PETARI_REPO_ROOT}/build/game-data/RMGE01/files")
            target_link_libraries(petari_editor_new_galaxy_tests PRIVATE petari_editor_galaxy petari_platform)
            add_test(NAME native_editor_new_galaxy COMMAND petari_editor_new_galaxy_tests)
        endif()
    endif()
endif()

# The panels, camera and gizmo (Dear ImGui; needs aurora's imgui target and the Home menu's fonts).
add_subdirectory(ui)
