add_library(orbit2sx_ee_memory STATIC EE_Memory.cpp EE_Memory.h)

target_include_directories(orbit2sx_ee_memory
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(orbit2sx_ee_memory PUBLIC cxx_std_11)

if(ORBIT2SX_BUILD_TESTS)
    add_executable(orbit2sx_ee_memory_tests tests/EE_MemoryTests.cpp)
    target_link_libraries(orbit2sx_ee_memory_tests PRIVATE orbit2sx_ee_memory)
    add_test(NAME orbit2sx_ee_memory_tests COMMAND orbit2sx_ee_memory_tests)
endif()
