add_library(orbit2sx_ee_memory_map STATIC
    EE_MemoryMap.cpp
    EE_MemoryMap.h
)

target_include_directories(orbit2sx_ee_memory_map
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(orbit2sx_ee_memory_map PUBLIC cxx_std_11)

if(ORBIT2SX_BUILD_TESTS)
    add_executable(
        orbit2sx_ee_memory_map_tests
        tests/EE_MemoryMapTests.cpp
    )

    target_link_libraries(
        orbit2sx_ee_memory_map_tests
        PRIVATE orbit2sx_ee_memory_map
    )

    add_test(
        NAME orbit2sx_ee_memory_map_tests
        COMMAND orbit2sx_ee_memory_map_tests
    )
endif()
