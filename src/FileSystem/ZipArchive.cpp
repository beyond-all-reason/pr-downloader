/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "ZipArchive.h"

#include <algorithm>
#include <stdexcept>

#include "Logger.h"

CZipArchive::CZipArchive(const std::string& archiveName)
	: IArchive(archiveName)
{
	zip = unzOpen(archiveName.c_str());
	if (!zip) {
		LOG_ERROR("Error opening %s", archiveName.c_str());
		return;
	}

	// We need to map file positions to speed up opening later
	for (int ret = unzGoToFirstFile(zip); ret == UNZ_OK; ret = unzGoToNextFile(zip)) {
		unz_file_info info;
		char fName[512];

		unzGetCurrentFileInfo(zip, &info, fName, 512, nullptr, 0, nullptr, 0);

		const std::string fLowerName = fName;
		if (fLowerName.empty()) {
			continue;
		}
		const char last = fLowerName[fLowerName.length() - 1];
		if ((last == '/') || (last == '\\')) {
			continue;  // exclude directory names
		}

		FileData fd;
		unzGetFilePos(zip, &fd.fp);
		fd.size = info.uncompressed_size;
		fd.origName = fName;
		fd.crc = info.crc;
		fd.mode = 0755;
		if (info.external_fa > 0) {
			fd.mode = info.external_fa >> 16;
		}
		fileData.push_back(fd);
		//		lcNameIndex[fLowerName] = fileData.size() - 1;
	}
}

CZipArchive::~CZipArchive()
{
	if (zip) {
		unzClose(zip);
	}
}

bool CZipArchive::IsOpen() const
{
	return zip != nullptr;
}

unsigned int CZipArchive::NumFiles() const
{
	return fileData.size();
}

void CZipArchive::FileInfo(unsigned int fid, std::string& name, int& size, int& mode) const
{
	//	assert(IsFileId(fid));

	name = fileData[fid].origName;
	size = fileData[fid].size;
	mode = fileData[fid].mode;
}

unsigned int CZipArchive::GetCrc32(unsigned int fid)
{
	//	assert(IsFileId(fid));

	return fileData[fid].crc;
}

// To simplify things, files are always read completely into memory from
// the zip-file, since zlib does not provide any way of reading more
// than one file at a time
bool CZipArchive::GetFile(unsigned int fid, std::vector<unsigned char>& buffer)
{
	// Prevent opening files on missing/invalid archives
	if (!zip) {
		return false;
	}
	//	assert(IsFileId(fid));

	unzGoToFilePos(zip, &fileData[fid].fp);

	unz_file_info fi;
	unzGetCurrentFileInfo(zip, &fi, nullptr, 0, nullptr, 0, nullptr, 0);

	if (unzOpenCurrentFile(zip) != UNZ_OK) {
		return false;
	}

	buffer.resize(fi.uncompressed_size);

	bool ret = true;
	if (!buffer.empty() &&
	    unzReadCurrentFile(zip, &buffer[0], fi.uncompressed_size) != (int)fi.uncompressed_size) {
		ret = false;
	}

	if (unzCloseCurrentFile(zip) == UNZ_CRCERROR) {
		ret = false;
	}

	if (!ret) {
		buffer.clear();
	}

	return ret;
}
