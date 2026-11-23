cmake_minimum_required(VERSION 3.15)
project(ORBIT2SX LANGUAGES CXX)

option(ORBIT2SX_BUILD_TESTS "Build ORBIT2SX native unit tests" ON)
if(ORBIT2SX_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(memory)
add_subdirectory(memory_map)
add_subdirectory(memory_bus)
