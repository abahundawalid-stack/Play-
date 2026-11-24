add_library(orbit2sx_ee_device_registry STATIC
    EE_DeviceRegistry.cpp
    EE_DeviceRegistry.h
)

target_include_directories(orbit2sx_ee_device_registry
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../memory_bus
        ${CMAKE_CURRENT_SOURCE_DIR}/../memory_map
        ${CMAKE_CURRENT_SOURCE_DIR}/../memory
)

target_compile_features(orbit2sx_ee_device_registry
    PUBLIC
        cxx_std_11
)

target_link_libraries(orbit2sx_ee_device_registry
    PUBLIC
        orbit2sx_ee_memory_bus
        orbit2sx_ee_memory_map
        orbit2sx_ee_memory
)

if(ORBIT2SX_BUILD_TESTS)
    add_executable(orbit2sx_ee_device_registry_tests
        tests/EE_DeviceRegistryTests.cpp
    )

    target_link_libraries(orbit2sx_ee_device_registry_tests
        PRIVATE
            orbit2sx_ee_device_registry
    )

    add_test(
        NAME orbit2sx_ee_device_registry_tests
        COMMAND orbit2sx_ee_device_registry_tests
    )
endif()
