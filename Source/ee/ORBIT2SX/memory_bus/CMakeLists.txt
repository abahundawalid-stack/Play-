add_library(orbit2sx_ee_memory_bus STATIC
    EE_MemoryBus.cpp
    EE_MemoryBus.h
)

target_include_directories(orbit2sx_ee_memory_bus
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../memory
        ${CMAKE_CURRENT_SOURCE_DIR}/../memory_map
)

target_compile_features(orbit2sx_ee_memory_bus PUBLIC cxx_std_11)

target_link_libraries(orbit2sx_ee_memory_bus
    PUBLIC
        orbit2sx_ee_memory
        orbit2sx_ee_memory_map
)

if(ORBIT2SX_BUILD_TESTS)
    add_executable(orbit2sx_ee_memory_bus_tests
        tests/EE_MemoryBusTests.cpp
    )

    target_link_libraries(orbit2sx_ee_memory_bus_tests
        PRIVATE orbit2sx_ee_memory_bus
    )

    add_test(
        NAME orbit2sx_ee_memory_bus_tests
        COMMAND orbit2sx_ee_memory_bus_tests
    )
endif()
