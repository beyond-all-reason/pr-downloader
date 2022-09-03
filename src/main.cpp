/* This file is part of pr-downloader (GPL v2 or later), see the LICENSE file */

#include "Util.h"
#include "Version.h"
#include "Logger.h"
#include "pr-downloader.h"

#include <lsl/lslutils/platform.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

enum {
	RAPID_DOWNLOAD = 0,
	RAPID_VALIDATE,
	RAPID_VALIDATE_DELETE,
	HTTP_DOWNLOAD,
	FILESYSTEM_WRITEPATH,
	FILESYSTEM_DUMPSDP,
	FILESYSTEM_VALIDATESDP,
	DOWNLOAD_MAP,
	DOWNLOAD_GAME,
	DOWNLOAD_ENGINE,
	DISABLE_LOGGING,
	DISABLE_FETCH_DEPENDS,
	HELP,
	SHOW_VERSION
};

static struct option long_options[] = {
    {"rapid-download", 1, 0, RAPID_DOWNLOAD},
    {"rapid-validate", 0, 0, RAPID_VALIDATE},
    {"delete", 0, 0, RAPID_VALIDATE_DELETE},
    {"dump-sdp", 1, 0, FILESYSTEM_DUMPSDP},
    {"validate-sdp", 1, 0, FILESYSTEM_VALIDATESDP},
    {"http-download", 1, 0, HTTP_DOWNLOAD},
    {"download-map", 1, 0, DOWNLOAD_MAP},
    {"download-game", 1, 0, DOWNLOAD_GAME},
    {"download-engine", 1, 0, DOWNLOAD_ENGINE},
    {"filesystem-writepath", 1, 0, FILESYSTEM_WRITEPATH},
    {"disable-logging", 0, 0, DISABLE_LOGGING},
    {"disable-fetch-depends", 0, 0, DISABLE_FETCH_DEPENDS},
    {"help", 0, 0, HELP},
    {"version", 0, 0, SHOW_VERSION},
    {0, 0, 0, 0}};

void show_version()
{
	LOG("pr-downloader %s (%s)\n", getVersion(), LSL::Util::GetCurrentPlatformString());
}

void show_help(const char* cmd)
{
	int i = 0;
	LOG("Usage: %s \n", cmd);
	bool append = false;
	while (long_options[i].name != 0) {
		if (append) {
			LOG("\n");
		}
		append = true;
		LOG("  --%s", long_options[i].name);
		if (long_options[i].has_arg != 0)
			LOG(" <name>");
		i++;
	}
	LOG("\n\n");
	LOG("Environment variables:\n");
	LOG("  PRD_RAPID_USE_STREAMER=[true]|false\n");
	LOG("\tWhatever to use streamer.cgi for downloading.\n");
	LOG("  PRD_RAPID_REPO_MASTER=[https://repos.springrts.com/repos.gz]\n");
	LOG("\tURL of the rapid repo master\n");
	LOG("  PRD_MAX_HTTP_REQS_PER_SEC=[0]\n");
	LOG("\tLimit on number of requests per second for HTTP downloading, 0 = unlimited\n");
	LOG("  PRD_HTTP_SEARCH_URL=[https://springfiles.springrts.com/json.php]\n");
	LOG("\tURL of springfiles used to download maps etc.\n");
	LOG("  PRD_DISABLE_CERT_CHECK=[false]|true\n");
	LOG("\tAllows to disable TLS certificate validation, useful for testing.\n");
	exit(1);
}

void show_results(int count)
{
	for (int i = 0; i < count; i++) {
		downloadInfo dl;
		DownloadGetInfo(i, dl);
		LOG_DEBUG("Download path: %s", dl.filename);
	}
}

int main(int argc, char** argv)
{
	ensureUtf8Argv(&argc, &argv);
	show_version();
	if (argc < 2)
		show_help(argv[0]);

	bool removeinvalid = false;
	bool fsset = false;

	while (true) {
		const int c = getopt_long(argc, argv, "", long_options, nullptr);
		if (c == -1)
			break;
		switch(c) {
			case RAPID_VALIDATE_DELETE: {
				removeinvalid = true;
				break;
			}
			case FILESYSTEM_WRITEPATH: {
				fsset = true;
				DownloadSetConfig(CONFIG_FILESYSTEM_WRITEPATH, optarg);
				break;
			}
			case DISABLE_LOGGING:
				DownloadDisableLogging(true);
				break;
			case DISABLE_FETCH_DEPENDS: {
				bool fetch_depends = false;
				DownloadSetConfig(CONFIG_FETCH_DEPENDS, &fetch_depends);
				break;
			}
			default:
				break;
		}
	}
	if (!fsset) {
		DownloadSetConfig(CONFIG_FILESYSTEM_WRITEPATH, "");
	}

	optind = 1; // reset argv scanning
	std::vector<DownloadSearchItem> items;
	while (true) {
		const int c = getopt_long(argc, argv, "", long_options, nullptr);
		if (c == -1)
			break;
		switch (c) {
			case RAPID_DOWNLOAD: {
				items.emplace_back(DownloadEnum::CAT_GAME, optarg);
				break;
			}
			case RAPID_VALIDATE: {
				if (!DownloadRapidValidate(removeinvalid)) {
					LOG_ERROR("Validation of the rapid pool failed");
					return 1;
				}
				return 0;
			}
			case FILESYSTEM_DUMPSDP: {
				if (!DownloadDumpSDP(optarg)) {
					LOG_ERROR("Error dumping sdp");
					return 1;
				}
				return 0;
			}
			case FILESYSTEM_VALIDATESDP: {
				if (!ValidateSDP(optarg)) {
					LOG_ERROR("Error validating SDP");
					return 1;
				}
				return 0;
			}
			case DOWNLOAD_MAP: {
				items.emplace_back(DownloadEnum::CAT_MAP, optarg);
				break;
			}
			case DOWNLOAD_GAME: {
				items.emplace_back(DownloadEnum::CAT_GAME, optarg);
				break;
			}
			case SHOW_VERSION:
				show_version();
				return 0;
			case DOWNLOAD_ENGINE: {
				items.emplace_back(DownloadEnum::CAT_ENGINE, optarg);
				break;
			}
			case HELP: {
				show_help(argv[0]);
				return 0;
			}
		}
	}
	if (optind < argc) {
		while (optind < argc) {
			items.emplace_back(DownloadEnum::CAT_NONE, argv[optind]);
			optind++;
		}
	}
	DownloadInit();
	int count = DownloadSearch(items);
	if (count < 0) {
		DownloadShutdown();
		return 1;
	}
	bool found_all = true;
	for (auto& item: items) {
		if (!item.found) {
			found_all = false;
			LOG_ERROR("Failed to find '%s' for download", item.name.c_str());
		}
	}
	if (!found_all) {
		DownloadShutdown();
		return 1;
	}
	for (int i = 0; i < count; ++i) {
		DownloadAdd(i);
	}
	const int dlres = DownloadStart();
	if (dlres == 0) {
		show_results(count);
		LOG_INFO("Download complete!");
	} else {
		LOG_ERROR("Error occurred while downloading: %d", dlres);
	}
	DownloadShutdown();
	return dlres;
}
