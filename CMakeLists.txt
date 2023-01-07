cmake_minimum_required(VERSION 3.19)

project(pr-downloader)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_MODULES_DOWNLOADER "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_MODULES_DOWNLOADER}")
include(UtilGit)
if(GIT_FOUND)
    git_util_describe(PR_DOWNLOADER_VERSION "${CMAKE_CURRENT_SOURCE_DIR}/src" "*")
endif()

# Version couldn't be detected, we guess its compiled from tarball
if("${PR_DOWNLOADER_VERSION}" STREQUAL "")
    set(PR_DOWNLOADER_VERSION "tarball")
endif()
message(STATUS "PR-Downloader version: ${PR_DOWNLOADER_VERSION}")

set(PRD_BINDIR "." CACHE PATH "Where to install pr-downloader binary.")

# Resolve dependencies.

# CURL
if(UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(libcurl REQUIRED IMPORTED_TARGET libcurl>=7.84)
    set(CURL_TARGET PkgConfig::libcurl)
else()
    find_package(CURL 7.84 REQUIRED)
    set(CURL_TARGET CURL::libcurl)
endif()

# ZLIB
find_package(ZLIB REQUIRED)

# jsoncpp
if(UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(jsoncpp IMPORTED_TARGET jsoncpp)
    if(jsoncpp_FOUND)
        set(JSONCPP_TARGET PkgConfig::jsoncpp)
    endif()
endif()
if(NOT jsoncpp_FOUND)
    message(STATUS "Not found jsoncpp in system, using integrated version")
    add_subdirectory(src/lib/jsoncpp)
    set(JSONCPP_TARGET pr-jsoncpp)
endif()

# minizip
if(UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(minizip IMPORTED_TARGET minizip)
    if(minizip_FOUND)
        set(MINIZIP_TARGET PkgConfig::minizip)
    endif()
else()
    find_package(minizip)
    if(minizip_FOUND)
        set(MINIZIP_TARGET minizip::minizip)
    endif()
endif()
if(NOT minizip_FOUND)
    message(STATUS "Not found minizip in system, using integrated version")
    add_subdirectory(src/lib/minizip)
    set(MINIZIP_TARGET pr-minizip)
endif()

add_subdirectory(src/lib)

# Include actual program source
add_subdirectory(src)

option(PRD_TESTS "Build tests" OFF)
if(PRD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
