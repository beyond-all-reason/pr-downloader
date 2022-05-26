/* This file is part of pr-downloader (GPL v2 or later), see the LICENSE file */

#include "HttpDownloader.h"
#include "Downloader/Download.h"

#include <algorithm>
#include <memory>
#include <stdio.h>
#include <string>
#include <sstream>
#include <stdlib.h>
#include <sys/types.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

#include <json/reader.h>

#include "DownloadData.h"
#include "FileSystem/FileSystem.h"
#include "FileSystem/File.h"
#include "FileSystem/HashMD5.h"
#include "FileSystem/HashSHA1.h"
#include "Util.h"
#include "Logger.h"
#include "Downloader/Mirror.h"
#include "Downloader/CurlWrapper.h"

static size_t WriteMemoryCallback(void* contents, size_t size, size_t nmemb,
				  void* userp)
{
	if (IDownloader::AbortDownloads()) {
		return -1;
	}

	const size_t realsize = size * nmemb;
	std::string* res = static_cast<std::string*>(userp);
	res->append((char*)contents, realsize);
	return realsize;
}

static int progress_func(DownloadData* data, double total, double done, double,
			 double)
{
	if (IDownloader::AbortDownloads()) {
		return -1;
	}
	data->updateProgress(total, done);
	return 0;
}

// downloads url into res
bool CHttpDownloader::DownloadUrl(const std::string& url, std::string& res)
{
	DownloadData d;
	d.download = new IDownload();
	d.download->addMirror(url);
	d.download->name = url;
	d.download->origin_name = url;

	CurlWrapper curlw;
	curl_easy_setopt(curlw.GetHandle(), CURLOPT_URL, CurlWrapper::escapeUrl(url).c_str());
	curl_easy_setopt(curlw.GetHandle(), CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
	curl_easy_setopt(curlw.GetHandle(), CURLOPT_WRITEDATA, (void*)&res);
	curl_easy_setopt(curlw.GetHandle(), CURLOPT_PROGRESSDATA, &d);
	curl_easy_setopt(curlw.GetHandle(), CURLOPT_PROGRESSFUNCTION, progress_func);
	curl_easy_setopt(curlw.GetHandle(), CURLOPT_NOPROGRESS, 0L);

	curl_easy_setopt(curlw.GetHandle(), CURLOPT_VERBOSE, 1L);

	const CURLcode curlres = curl_easy_perform(curlw.GetHandle());

	delete d.download;
	d.download = nullptr;
	if (curlres != CURLE_OK) {
		LOG_ERROR("Error in curl %s (%s)", curl_easy_strerror(curlres), curlw.GetError().c_str());
	}
	return curlres == CURLE_OK;
}

static std::string getRequestUrl(const std::string& name,
				 DownloadEnum::Category cat)
{
	std::string url = HTTP_SEARCH_URL + std::string("?");
	if (cat != DownloadEnum::CAT_NONE) {
		url += "category=" + DownloadEnum::getCat(cat) + std::string("&");
	}
	return url + std::string("springname=") + name;
}

bool CHttpDownloader::ParseResult(const std::string& /*name*/,
				  const std::string& json,
				  std::list<IDownload*>& res)
{
	Json::Value result; // will contains the root value after parsing.
	Json::Reader reader;
	const bool parsingSuccessful = reader.parse(json, result);
	if (!parsingSuccessful) {
		LOG_ERROR("Couldn't parse result: %s %s",
			  reader.getFormattedErrorMessages().c_str(), json.c_str());
		return false;
	}

	if (!result.isArray()) {
		LOG_ERROR("Returned json isn't an array!");
		return false;
	}

	for (Json::Value::ArrayIndex i = 0; i < result.size(); i++) {
		const Json::Value resfile = result[i];

		if (!resfile.isObject()) {
			LOG_ERROR("Entry isn't object!");
			return false;
		}
		if (!resfile["category"].isString()) {
			LOG_ERROR("No category in result");
			return false;
		}
		if (!resfile["springname"].isString()) {
			LOG_ERROR("No springname in result");
			return false;
		}
		std::string filename = fileSystem->getSpringDir();
		const std::string category = resfile["category"].asString();
		const std::string springname = resfile["springname"].asString();
		filename += PATH_DELIMITER;

		if (category == "map") {
			filename += "maps";
		} else if (category == "game") {
			filename += "games";
		} else if (category.find("engine") ==
			   0) { // engine_windows, engine_linux, engine_macosx
			filename += "engine";
		} else
			LOG_ERROR("Unknown Category %s", category.c_str());
		filename += PATH_DELIMITER;

		if ((!resfile["mirrors"].isArray()) || (!resfile["filename"].isString())) {
			LOG_ERROR("Invalid type in result");
			return false;
		}
		filename.append(
		    CFileSystem::EscapeFilename(resfile["filename"].asString()));

		const DownloadEnum::Category cat = DownloadEnum::getCatFromStr(category);
		IDownload* dl = new IDownload(filename, springname, cat);
		const Json::Value mirrors = resfile["mirrors"];
		for (Json::Value::ArrayIndex j = 0; j < mirrors.size(); j++) {
			if (!mirrors[j].isString()) {
				LOG_ERROR("Invalid type in result");
			} else {
				dl->addMirror(mirrors[j].asString());
			}
		}

		if (resfile["version"].isString()) {
			const std::string& version = resfile["version"].asString();
			dl->version = version;
		}
		if (resfile["md5"].isString()) {
			dl->hash = std::make_unique<HashMD5>();
			dl->hash->Set(resfile["md5"].asString());
			dl->out_hash = std::make_unique<HashMD5>();
		}
		if (resfile["size"].isInt()) {
			dl->size = resfile["size"].asInt();
		}
		if (resfile["depends"].isArray()) {
			for (Json::Value::ArrayIndex i = 0; i < resfile["depends"].size(); i++) {
				if (resfile["depends"][i].isString()) {
					const std::string& dep = resfile["depends"][i].asString();
					dl->addDepend(dep);
				}
			}
		}
		res.push_back(dl);
	}
	LOG_DEBUG("Parsed %d results", res.size());
	return true;
}

bool CHttpDownloader::search(std::list<IDownload*>& res,
			     const std::string& name,
			     DownloadEnum::Category cat)
{
	LOG_DEBUG("%s", name.c_str());
	std::string dlres;
	const std::string url = getRequestUrl(name, cat);
	if (!DownloadUrl(url, dlres)) {
		LOG_ERROR("Error downloading %s %s", url.c_str(), dlres.c_str());
		return false;
	}
	return ParseResult(name, dlres, res);
}

static size_t multi_write_data(void* ptr, size_t size, size_t nmemb,
			       DownloadData* data)
{
	if (IDownloader::AbortDownloads())
		return -1;
	data->download->state = IDownload::STATE_DOWNLOADING;
	if (data->download->out_hash != nullptr) {
		data->download->out_hash->Update((const char*)ptr, size * nmemb);
	}
	return data->download->file->Write((const char*)ptr, size * nmemb);
}

bool CHttpDownloader::setupDownload(DownloadData* piece)
{
	if (piece->download->isFinished())
		return false;

	piece->mirror = piece->download->getFastestMirror();
	if (piece->mirror == nullptr) {
		LOG_ERROR("No mirror found for %s", piece->download->name.c_str());
		return false;
	}

	if (piece->download->file != nullptr) {
		bool discard = piece->download->file->IsNewFile()
		    || piece->download->state != IDownload::STATE_NONE;
		piece->download->file->Close(discard);
	}
	piece->download->file = std::make_unique<CFile>();
	if (!piece->download->file->Open(piece->download->name)) {
		piece->download->file.reset();
		return false;
	}

	if (piece->download->out_hash != nullptr) {
		piece->download->out_hash->Init();
	}

	piece->curlw = std::make_unique<CurlWrapper>();
	CURL* curle = piece->curlw->GetHandle();

	curl_easy_setopt(curle, CURLOPT_PRIVATE, piece);
	curl_easy_setopt(curle, CURLOPT_WRITEFUNCTION, multi_write_data);
	curl_easy_setopt(curle, CURLOPT_WRITEDATA, piece);
	curl_easy_setopt(curle, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curle, CURLOPT_PROGRESSDATA, piece);
	curl_easy_setopt(curle, CURLOPT_PROGRESSFUNCTION, progress_func);
	curl_easy_setopt(curle, CURLOPT_URL, CurlWrapper::escapeUrl(piece->mirror->url).c_str());

	curl_easy_setopt(curle, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NO_REVOKE);

	if (!piece->download->validateTLS) {
		LOG_DEBUG("Not Validating TLS");
		curl_easy_setopt(curle, CURLOPT_SSL_VERIFYPEER, 0);
	}
	// this sets the header If-Modified-Since -> downloads only when remote file
	// is newer than local file
	const long timestamp = piece->download->file->GetTimestamp();
	if ((timestamp >= 0) &&
	    (piece->download->hash ==
	     nullptr)) { // timestamp known + hash not known -> only dl when changed
		curl_easy_setopt(curle, CURLOPT_TIMECONDITION, CURL_TIMECOND_IFMODSINCE);
		curl_easy_setopt(curle, CURLOPT_TIMEVALUE, timestamp);
		curl_easy_setopt(curle, CURLOPT_FILETIME, 1);
	}
	return true;
}

bool CHttpDownloader::processMessages(CURLM* curlm,
                                      std::vector<DownloadData*>& downloads,
                                      std::vector<DownloadData*>::iterator& next_download)
{
	int msgs_left;
	bool ok = true;
	while (struct CURLMsg* msg = curl_multi_info_read(curlm, &msgs_left)) {
		switch (msg->msg) {
			case CURLMSG_DONE: { // a piece has been downloaded, verify it
				DownloadData* data;
				curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &data);
				long http_code = 0;
				switch (msg->data.result) {
					case CURLE_OK:
						curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &http_code);
						if (http_code == 304 || data->download->out_hash == nullptr) {
							data->download->state = IDownload::STATE_FINISHED;
						} else {
							// When we downloaded file that has hash we still keep it
							// in the downloading state, because we don't know if the
							// hashes match.
							data->download->out_hash->Final();
						}
						break;
					case CURLE_HTTP_RETURNED_ERROR: // some 4* HTTP-Error (file not found,
									// access denied,...)
					default:
						long http_code = 0;
						curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &http_code);
						LOG_ERROR("CURL error(%d:%d): %s %d (%s)", msg->msg, msg->data.result,
							  curl_easy_strerror(msg->data.result), http_code,
							  data->mirror->url.c_str());
						data->mirror->status = Mirror::STATUS_BROKEN;
						// TODO(p2004a): Implement retrying of server errors with exponential backoff.
						ok = false;
				}
				assert(data->download->file != nullptr);

				// get speed at which this piece was downloaded + update mirror info
				double dlSpeed;
				curl_easy_getinfo(data->curlw->GetHandle(), CURLINFO_SPEED_DOWNLOAD,
						  &dlSpeed);
				data->mirror->UpdateSpeed(dlSpeed);
				if (data->mirror->status ==
				    Mirror::STATUS_UNKNOWN) // set mirror status only when unset
					data->mirror->status = Mirror::STATUS_OK;

				// remove easy handle, as its finished
				curl_multi_remove_handle(curlm, data->curlw->GetHandle());
				data->curlw = nullptr;

				if (ok && next_download != downloads.end()) {
					if (setupDownload(*next_download)) {
						curl_multi_add_handle(curlm, (*next_download)->curlw->GetHandle());
						++next_download;
					} else {
						ok = false;
					}
				}
				break;
			}
			default:
				LOG_ERROR("Unhandled message %d", msg->msg);
		}
	}
	return ok;
}

static void CleanupDownloads(CURLM* curlm,
                             std::list<IDownload*>& download,
                             std::vector<DownloadData*>& downloads)
{
	// close all open files
	for (IDownload* dl : download) {
		if (dl->file == nullptr) continue;
		bool discard = true;
		switch (dl->state) {
			case IDownload::STATE_NONE:
				// We haven't writen any data, so drop only if it's a entirely new file.
				discard = dl->file->IsNewFile();
				break;
			case IDownload::STATE_DOWNLOADING:
				// Some other error interrupted overall transfer (this or other file).
				// We drop file because it's not in a consistent state after partial write.
				dl->state = IDownload::STATE_FAILED;
			case IDownload::STATE_FAILED:
				// Something went wrong with download, we drop the file.
				discard = true;
				break;
			case IDownload::STATE_FINISHED:
				discard = false;
		}
		dl->file->Close(discard);
		dl->file = nullptr;
	}

	for (DownloadData* data: downloads) {
		if (data->curlw != nullptr) {
			curl_multi_remove_handle(curlm, data->curlw->GetHandle());
			data->curlw = nullptr;
		}
		delete data;
	}

	downloads.clear();
}

void VerifySinglePieceDownload(IDownload* dl)
{
	if (dl->state != IDownload::STATE_DOWNLOADING || dl->out_hash == nullptr) return;

	if (!dl->out_hash->isSet() || !dl->hash->compare(dl->out_hash.get())) {
		dl->state = IDownload::STATE_FAILED;
	} else {
		dl->state = IDownload::STATE_FINISHED;
	}
}

bool CHttpDownloader::download(std::list<IDownload*>& download,
                               int max_parallel)
{
	std::vector<DownloadData*> downloads;
	DownloadDataPack download_pack;
	CURLM* curlm = curl_multi_init();
	for (IDownload* dl : download) {
		if (dl->isFinished()) {
			continue;
		}
		if (dl->dltype != IDownload::TYP_HTTP) {
			LOG_DEBUG("skipping non http-dl")
			continue;
		}
		if (dl->getMirrorCount() <= 0) {
			LOG_WARN("No mirrors found");
			return false;
		}
		DownloadData* dlData = new DownloadData();
		dlData->download = dl;
		dlData->data_pack = &download_pack;
		if (dl->size > 0) {
			download_pack.size += dl->size;
		} else {
			download_pack.size += dl->approx_size;
		}
		downloads.push_back(dlData);
	}
	if (downloads.empty()) {
		LOG_DEBUG("Nothing to download!");
		CleanupDownloads(curlm, download, downloads);
		return true;
	}

	bool aborted = false;

	std::vector<DownloadData*>::iterator next_download = downloads.begin();
	for (int i = 0; i < max_parallel && next_download != downloads.end(); ++i) {
		if (!setupDownload(*next_download)) {
			aborted = true;
			break;
		}
		curl_multi_add_handle(curlm, (*next_download)->curlw->GetHandle());
		++next_download;
	}

	int running = 1;
	while (running > 0 && !aborted) {
		CURLMcode ret = CURLM_CALL_MULTI_PERFORM;
		while (ret == CURLM_CALL_MULTI_PERFORM) {
			ret = curl_multi_perform(curlm, &running);
		}
		if (ret == CURLM_OK) {
			if (!processMessages(curlm, downloads, next_download)) {
				aborted = true;
				break;
			}
			ret = curl_multi_poll(curlm, NULL, 0, 100, NULL);
		}
		if (ret != CURLM_OK) {
			LOG_ERROR("curl_multi failed, code %d.\n", ret);
			aborted = true;
		 	break;
		}
	}

	int all_verified = true;
	for (IDownload* download: download) {
		VerifySinglePieceDownload(download);
		all_verified = all_verified && download->isFinished();
	}

	LOG("\n");

	if (!aborted) {
		LOG_DEBUG("download complete");
	}
	CleanupDownloads(curlm, download, downloads);
	curl_multi_cleanup(curlm);
	return !aborted && all_verified;
}
