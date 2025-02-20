cmake_minimum_required(VERSION 3.16)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(WIN32)
	set(VCPKG_TARGET_TRIPLET "x64-windows-static")
endif()

if(DEFINED ENV{VCPKG_ROOT})
	set(toolchain "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
	message(STATUS "Using cmake toolchain file: ${toolchain}")
	set(CMAKE_TOOLCHAIN_FILE ${toolchain} CACHE STRING "")
endif()

# Configure project

project(unsync)

option(UNSYNC_USE_TLS "Include TLS support for encrypted connections via libretls" ON)
option(UNSYNC_USE_DEBUG_HEAP "Include support for debug memory allocator" ON)

# Validate compiler

if (NOT WIN32 AND NOT ${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU")
	message(FATAL_ERROR "Only GCC-11 or newer is currently supported.")
endif()

if (${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU" AND ${CMAKE_CXX_COMPILER_VERSION} VERSION_LESS 11)
	message(FATAL_ERROR "Only GCC-11 or newer is currently supported")
endif()

# Add unsync code

set(src
	Private/UnsyncBuffer.cpp
	Private/UnsyncBuffer.h
	Private/UnsyncCmdDiff.cpp
	Private/UnsyncCmdDiff.h
	Private/UnsyncCmdHash.cpp
	Private/UnsyncCmdHash.h
	Private/UnsyncCmdPatch.cpp
	Private/UnsyncCmdPatch.h
	Private/UnsyncCmdPush.cpp
	Private/UnsyncCmdPush.h
	Private/UnsyncCmdQuery.cpp
	Private/UnsyncCmdQuery.h
	Private/UnsyncCmdSync.cpp
	Private/UnsyncCmdSync.h
	Private/UnsyncCommon.h
	Private/UnsyncCompression.cpp
	Private/UnsyncCompression.h
	Private/UnsyncCore.cpp
	Private/UnsyncCore.h
	Private/UnsyncError.h
	Private/UnsyncFile.cpp
	Private/UnsyncFile.h
	Private/UnsyncHash.cpp
	Private/UnsyncHash.h
	Private/UnsyncHttp.cpp
	Private/UnsyncHttp.h
	Private/UnsyncJupiter.cpp
	Private/UnsyncJupiter.h
	Private/UnsyncLog.cpp
	Private/UnsyncLog.h
	Private/UnsyncMain.cpp
	Private/UnsyncMemory.cpp
	Private/UnsyncMemory.h
	Private/UnsyncMiniCb.cpp
	Private/UnsyncMiniCb.h
	Private/UnsyncPool.h
	Private/UnsyncProtocol.cpp
	Private/UnsyncProtocol.h
	Private/UnsyncProxy.cpp
	Private/UnsyncProxy.h
	Private/UnsyncRemote.cpp
	Private/UnsyncRemote.h
	Private/UnsyncScan.h
	Private/UnsyncSerialization.cpp
	Private/UnsyncSerialization.h
	Private/UnsyncSocket.cpp
	Private/UnsyncSocket.h
	Private/UnsyncTest.cpp
	Private/UnsyncTest.h
	Private/UnsyncThread.cpp
	Private/UnsyncThread.h
	Private/UnsyncUtil.cpp
	Private/UnsyncUtil.h
	Private/UnsyncVarInt.h
)

# Add third party code

set(blake3dir ${CMAKE_CURRENT_SOURCE_DIR}/../../ThirdParty/BLAKE3/0.3.7/c)

set(thirdparty
	${blake3dir}/blake3_dispatch.c
	${blake3dir}/blake3_impl.h
	${blake3dir}/blake3_portable.c
	${blake3dir}/blake3_sse41.c
	${blake3dir}/blake3_avx2.c
	${blake3dir}/blake3.c
	${blake3dir}/blake3.h
	ThirdParty/md5-sse2.cpp
	ThirdParty/md5-sse2.h
	ThirdParty/json11.cpp
	ThirdParty/json11.hpp
)

set(thirdparty ${thirdparty}
	ThirdParty/ig-debugheap/DebugHeap.h
	ThirdParty/ig-debugheap/DebugHeap.c
)

add_executable(unsync ${src} ${thirdparty} ${generated})

set_property(TARGET unsync PROPERTY CXX_STANDARD 20)
set_property(TARGET unsync PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

find_package(CLI11 CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(unofficial-http-parser CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)

target_include_directories(unsync PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty ${blake3dir})
target_compile_definitions(unsync PRIVATE BLAKE3_NO_SSE2=1 BLAKE3_NO_AVX512=1)

target_link_directories(unsync PRIVATE ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib)

target_link_libraries(unsync PRIVATE 
	CLI11::CLI11
	fmt::fmt-header-only # Header-only version as workaround for linker errors on Mac
	unofficial::http_parser::http_parser
	zstd::libzstd_static
)

if (UNSYNC_USE_TLS)
	find_path(LIBRESSL_INCLUDE_DIR tls.h)
	target_include_directories(unsync PRIVATE ${LIBRESSL_INCLUDE_DIR})
	target_link_libraries(unsync PRIVATE tls ssl crypto)
	target_compile_definitions(unsync PRIVATE UNSYNC_USE_TLS=1)
	message(STATUS "TLS support enabled")
endif(UNSYNC_USE_TLS)

if (UNSYNC_USE_DEBUG_HEAP)
	target_compile_definitions(unsync PRIVATE UNSYNC_USE_DEBUG_HEAP=1)
endif(UNSYNC_USE_DEBUG_HEAP)

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	target_compile_definitions(unsync PRIVATE
		UNSYNC_PLATFORM_WINDOWS=1
		UNSYNC_PLATFORM_UNIX=0
		UNSYNC_USE_CONCRT=1
		WIN32_LEAN_AND_MEAN=1
		NOMINMAX=1
	)
else()
	target_compile_definitions(unsync PRIVATE 
		UNSYNC_PLATFORM_UNIX=1
		UNSYNC_PLATFORM_WINDOWS=0
		UNSYNC_USE_CONCRT=0
	)
endif()

if(MSVC)

	add_definitions(-MP)
	add_definitions(-D_SILENCE_CXX17_OLD_ALLOCATOR_MEMBERS_DEPRECATION_WARNING=1)
	add_definitions(-D_CRT_SECURE_NO_WARNINGS=1)
	add_definitions(-D_WINSOCK_DEPRECATED_NO_WARNINGS=1)
	add_definitions(-D_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING=1)
	target_compile_options(unsync PRIVATE 
		-W4 -WX # level 4 warnings as errors
		-wd4100 # unreferenced formal parameter
	)

	# Less strict warnings in third party code
	foreach(file ${thirdparty})
		set_source_files_properties(${file} PROPERTIES COMPILE_FLAGS -W0)
	endforeach()

	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT unsync)

else()

	set_source_files_properties(${blake3dir}/blake3_sse41.c COMPILE_FLAGS -msse4.1)
	set_source_files_properties(${blake3dir}/blake3_avx2.c  COMPILE_FLAGS -mavx2)

endif()

source_group("Private" FILES ${src})
source_group("Generated" FILES ${generated})
source_group("ThirdParty" FILES ${thirdparty})
