cmake_minimum_required(VERSION 3.20)
project(GameModManager-Plugins LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(ABI_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ABI/include")
set(CORE_ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../src/engine")

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
    src/plugins/thebindingofisaacrebirth/thebindingofisaacrebirth.cpp
)

add_game_plugin(FomodInstaller
    src/plugins/fomod_installer/fomod_installer.cpp
)

add_game_plugin(Anm2Support
    src/plugins/Anm2Support/anm2_support.cpp
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

# Wine.json for Skyrim
set(WINE_JSON "${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/SkyrimSpecialEdition/GameSupport/wine.json")
if(EXISTS "${WINE_JSON}")
    configure_file(
        "${WINE_JSON}"
        "${CMAKE_BINARY_DIR}/plugins/SkyrimSpecialEdition/wine.json"
        COPYONLY
    )
endif()
