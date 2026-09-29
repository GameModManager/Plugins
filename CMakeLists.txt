cmake_minimum_required(VERSION 3.20)
project(GameModManager-Plugins LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Locate the ABI headers and the Core engine sources. The Plugins repo is built
# both standalone (sibling of Core/ABI in the workspace) and as a submodule of
# Core; resolve either layout so the same CMakeLists works in both.
foreach(p "${CMAKE_CURRENT_SOURCE_DIR}/../ABI/include"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../external/abi/include"
          "${CMAKE_CURRENT_SOURCE_DIR}/../abi/include")
    if(EXISTS "${p}")
        set(ABI_INCLUDE_DIR "${p}")
        break()
    endif()
endforeach()

foreach(p "${CMAKE_CURRENT_SOURCE_DIR}/../Core/src/engine"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/engine")
    if(EXISTS "${p}")
        set(CORE_ENGINE_DIR "${p}")
        break()
    endif()
endforeach()

# Shared packet: Gamebryo save parsing.
# Vendors its own private GamebryoSaveReader (no Core dependency) so a
# future Bethesda game plugin (FO4, etc.) can link GMM_Gamebryo without
# pulling in engine headers.
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)
pkg_check_modules(LZ4 REQUIRED IMPORTED_TARGET liblz4)

add_library(GMM_Gamebryo STATIC
    src/shared/Gamebryo/GamebryoSaveReader.cpp
    src/shared/Gamebryo/GamebryoSaveGame.cpp
    src/shared/Gamebryo/SkyrimSaveGame.cpp
    src/shared/Gamebryo/SkyrimSESaveGame.cpp
)
target_include_directories(GMM_Gamebryo PUBLIC src/shared/Gamebryo)
target_link_libraries(GMM_Gamebryo PUBLIC PkgConfig::ZLIB PkgConfig::LZ4)

function(add_game_plugin NAME)
    add_library(${NAME} MODULE ${ARGN})
    target_include_directories(${NAME} PRIVATE ${ABI_INCLUDE_DIR})
    target_compile_definitions(${NAME} PRIVATE VERSION="${GMM_VERSION}")
    set_target_properties(${NAME} PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    )
endfunction()

# Game plugins
add_game_plugin(SkyrimSpecialEdition
    src/plugins/GameSupport/SkyrimSpecialEdition/SkyrimSpecialEdition.cpp
    src/plugins/GameSupport/SkyrimSpecialEdition/SkyrimSESaveParser.cpp
)
target_link_libraries(SkyrimSpecialEdition PRIVATE GMM_Gamebryo)
target_include_directories(SkyrimSpecialEdition PRIVATE
    src/plugins/GameSupport/SkyrimSpecialEdition
)

add_game_plugin(TheBindingOfIsaacRebirth
    src/plugins/GameSupport/TheBindingOfIsaacRebirth/TheBindingOfIsaacRebirth.cpp
)

add_game_plugin(FomodInstaller
    src/plugins/InstallerSupport/FomodInstaller/fomod_installer.cpp
)

add_game_plugin(ANM2
    src/plugins/FileSupport/ANM2/anm2_plugin.cpp
    src/plugins/FileSupport/ANM2/anm2_parser.cpp
    src/plugins/FileSupport/ANM2/anm2_renderer.cpp
)
target_link_libraries(ANM2 PRIVATE Qt6::Widgets)

# ANM2 geometry tests. Only when the host asks for its test suite, so a
# standalone plugin build pulls in no test-only code. The renderer sources are
# compiled again into the test binary rather than linked from the module: a
# MODULE library has no linkable import library on every platform, and the
# point is to exercise the sources directly.
if(GMM_BUILD_TESTS)
    set(ANM2_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/FileSupport/ANM2")
    add_executable(test_anm2_renderer
        tests/ANM2/test_anm2_renderer.cpp
        ${ANM2_SRC_DIR}/anm2_parser.cpp
        ${ANM2_SRC_DIR}/anm2_renderer.cpp
    )
    target_include_directories(test_anm2_renderer PRIVATE
        ${ABI_INCLUDE_DIR}
        ${ANM2_SRC_DIR}
    )
    target_link_libraries(test_anm2_renderer PRIVATE Qt6::Widgets)

    # One CTest entry per case, so a failure names itself and
    # `ctest -R <case>` runs just that one.
    foreach(case
            scale_applied_once
            scale_composes_with_crop_once
            negative_scale_mirrors
            pivot_at_centre_does_not_move
            pivot_controls_layer_placement
            pivot_reorder_negative_control
            bounds_match_draw
            negative_tint_offset_applies
            tint_offset_clamps_to_channel_range
            tint_offset_applies_to_untouched_tint
            placeholder_path_scales_once
            parser_scale_attribute_defaults
            oversize_canvas_is_refused_not_allocated
            measured_real_maximum_still_renders
            oversize_is_reported_not_silent
            canvas_size_arithmetic_cannot_overflow
            scale_is_bounded_by_the_canvas_not_clamped
            frame_count_is_bounded)
        add_test(NAME anm2_geometry.${case}
                 COMMAND test_anm2_renderer ${case})
    endforeach()
    set_tests_properties(anm2_geometry.scale_applied_once
        anm2_geometry.scale_composes_with_crop_once
        anm2_geometry.negative_scale_mirrors
        anm2_geometry.pivot_at_centre_does_not_move
        anm2_geometry.pivot_controls_layer_placement
        anm2_geometry.pivot_reorder_negative_control
        anm2_geometry.bounds_match_draw
        anm2_geometry.negative_tint_offset_applies
        anm2_geometry.tint_offset_clamps_to_channel_range
        anm2_geometry.tint_offset_applies_to_untouched_tint
        anm2_geometry.placeholder_path_scales_once
        anm2_geometry.parser_scale_attribute_defaults
        anm2_geometry.oversize_canvas_is_refused_not_allocated
        anm2_geometry.measured_real_maximum_still_renders
        anm2_geometry.oversize_is_reported_not_silent
        anm2_geometry.canvas_size_arithmetic_cannot_overflow
        anm2_geometry.scale_is_bounded_by_the_canvas_not_clamped
        anm2_geometry.frame_count_is_bounded
        PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# Isaac Mod Sorter — needs Core engine/sort headers + yaml-cpp
find_package(PkgConfig REQUIRED)
pkg_check_modules(YAMLCPP REQUIRED IMPORTED_TARGET yaml-cpp)

add_library(IsaacModSorter MODULE
    src/plugins/Tool/ModSorter/ModSorter.cpp
    src/plugins/Tool/ModSorter/ModSorterProvider.cpp
    src/plugins/Tool/ModSorter/AutoSorter.cpp
)
target_include_directories(IsaacModSorter PRIVATE
    ${ABI_INCLUDE_DIR}
    ${CORE_ENGINE_DIR}/sort
)
target_link_libraries(IsaacModSorter PRIVATE PkgConfig::YAMLCPP)
target_compile_definitions(IsaacModSorter PRIVATE VERSION="${GMM_VERSION}")
set_target_properties(IsaacModSorter PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)

# Event Bus Viewer — v2 IPluginTool plugin.
# Shows a Qt dialog listing recent EventBus activity. It reaches the host's
# EventBus singleton through the executable's exported symbols (gamemodmanager is
# linked with -rdynamic), so it intentionally does NOT link the engine — linking
# the static engine would create a second, empty EventBus inside the plugin.
find_package(Qt6 COMPONENTS Widgets REQUIRED)

add_library(EventBusViewer MODULE
    src/plugins/Tool/EventBusViewer/event_bus_viewer.cpp
)
target_include_directories(EventBusViewer PRIVATE
    ${ABI_INCLUDE_DIR}
    ${CORE_ENGINE_DIR}
)
target_link_libraries(EventBusViewer PRIVATE Qt6::Widgets)
target_compile_definitions(EventBusViewer PRIVATE VERSION="${GMM_VERSION}")
set_target_properties(EventBusViewer PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)

# Wine.json files shipped next to each game plugin (<game_id>/wine.json).
# The UI reads them as "Recommended Wine Packages".
foreach(GAME_ID SkyrimSpecialEdition TheBindingOfIsaacRebirth)
    set(WINE_JSON "${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/GameSupport/${GAME_ID}/wine.json")
    if(EXISTS "${WINE_JSON}")
        configure_file(
            "${WINE_JSON}"
            "${CMAKE_BINARY_DIR}/plugins/${GAME_ID}/wine.json"
            COPYONLY
        )
    endif()
endforeach()
