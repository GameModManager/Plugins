cmake_minimum_required(VERSION 3.20)
project(GameModManager-Plugins LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Each plugin is a shared library loaded via dlopen at runtime.
# The ABI header lives in ../abi/include/.

set(ABI_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../abi/include")

# Helper function to add a game plugin
function(add_game_plugin NAME SOURCE)
    add_library(${NAME} MODULE ${SOURCE})
    target_include_directories(${NAME} PRIVATE ${ABI_INCLUDE_DIR})
    target_compile_definitions(${NAME} PRIVATE VERSION="0.1.0")
    # Plugins must not link against Qt or any engine code — they use only the C ABI.
    set_target_properties(${NAME} PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    )
endfunction()

# -- Game Support Plugins --

add_game_plugin(SkyrimSpecialEdition
    SkyrimSpecialEdition/GameSupport/SkyrimSpecialEdition.cpp
)

add_game_plugin(TheBindingOfIsaacRebirth
    TheBindingOfIsaacRebirth/GameSupport/TheBindingOfIsaacRebirth.cpp
)

# -- Tool Plugins --

# Isaac Mod Sorter — separate from the game recognition plugin
add_library(IsaacModSorter MODULE
    TheBindingOfIsaacRebirth/Tools/ModSorter/ModSorter.cpp
    TheBindingOfIsaacRebirth/Tools/ModSorter/ModSorterProvider.cpp
    TheBindingOfIsaacRebirth/Tools/ModSorter/AutoSorter.cpp
)
target_include_directories(IsaacModSorter PRIVATE
    ${ABI_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/engine/sort
)
target_compile_definitions(IsaacModSorter PRIVATE VERSION="0.1.0")
set_target_properties(IsaacModSorter PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)
