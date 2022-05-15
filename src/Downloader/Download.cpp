/* This file is part of pr-downloader (GPL v2 or later), see the LICENSE file */

#include "Download.h"
#include "Logger.h"
#include "FileSystem/IHash.h"
#include "FileSystem/File.h"
#include "Mirror.h"

#include <string>
#include <list>
#include <stdio.h>

IDownload::IDownload(const std::string& name, const std::string& origin_name,
		     DownloadEnum::Category cat, download_type typ)
    : cat(cat)
    , dltype(typ)
    , name(name)
    , origin_name(origin_name)
{
}

IDownload::~IDownload()
{
	if (hash != nullptr)
		delete hash;
	hash = nullptr;
	if (file != nullptr) {
		delete file;
		file = nullptr;
	}
}

std::string IDownload::getUrl() const
{
	return mirrors.empty() ? "" : mirrors[0]->url;
}

Mirror* IDownload::getMirror(unsigned i) const
{
	assert(i < mirrors.size());
	return mirrors[i].get();
}

Mirror* IDownload::getFastestMirror()
{
	int max = -1;
	int pos = -1;
	for (unsigned i = 0; i < mirrors.size(); i++) {
		if (mirrors[i]->status ==
		    Mirror::STATUS_UNKNOWN) { // prefer mirrors with unknown status
			mirrors[i]->status =
			    Mirror::STATUS_OK; // set status to ok, to not use it again
			LOG_DEBUG("Mirror %d: status unknown", i);
			return mirrors[i].get();
		}
		if ((mirrors[i]->status != Mirror::STATUS_BROKEN) &&
		    (mirrors[i]->maxspeed > max)) {
			max = mirrors[i]->maxspeed;
			pos = i;
		}
		LOG_DEBUG("Mirror %d: (%d): %s", i, mirrors[i]->maxspeed,
			  mirrors[i]->url.c_str());
	}
	if (pos < 0) {
		LOG_DEBUG("no mirror selected");
		return nullptr;
	}
	LOG_DEBUG("Fastest mirror %d: (%d): %s", pos, mirrors[pos]->maxspeed,
		  mirrors[pos]->url.c_str());
	return mirrors[pos].get();
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
	this->mirrors.emplace_back(new Mirror(url));
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
