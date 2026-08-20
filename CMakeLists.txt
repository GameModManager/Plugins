cmake_minimum_required(VERSION 3.20)
project(GameModManager-Plugins LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(ABI_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../abi/include")

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
add_game_plugin(TheBindingOfIsaacRebirth
    src/plugins/thebindingofisaacrebirth/thebindingofisaacrebirth.cpp
)
