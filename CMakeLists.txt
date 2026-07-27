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

add_game_plugin(skyrimse skyrimse/skyrimse.cpp)
add_game_plugin(isaac isaac/isaac.cpp)
