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

function(add_game_plugin NAME SOURCE)
    add_library(${NAME} MODULE ${SOURCE})
    target_include_directories(${NAME} PRIVATE ${ABI_INCLUDE_DIR})
    target_compile_definitions(${NAME} PRIVATE VERSION="${GMM_VERSION}")
    set_target_properties(${NAME} PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    )
endfunction()

# Game plugins
add_game_plugin(SkyrimSpecialEdition
    src/plugins/SkyrimSpecialEdition/GameSupport/SkyrimSpecialEdition.cpp
)

add_game_plugin(TheBindingOfIsaacRebirth
    src/plugins/TheBindingOfIsaacRebirth/GameSupport/TheBindingOfIsaacRebirth.cpp
)

add_game_plugin(FomodInstaller
    src/plugins/InstallerSupport/FomodInstaller/fomod_installer.cpp
)

add_game_plugin(Anm2Support
    src/plugins/FileSupport/Anm2Support/anm2_support.cpp
)

# Isaac Mod Sorter — needs Core engine/sort headers + yaml-cpp
find_package(PkgConfig REQUIRED)
pkg_check_modules(YAMLCPP REQUIRED IMPORTED_TARGET yaml-cpp)

add_library(IsaacModSorter MODULE
    src/plugins/TheBindingOfIsaacRebirth/Tools/ModSorter/ModSorter.cpp
    src/plugins/TheBindingOfIsaacRebirth/Tools/ModSorter/ModSorterProvider.cpp
    src/plugins/TheBindingOfIsaacRebirth/Tools/ModSorter/AutoSorter.cpp
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
    src/plugins/EventBusViewer/event_bus_viewer.cpp
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

# Wine.json for Skyrim
set(WINE_JSON "${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/SkyrimSpecialEdition/GameSupport/wine.json")
if(EXISTS "${WINE_JSON}")
    configure_file(
        "${WINE_JSON}"
        "${CMAKE_BINARY_DIR}/plugins/SkyrimSpecialEdition/wine.json"
        COPYONLY
    )
endif()
