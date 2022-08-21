/* This file is part of pr-downloader (GPL v2 or later), see the LICENSE file */

#include "Repo.h"
#include "FileSystem/FileSystem.h"
#include "Downloader/IDownloader.h"
#include "RapidDownloader.h"
#include "Sdp.h"
#include "Util.h"
#include "Logger.h"

#include <zlib.h>
#include <stdio.h>
#include <cassert>

CRepo::CRepo(const std::string& repourl, const std::string& _shortname,
	     CRapidDownloader* rapid)
    : repourl(repourl)
    , rapid(rapid)
    , shortname(_shortname)
{
}

IDownload* CRepo::getDownload()
{
	std::string tmp;
	urlToPath(repourl, tmp);
	LOG_DEBUG("%s", tmp.c_str());
	tmpFile = fileSystem->getSpringDir() + PATH_DELIMITER + "rapid" +
		  PATH_DELIMITER + tmp + PATH_DELIMITER + "versions.gz";
	fileSystem->createSubdirs(CFileSystem::DirName(tmpFile));
	// first try already downloaded file, as repo master file rarely changes
	if ((fileSystem->fileExists(tmpFile)) && !fileSystem->isOlder(tmpFile, REPO_RECHECK_TIME))
		return nullptr;

	IDownload* dl = new IDownload(tmpFile);
	dl->noCache = true;
	dl->addMirror(repourl + "/versions.gz");
	return dl;
}

bool CRepo::parse()
{
	assert(!tmpFile.empty());
	if (tmpFile.empty()) {
		LOG_DEBUG("tmpfile empty, repo not initialized?");
		return false;
	}
	LOG_DEBUG("%s", tmpFile.c_str());
	FILE* f = fileSystem->propen(tmpFile, "rb");
	if (f == nullptr) {
		LOG_ERROR("Could not open %s", tmpFile.c_str());
		return false;
	}
	gzFile fp = gzdopen(fileno(f), "rb");
	if (fp == Z_NULL) {
		fclose(f);
		LOG_ERROR("Could not gzdopen %s", tmpFile.c_str());
		return false;
	}

	char buf[IO_BUF_SIZE];
	sdps.clear();
	while ((gzgets(fp, buf, sizeof(buf))) != Z_NULL) {
		for (unsigned int i = 0; i < sizeof(buf); i++) {
			if (buf[i] == '\n') {
				buf[i] = 0;
				break;
			}
		}

		const std::string line = buf;
		const std::vector<std::string> items = tokenizeString(line, ',');
		if (items.size() < 4) {
			LOG_ERROR("Invalid line: %s", line.c_str());
			gzclose(fp);
			fclose(f);
			return false;
		}

		// create new repo from url
		rapid->addRemoteSdp(CSdp { items[0], items[1], items[3], items[2], repourl });
	}
	int errnum = Z_OK;
	bool ok = true;
	const char* errstr = gzerror(fp, &errnum);
	if (errnum != Z_OK && errnum != Z_STREAM_END) {
		LOG_ERROR("%d %s\n", errnum, errstr);
		ok = false;
	}
	gzclose(fp);
	fclose(f);
	return ok;
}

bool CRepo::deleteRepoFile()
{
	return fileSystem->removeFile(tmpFile);
}
