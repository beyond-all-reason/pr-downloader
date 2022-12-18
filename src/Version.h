/* This file is part of pr-downloader (GPL v2 or later), see the LICENSE file */

#pragma once

const char* getVersion();
const char* getAgent();

enum class Platform {
	Windows_x64,
	Linux_x64,
};

#if defined(_WIN64)
constexpr Platform PRD_CURRENT_PLATFORM = Platform::Windows_x64;
#elif defined(__linux__) && defined(__x86_64__)
constexpr Platform PRD_CURRENT_PLATFORM = Platform::Linux_x64;
#endif

const char* platformToString(Platform Platform);
