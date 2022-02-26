cmake_minimum_required(VERSION 3.15)

project(Crinkler LANGUAGES CXX RC)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Cache option defaults
set(DOWNLOAD_NASM OFF CACHE BOOL "Download nasm if not present.")
set(NASM_PATH "${PROJECT_BINARY_DIR}/downloads/nasm-2.15.05-win32/" CACHE PATH "Search path for nasm.")

# Download nasm if configured in cache and not present.
find_program(NASM NAMES nasm.exe HINTS "${NASM_PATH}/nasm-2.15.05/" "${NASM_PATH}")
if(${NASM} MATCHES NASM-NOTFOUND AND DOWNLOAD_NASM)
    set(NASM_URL "https://www.nasm.us/pub/nasm/releasebuilds/2.15.05/win32/nasm-2.15.05-win32.zip")
    set(NASM_ARCHIVE_FILE "${CMAKE_CURRENT_BINARY_DIR}/downloads/nasm-2.15.05-win32.zip")
    find_file(NASM_ARCHIVE NAMES nasm-2.15.05-win32.zip HINTS "${CMAKE_CURRENT_BINARY_DIR}/downloads/")
    if(${NASM_ARCHIVE} MATCHES NASM_ARCHIVE-NOTFOUND)
        message(STATUS "Downloading nasm from ${NASM_URL}...")
        file(DOWNLOAD ${NASM_URL} ${NASM_ARCHIVE_FILE} SHOW_PROGRESS)
    endif()
    file(ARCHIVE_EXTRACT INPUT ${NASM_ARCHIVE_FILE} DESTINATION "${NASM_PATH}")
    find_program(NASM NAMES nasm.exe HINTS "${NASM_PATH}/nasm-2.15.05/")
endif()
message(STATUS "Found nasm: ${NASM}")

# Build-type-dependent configuration
add_compile_definitions(WIN32 _WINDOWS _USRDLL COMPRESSOR_EXPORTS)
if(${CMAKE_BUILD_TYPE} MATCHES "Debug")
    add_compile_definitions(_DEBUG D3D_DEBUG_INFO)
elseif(${CMAKE_BUILD_TYPE} MATCHES "Release")
    add_compile_definitions(NDEBUG _CRT_SECURE_NO_DEPRECATE)
endif()

# Architecture-dependent configuration
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    link_directories("${PROJECT_SOURCE_DIR}/external/distorm/x64/")
    set(NASM_OPTIONS -fwin64)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    link_directories("${PROJECT_SOURCE_DIR}/external/distorm/x86/")
    set(NASM_OPTIONS -fwin32)
endif()
set(NASM_OPTIONS ${NASM_OPTIONS} -Xvc)

# Ingredients
add_subdirectory(source/Compressor)
add_subdirectory(source/CompressorExample)
add_subdirectory(source/Crinkler)
add_subdirectory(source/ExportScraper)
