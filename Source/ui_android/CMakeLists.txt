cmake_minimum_required(VERSION 3.18)

set(CMAKE_MODULE_PATH
	${CMAKE_CURRENT_SOURCE_DIR}/../../deps/Dependencies/cmake-modules
	${CMAKE_MODULE_PATH}
)
include(Header)

project(PlayUI)

# Optimasi C++ untuk performa emulasi maksimal
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if(NOT TARGET PlayCore)
	add_subdirectory(
		${CMAKE_CURRENT_SOURCE_DIR}/../
		${CMAKE_CURRENT_BINARY_DIR}/Source
	)
endif()
list(APPEND UI_ANDROID_PROJECT_LIBS PlayCore)

if(NOT TARGET gsh_opengl)
	add_subdirectory(
		${CMAKE_CURRENT_SOURCE_DIR}/../gs/GSH_OpenGL
		${CMAKE_CURRENT_BINARY_DIR}/gs/GSH_OpenGL
	)
endif()
list(INSERT UI_ANDROID_PROJECT_LIBS 0 gsh_opengl)

if(NOT TARGET gsh_vulkan)
	add_subdirectory(
		${CMAKE_CURRENT_SOURCE_DIR}/../gs/GSH_Vulkan
		${CMAKE_CURRENT_BINARY_DIR}/gs/GSH_Vulkan
	)
endif()
list(INSERT UI_ANDROID_PROJECT_LIBS 0 gsh_vulkan)

if(NOT TARGET ui_shared)
	add_subdirectory(
		${CMAKE_CURRENT_SOURCE_DIR}/../ui_shared
		${CMAKE_CURRENT_BINARY_DIR}/ui_shared
	)
endif()
list(APPEND UI_ANDROID_PROJECT_LIBS ui_shared)

set(ANDROID_SRC_FILES
	Egl/EglDisplayManager.cpp
	Egl/EglDisplayManager.h
	BootablesInterop.cpp
	com_virtualapplications_play_Bootable.cpp
	com_virtualapplications_play_Bootable.h
	GSH_OpenGLAndroid.cpp
	GSH_OpenGLAndroid.h
	GSH_VulkanAndroid.cpp
	GSH_VulkanAndroid.h
	InputManager.cpp
	NativeInterop.cpp
	NativeShared.cpp
	NativeWindowUpdateListener.h
	SettingsManager.cpp
	SH_OpenSL.cpp
	StatsInterop.cpp
)

# Pastikan EGL dan GLESv3 ikut ditautkan secara eksplisit
list(APPEND UI_ANDROID_PROJECT_LIBS android log OpenSLES EGL GLESv3)

add_library(Play SHARED ${ANDROID_SRC_FILES})

# Dukungan 16 KB Page Size (Wajib untuk Android 15+ / Google Play)
target_link_options(Play PRIVATE "-Wl,-z,max-page-size=16384")

# Optimasi agresif khusus build Release (LTO & Fast Math aman)
target_compile_options(Play PRIVATE
	$<$<CONFIG:Release>:-O3 -flto>
)
target_link_options(Play PRIVATE
	$<$<CONFIG:Release>:-flto>
)

target_link_libraries(Play PRIVATE ${UI_ANDROID_PROJECT_LIBS})
