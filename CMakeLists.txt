cmake_minimum_required(VERSION 3.15)

if(APPLE AND NOT TARGET_IOS)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING
        "Minimum OS X deployment version")
endif()

project(Play)

include(GNUInstallDirs)

set(CMAKE_MODULE_PATH
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/Dependencies/cmake-modules
    ${CMAKE_MODULE_PATH}
)

include(Header)

set(BUILD_PLAY ON CACHE BOOL "Build Play! Emulator")
set(BUILD_PSFPLAYER OFF CACHE BOOL "Build PsfPlayer")
set(BUILD_TESTS OFF CACHE BOOL "Build Tests")
set(USE_AOT_CACHE OFF CACHE BOOL "Use AOT block cache")
set(BUILD_AOT_CACHE OFF CACHE BOOL "Build AOT block cache")
set(BUILD_LIBRETRO_CORE OFF CACHE BOOL "Build Libretro Core")

set(PROJECT_NAME "Play!")
set(PROJECT_Version 0.30)

if(BUILD_TESTS)
    enable_testing()
endif()

find_package(Git)

if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git/")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --always
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        RESULT_VARIABLE GIT_DESCRIBE_RESULT
        OUTPUT_VARIABLE GIT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if(GIT_DESCRIBE_RESULT EQUAL 0)
        message("Building for ${GIT_TAG}.")
        set(PROJECT_Version ${GIT_TAG})
    else()
        message("Warning: Failed to obtain Git tag.")
    endif()
else()
    message("Warning: Not a git repo or git not found.")
endif()

add_definitions(-DPLAY_VERSION="${PROJECT_Version}")

set(PROJECT_LIBS)

include(PrecompiledHeader)

# Libretro is independent from the Android Play application.
if(BUILD_LIBRETRO_CORE)
    if(NOT MSVC)
        add_compile_options(
            $<$<NOT:$<COMPILE_LANGUAGE:Swift>>:-fPIC>
        )
    endif()

    add_subdirectory(Source/ui_libretro)
endif()

# Main Play application.
if(BUILD_PLAY)

    if(TARGET_PLATFORM_UNIX OR
       TARGET_PLATFORM_MACOS OR
       TARGET_PLATFORM_WIN32 OR
       USE_QT)

        add_subdirectory(Source/ui_qt/)
        add_subdirectory(tools/ElfView)
        add_subdirectory(tools/DiscChecker)

    endif()

    if(NOT USE_QT)

        if(TARGET_PLATFORM_IOS)
            add_subdirectory(Source/ui_ios/)
        endif()

        if(TARGET_PLATFORM_ANDROID)
            add_subdirectory(Source/ui_android/)
        endif()

        if(TARGET_PLATFORM_JS)
            add_subdirectory(Source/ui_js)
        endif()

    endif()
endif()

# Desktop tests are not part of the Android build.
if(BUILD_TESTS AND NOT TARGET_PLATFORM_ANDROID)

    add_subdirectory(tools/AutoTest/)
    add_subdirectory(tools/GsAreaTest/)
    add_subdirectory(tools/McServTest/)
    add_subdirectory(tools/SpuTest/)
    add_subdirectory(tools/VuTest/)

    add_subdirectory(deps/Framework/build_cmake/Tests)

endif()

# Desktop utility only.
if(NOT TARGET_PLATFORM_ANDROID)
    add_subdirectory(tools/NamcoSys147NANDTools)
endif()

if(BUILD_PSFPLAYER AND NOT TARGET_PLATFORM_ANDROID)
    add_subdirectory(tools/PsfPlayer)
endif()
