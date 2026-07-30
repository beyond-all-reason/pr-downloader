# SPDX-License-Identifier: GPL-2.0-or-later
#
# Points dependency lookup at a mingwlibs64 checkout. Mirrors the windows branch
# of the engine's top level CMakeLists (beyond-all-reason/RecoilEngine), which
# pr-downloader does not do for itself because inside the engine build it
# inherits all of this from the parent scope.

if(NOT MINGWLIBS AND DEFINED ENV{MINGWLIBS})
    set(MINGWLIBS $ENV{MINGWLIBS} CACHE PATH "Location of the mingwlibs64 package")
endif()

if(NOT EXISTS "${MINGWLIBS}" OR NOT IS_DIRECTORY "${MINGWLIBS}")
    message(FATAL_ERROR "MINGWLIBS '${MINGWLIBS}' is not a valid directory")
endif()

# mingwlibs64 ships DLLs with no accompanying import libraries, which CMake
# refuses to consider as link targets by default since 3.17.
# https://discourse.cmake.org/t/findzlib-fails-starting-with-cmake-3-17/6046/4
list(PREPEND CMAKE_FIND_LIBRARY_SUFFIXES ".dll")

set(CMAKE_PREFIX_PATH ${MINGWLIBS})
set(CMAKE_LIBRARY_PATH ${MINGWLIBS}/dll ${MINGWLIBS}/lib)
set(CMAKE_FIND_ROOT_PATH ${MINGWLIBS} ${CMAKE_FIND_ROOT_PATH})
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
