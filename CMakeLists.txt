cmake_minimum_required(VERSION 3.20)
project(GameModManager-Plugins LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ABI headers
set(ABI_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../abi/include)

# Skyrim SE plugin
add_library(skyrimse MODULE skyrimse/skyrimse.cpp)
target_include_directories(skyrimse PRIVATE ${ABI_INCLUDE_DIR})
set_target_properties(skyrimse PROPERTIES
    PREFIX ""
    SUFFIX ".so"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/plugins
)

# Isaac plugin (stub)
add_library(isaac MODULE isaac/isaac.cpp)
target_include_directories(isaac PRIVATE ${ABI_INCLUDE_DIR})
set_target_properties(isaac PROPERTIES
    PREFIX ""
    SUFFIX ".so"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/plugins
)
