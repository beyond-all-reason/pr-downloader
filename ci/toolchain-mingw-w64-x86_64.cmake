# SPDX-License-Identifier: GPL-2.0-or-later
#
# Cross compile to windows x86_64 with mingw-w64. Mirrors the engine's toolchain
# file (docker-build-v2/images/amd64-windows/toolchain.cmake in
# beyond-all-reason/RecoilEngine), which covers the compiler only.
#
# Dependency resolution lives in mingwlibs64.cmake, pulled in after project()
# because some of what it sets gets overwritten by CMake's platform modules if
# set here.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# The -posix suffixed drivers select the winpthreads threading model, which is
# what makes libstdc++ ship working std::thread / std::mutex.
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc-posix)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++-posix)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_DLLTOOL x86_64-w64-mingw32-dlltool)

set(CMAKE_C_FLAGS_INIT "-static-libstdc++ -static-libgcc")
set(CMAKE_CXX_FLAGS_INIT "-static-libstdc++ -static-libgcc")

set(CMAKE_PROJECT_pr-downloader_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/mingwlibs64.cmake")
