/* This file is part of pr-downloader (GPL v2 or later), see the LICENSE file */

#pragma once

#include <string>
#include <cstdio>

class CFile
{
public:
	/**
	* general file abstraction for writing files.
	*/
	~CFile();
	/**
	* open file, it always creates a temporary file first.
	*/
	bool Open(const std::string& filename);
	/**
	* close file. If discard is set, removes the file as something is wrong
	* with its contents.
	*/
	bool Close(bool discard = false);
	/**
	* write bufsize bytes to the file.
	*/
	bool Write(const char* buf, int bufsize);

private:
	std::string filename;
	std::string tmpfile;
	FILE* handle = nullptr;   // file handle
};
