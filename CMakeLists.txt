cmake_minimum_required(VERSION 3.16)

option(PRD_TESTS "Build tests" OFF)
if(PRD_TESTS)
  list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()

project(pr-downloader VERSION 0.8)

message(${CMAKE_SYSTEM_NAME})
message(${WIN32} "asd")

option(PRD_LINK_STATIC "link pr-downloader static or dynamic" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (WIN32)
    add_definitions(-DUNICODE -D_UNICODE)
endif ()

if (MSVC)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX)
endif ()

find_package(CURL 7.84 REQUIRED)
find_package(ZLIB REQUIRED)
find_package(jsoncpp REQUIRED)

# Minizip is not providing a cmake files under debian, so we fallback
# to pkg-config when needed
find_package(minizip QUIET)
if (minizip_FOUND)
    set(MINIZIP_TARGET minizip::minizip)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(minizip REQUIRED IMPORTED_TARGET minizip)
    set(MINIZIP_TARGET PkgConfig::minizip)
endif()

find_path(READERWRITERQUEUE_INCLUDE_DIRS "readerwriterqueue/readerwriterqueue.h")

add_subdirectory(src)

if (PRD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif ()
