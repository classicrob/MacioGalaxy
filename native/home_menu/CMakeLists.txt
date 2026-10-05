# Native replacement for the HOME Button Menu module (see petari/home_menu.hpp).
#
# Root integration:   add_subdirectory(native/home_menu)   (after native/gx, so aurora's imgui target exists)
#   - link petari_home_menu into the target that compiles src/Game/System/HomeButtonMenuWrapper.cpp;
#   - link petari_home_menu_imgui into the application and call
#     PetariNative::HomeMenu::drawImGuiOverlay between aurora_begin_frame() and aurora_end_frame().
# Standalone build:   cmake -S native/home_menu -B build/home-menu -DPETARI_SANITIZERS=ON
#                     cmake --build build/home-menu && ctest --test-dir build/home-menu
cmake_minimum_required(VERSION 3.25)

set(_petari_home_menu_standalone OFF)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(_petari_home_menu_standalone ON)
    project(PetariNativeHomeMenu LANGUAGES C CXX)
    if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Target architecture" FORCE)
    endif()
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    option(PETARI_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
    include(CTest)
endif()

get_filename_component(PETARI_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

if(_petari_home_menu_standalone)
    # The part of the root build's shared configuration these sources need.
    add_library(petari_native_config INTERFACE)
    target_compile_definitions(petari_native_config INTERFACE PETARI_NATIVE=1)
    target_include_directories(petari_native_config INTERFACE
        ${PETARI_REPO_ROOT}/native/include ${PETARI_REPO_ROOT}/include
        ${PETARI_REPO_ROOT}/libs/RVL_SDK/include ${PETARI_REPO_ROOT}/libs/JSystem/include
        ${PETARI_REPO_ROOT}/libs/nw4r/include)
    target_compile_options(petari_native_config INTERFACE
        -fno-strict-aliasing -ffp-contract=off -fcheck-new -Werror=return-type -Wno-register)
    if(PETARI_SANITIZERS)
        target_compile_options(petari_native_config INTERFACE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(petari_native_config INTERFACE -fsanitize=address,undefined)
    endif()
    # HostAllocationScope, which progress.cpp uses.
    add_library(petari_host_runtime STATIC ${PETARI_REPO_ROOT}/native/src/host_allocation.cpp)
    target_link_libraries(petari_host_runtime PUBLIC petari_native_config)
endif()

# The menu and the HBM entry points. home_menu.cpp has no SDK dependency; the
# bridge reads the SDK's HBM and KPAD structures.
# progress.cpp: the personal progress record (petari/progress.hpp), shown on the My Progress page.
add_library(petari_home_menu STATIC home_menu.cpp hbm_bridge.cpp launch_stage.cpp galaxy_font.cpp ${PETARI_REPO_ROOT}/native/progress/progress.cpp)
target_include_directories(petari_home_menu PUBLIC include ${PETARI_REPO_ROOT}/native/include)
target_link_libraries(petari_home_menu PUBLIC petari_native_config)
target_compile_features(petari_home_menu PUBLIC cxx_std_17)
# launch_stage.cpp lists the mod galaxy registry's galaxies (native/platform/dvd/mod_galaxy.cpp).
target_include_directories(petari_home_menu PUBLIC ${PETARI_REPO_ROOT}/native/platform/include)
if(TARGET petari_platform_dvd)
    target_link_libraries(petari_home_menu PUBLIC petari_platform_dvd)
else()
    target_sources(petari_home_menu PRIVATE ${PETARI_REPO_ROOT}/native/platform/dvd/mod_galaxy.cpp)
endif()
target_link_libraries(petari_home_menu PUBLIC petari_host_runtime)  # progress.cpp: HostAllocationScope
# The Controls page lists the live bindings when the input layer is in the
# build (the root adds native/input first). Standalone builds show no lines.
if(TARGET petari_input)
    target_link_libraries(petari_home_menu PUBLIC petari_input petari_platform_dvd)  # Mod folder page: mod_folder.hpp
    target_compile_definitions(petari_home_menu PRIVATE PETARI_HOME_MENU_INPUT=1)
endif()

# The Dear ImGui overlay. Uses aurora's imgui target (IMGUI_USER_CONFIG and all);
# a standalone build compiles ImGui's core from aurora's fetched sources.
set(PETARI_IMGUI_SOURCE_DIR "${PETARI_REPO_ROOT}/build/aurora/_deps/imgui-src" CACHE PATH
    "Dear ImGui sources for a standalone overlay test")
if(NOT TARGET imgui AND _petari_home_menu_standalone AND EXISTS "${PETARI_IMGUI_SOURCE_DIR}/imgui.h")
    add_library(imgui STATIC
        ${PETARI_IMGUI_SOURCE_DIR}/imgui.cpp ${PETARI_IMGUI_SOURCE_DIR}/imgui_draw.cpp
        ${PETARI_IMGUI_SOURCE_DIR}/imgui_tables.cpp ${PETARI_IMGUI_SOURCE_DIR}/imgui_widgets.cpp)
    target_include_directories(imgui PUBLIC ${PETARI_IMGUI_SOURCE_DIR})
endif()
if(TARGET imgui)
    add_library(petari_home_menu_imgui STATIC imgui_overlay.cpp)
    # Deliberately not petari_native_config: nothing here sees the SDK headers.
    target_include_directories(petari_home_menu_imgui PUBLIC ${PETARI_REPO_ROOT}/native/include)
    target_link_libraries(petari_home_menu_imgui PUBLIC petari_home_menu imgui)
    if(TARGET petari_input)
        target_compile_definitions(petari_home_menu_imgui PRIVATE PETARI_HOME_MENU_INPUT=1)
    endif()
    # Development tool: every page rendered offline to PNG (native/tools/home_menu_render.cpp).
    add_executable(petari_home_menu_render EXCLUDE_FROM_ALL ${PETARI_REPO_ROOT}/native/tools/home_menu_render.cpp)
    target_link_libraries(petari_home_menu_render PRIVATE petari_home_menu_imgui)
else()
    message(STATUS "petari_home_menu: no imgui target; the overlay is not built")
endif()

if(BUILD_TESTING)
    # HomeButtonMenuWrapper.cpp is compiled here too, so the tests go through
    # the game's own RSO::HBM* calls and prove the native branch links without
    # the RSO loader.
    add_executable(petari_home_menu_tests
        ${PETARI_REPO_ROOT}/native/tests/home_menu_tests.cpp
        ${PETARI_REPO_ROOT}/src/Game/System/HomeButtonMenuWrapper.cpp)
    if(COMMAND petari_encode_target_sources)
        petari_encode_target_sources(petari_home_menu_tests)
    endif()
    target_compile_options(petari_home_menu_tests PRIVATE -Wno-inconsistent-missing-override)
    if(TARGET petari_home_menu_imgui)
        target_link_libraries(petari_home_menu_tests PRIVATE petari_home_menu_imgui)
        target_compile_definitions(petari_home_menu_tests PRIVATE PETARI_HOME_MENU_TEST_IMGUI=1)
    else()
        target_link_libraries(petari_home_menu_tests PRIVATE petari_home_menu)
    endif()
    add_test(NAME native_home_menu COMMAND petari_home_menu_tests)

    # The game's fonts (galaxy_font.hpp): synthetic data, the ImGui atlas, and the
    # real Font.arc when the disc is in build/game-data (skipped otherwise).
    add_executable(petari_galaxy_font_tests ${PETARI_REPO_ROOT}/native/tests/galaxy_font_tests.cpp)
    target_compile_definitions(petari_galaxy_font_tests PRIVATE
        PETARI_TEST_FONT_ARCHIVE="${PETARI_REPO_ROOT}/build/game-data/RMGE01/files/LayoutData/Font.arc")
    if(TARGET petari_home_menu_imgui)
        target_link_libraries(petari_galaxy_font_tests PRIVATE petari_home_menu_imgui)
        target_compile_definitions(petari_galaxy_font_tests PRIVATE PETARI_GALAXY_FONT_TEST_IMGUI=1)
    else()
        target_link_libraries(petari_galaxy_font_tests PRIVATE petari_home_menu)
    endif()
    add_test(NAME native_galaxy_font COMMAND petari_galaxy_font_tests)

    add_executable(petari_progress_tests ${PETARI_REPO_ROOT}/native/tests/progress_tests.cpp)
    target_link_libraries(petari_progress_tests PRIVATE petari_home_menu)
    add_test(NAME native_progress COMMAND petari_progress_tests)

    # The fixed-array index audit (native/tools/array_index_audit.py): the gate against the
    # F1 -> Mods crash class. Fails on a constant index past an array's end or a computed index
    # (`items[count + 2]`) that nobody has reviewed (an `audit-ok:` comment or a row in
    # array_index_audit_allow.txt). The full run re-parses every native/ translation unit with
    # clang (minutes), so both tests carry the `audit` label: skip them with `ctest -LE audit`.
    # The full one is skipped by itself when there is no compile_commands.json.
    if(NOT _petari_home_menu_standalone)
        find_program(PETARI_PYTHON3 python3)
        if(PETARI_PYTHON3)
            add_test(NAME native_array_index_audit_selftest
                COMMAND ${PETARI_PYTHON3} ${PETARI_REPO_ROOT}/native/tools/array_index_audit.py --selftest)
            add_test(NAME native_array_index_audit
                COMMAND ${PETARI_PYTHON3} ${PETARI_REPO_ROOT}/native/tools/array_index_audit.py
                        --build ${CMAKE_BINARY_DIR} --jobs 4)
            set_tests_properties(native_array_index_audit_selftest PROPERTIES LABELS audit)
            set_tests_properties(native_array_index_audit PROPERTIES LABELS "audit;slow" TIMEOUT 1800
                SKIP_RETURN_CODE 77)
        endif()
    endif()
endif()
