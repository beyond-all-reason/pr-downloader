/* This file is part of pr-downloader (GPL v2 or later), see the LICENSE file */

#ifndef HTTP_DOWNLOAD_H
#define HTTP_DOWNLOAD_H

#include "Downloader/IDownloader.h"

#include <curl/curl.h>
#include <string>
#include <list>

class HashSHA1;
class CFile;
class DownloadData;

class CHttpDownloader : public IDownloader
{
public:
	virtual bool search(std::list<IDownload*>& result, const std::string& name,
			    DownloadEnum::Category = DownloadEnum::CAT_NONE) override;
	virtual bool download(std::list<IDownload*>& download,
			      int max_parallel = 10) override;
	static bool DownloadUrl(const std::string& url, std::string& res);
	static bool ParseResult(const std::string& name, const std::string& json,
				std::list<IDownload*>& res);

private:
	/**
  *	process curl messages
  *		- verify
  *		- starts redownloads piece, when piece dl failed from some
  *mirror
  *		- keep some stats (mark broken mirrors, downloadspeed)
  *	@returns false, when some fatal error occured -> abort
  */
	bool processMessages(CURLM* curlm, std::vector<DownloadData*>* to_retry);
	DownloadData* getDataByHandle(const std::vector<DownloadData*>& downloads,
				      const CURL* easy_handle) const;
};

#endif
