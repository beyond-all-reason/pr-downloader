/* This file is part of pr-downloader (GPL v2 or later), see the LICENSE file */

#ifndef _DOWNLOAD_DATA_H
#define _DOWNLOAD_DATA_H

#include <memory>
#include <vector>

class Mirror;
class IDownload;
class CurlWrapper;

// Used for computing progress across multiple DownloadData downloaded in
// parallel.
class DownloadDataPack
{
public:
	int size = 0;
	int progress = 0;
};

class DownloadData
{
public:
	DownloadData();
	std::unique_ptr<CurlWrapper> curlw; // curl_easy_handle
	Mirror* mirror = nullptr;     // mirror used
	IDownload* download;
	DownloadDataPack* data_pack = nullptr;

	void updateProgress(double total, double done);
};

#endif
