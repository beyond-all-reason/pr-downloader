/* This file is part of pr-downloader (GPL v2 or later), see the LICENSE file */

#include "Download.h"
#include "Logger.h"
#include "FileSystem/IHash.h"
#include "FileSystem/File.h"

#include <string>
#include <list>
#include <stdio.h>

IDownload::IDownload(std::string name_, std::string origin_name_,
                     DownloadEnum::Category cat_, download_type typ_)
    : cat(cat_)
    , dltype(typ_)
    , name(std::move(name_))
    , origin_name(std::move(origin_name_))
{
}

std::string IDownload::getMirror(unsigned i) const
{
	assert(i < mirrors.size());
	return mirrors[i];
}

int IDownload::getMirrorCount() const
{
	return mirrors.size();
}

bool IDownload::addMirror(const std::string& url)
{
	LOG_DEBUG("%s", url.c_str());
	if (origin_name.empty()) {
		origin_name = url;
	}
	this->mirrors.emplace_back(url);
	return true;
}

bool IDownload::addDepend(const std::string& depend)
{
	this->depend.push_back(depend);
	return true;
}

unsigned int IDownload::getProgress() const
{
	if (dltype == TYP_RAPID || dltype == TYP_HTTP)
		return progress;
	return 0;
}

void IDownload::updateProgress(unsigned int new_progress)
{
	progress = new_progress;
}
