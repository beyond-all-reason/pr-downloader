/* This file is part of pr-downloader (GPL v2 or later), see the LICENSE file */

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "File.h"
#include "FileSystem.h"
#include "Logger.h"

CFile::~CFile()
{
	Close();
}

void CFile::Close(bool discard)
{
	if (handle == nullptr) return;

	LOG_DEBUG("closing %s%s", filename.c_str(), discard ? ", with discard" : "");

	fclose(handle);
	handle = nullptr;

	if (discard) {
		fileSystem->removeFile(tmpfile);
		return;
	}	
	// delete possible existing destination file
	if (fileSystem->fileExists(filename)) {
		fileSystem->removeFile(filename);
	}
	fileSystem->Rename(tmpfile, filename);
}

bool CFile::Open(const std::string& filename)
{
	assert(handle == nullptr);
	LOG_DEBUG("Opening %s", filename.c_str());
	this->filename = filename;
	fileSystem->createSubdirs(CFileSystem::DirName(filename));
	tmpfile = filename + ".tmp";
	handle = fileSystem->propen(tmpfile, "wb");
	if (handle == nullptr) {
		LOG_ERROR("open(%s): %s", filename.c_str(), strerror(errno));
		return false;
	}
	return true;
}

int CFile::Write(const char* buf, int bufsize)
{
	assert(bufsize > 0);
	clearerr(handle);
	constexpr int PIECES = 1;
	const int res = fwrite(buf, bufsize, PIECES, handle);
	if (res != PIECES)
		LOG_ERROR("write error %s (%d):  %s", filename.c_str(), res,
			  strerror(errno));
	//	LOG("wrote bufsize %d", bufsize);
	if (ferror(handle) != 0) {
		LOG_ERROR("Error in write(): %s %s", strerror(errno), filename.c_str());
		abort();
	}
	if (feof(handle)) {
		LOG_ERROR("EOF in write(): %s %s", strerror(errno), filename.c_str());
	}
	return bufsize;
}
