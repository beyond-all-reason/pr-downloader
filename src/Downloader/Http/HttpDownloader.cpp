/* This file is part of pr-downloader (GPL v2 or later), see the LICENSE file */

#include "HttpDownloader.h"
#include "Downloader/Download.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <sys/types.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

#include <json/reader.h>

#include "DownloadData.h"
#include "Throttler.h"
#include "FileSystem/FileSystem.h"
#include "FileSystem/File.h"
#include "FileSystem/HashMD5.h"
#include "FileSystem/HashSHA1.h"
#include "Util.h"
#include "Logger.h"
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

// Computes the exponential retry duration to wait before making next request.
template <class D1, class D2, class DR = typename std::common_type<D1,D2>::type>
static DR retryAfter(int retry_num, D1 base_delay, D2 max_delay, double factor = 2.0)
{
	static std::default_random_engine gen(std::random_device{}());
	static std::uniform_real_distribution<> dis(0.7, 1.2);
	if (retry_num <= 0) {
		return DR(0);
	}
	auto backoff = base_delay * std::pow(factor, retry_num - 1) * dis(gen);
	return std::min(std::chrono::duration_cast<DR>(backoff),
	                std::chrono::duration_cast<DR>(max_delay));
}

static bool setupDownload(CURLM* curlm, DownloadData* piece)
{
	static std::default_random_engine gen(std::random_device{}());

	if (piece->download->isFinished())
		return false;

	if (piece->download->getMirrorCount() < 1) {
		LOG_ERROR("No mirror found for %s", piece->download->name.c_str());
		return false;
	}
	std::uniform_int_distribution<> dist(0, piece->download->getMirrorCount() - 1);
	piece->mirror = piece->download->getMirror(dist(gen));

	assert(piece->download->file == nullptr);
	piece->download->file = std::make_unique<CFile>();
	if (!piece->download->file->Open(piece->download->name)) {
		piece->download->file = nullptr;
		return false;
	}

	if (piece->download->out_hash != nullptr) {
		piece->download->out_hash->Init();
	}

	piece->curlw = std::make_unique<CurlWrapper>();
	CURL* curle = piece->curlw->GetHandle();

	curl_easy_setopt(curle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2);
	curl_easy_setopt(curle, CURLOPT_PRIVATE, piece);
	curl_easy_setopt(curle, CURLOPT_WRITEFUNCTION, multi_write_data);
	curl_easy_setopt(curle, CURLOPT_WRITEDATA, piece);
	curl_easy_setopt(curle, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curle, CURLOPT_PROGRESSDATA, piece);
	curl_easy_setopt(curle, CURLOPT_PROGRESSFUNCTION, progress_func);
	curl_easy_setopt(curle, CURLOPT_URL, CurlWrapper::escapeUrl(piece->mirror).c_str());
	curl_easy_setopt(curle, CURLOPT_PIPEWAIT, 1L);
	curl_easy_setopt(curle, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NO_REVOKE);

	if (piece->download->noCache) {
		piece->curlw->AddHeader("Cache-Control: no-cache");
	}
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

	curl_multi_add_handle(curlm, curle);
	return true;
}

static void cleanupDownload(CURLM* curlm, DownloadData* data)
{
	auto dl = data->download;

	if (dl->file != nullptr) {
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
	if (data->curlw != nullptr) {
		curl_multi_remove_handle(curlm, data->curlw->GetHandle());
		data->curlw = nullptr;
	}
}

bool CHttpDownloader::processMessages(CURLM* curlm,
                                      std::vector<DownloadData*>* to_retry)
{
	int msgs_left;
	bool ok = true;
	while (struct CURLMsg* msg = curl_multi_info_read(curlm, &msgs_left)) {
		if (msg->msg != CURLMSG_DONE) {
			// Currently CURLMSG_DONE is the only message defined
			LOG_ERROR("Unhandled message %d", msg->msg);
			continue;
		}
		DownloadData* data;
		curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &data);
		long http_code = 0;
		bool retry = false;
		switch (msg->data.result) {
			case CURLE_OK:
				curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &http_code);
				if (http_code == 304 || data->download->out_hash == nullptr) {
					data->download->state = IDownload::STATE_FINISHED;
				} else {
					// Verify that hash matches with expected when we are done.
					data->download->out_hash->Final();
					if (!data->download->hash->compare(data->download->out_hash.get())) {
						data->download->state = IDownload::STATE_FAILED;
						ok = false;
					} else {
						data->download->state = IDownload::STATE_FINISHED;
					}
				}
				break;
			// We consider this list of errors worth retrying.
			case CURLE_COULDNT_CONNECT:
			case CURLE_WEIRD_SERVER_REPLY:
			case CURLE_HTTP2:
			case CURLE_PARTIAL_FILE:
			case CURLE_OPERATION_TIMEDOUT:
			case CURLE_SSL_CONNECT_ERROR:
			case CURLE_GOT_NOTHING:
			case CURLE_SEND_ERROR:
			case CURLE_RECV_ERROR:
			case CURLE_HTTP2_STREAM:
			case CURLE_HTTP3:
			case CURLE_QUIC_CONNECT_ERROR:
			case CURLE_HTTP_RETURNED_ERROR:
				retry = true;
			default:
				curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &http_code);
				// Let's not retry on client errors except for the 429 Too Many Requests
				if (http_code >= 400 && http_code < 500 && http_code != 429) {
					retry = false;
				}
				if (retry) {
					curl_off_t retry_after_wait_s = 0;
					curl_easy_getinfo(msg->easy_handle, CURLINFO_RETRY_AFTER, &retry_after_wait_s);
					data->retry_after_from_server = std::chrono::seconds(retry_after_wait_s);

					to_retry->push_back(data);
				}

				LOG_ERROR("CURL error(%d:%d): %s %d (%s)%s", msg->msg, msg->data.result,
					  curl_easy_strerror(msg->data.result), http_code,
					  data->mirror.c_str(), retry ? ", will retry" : ", aborting");

				ok = ok && retry;
		}
		cleanupDownload(curlm, data);
	}
	return ok;
}

bool computeRetry(DownloadData* data) {
	auto now = std::chrono::steady_clock::now();
	using namespace std::chrono_literals;
	constexpr int retry_num_limit = 10;
	++data->retry_num;
	if (data->retry_num > retry_num_limit) {
		LOG_ERROR("Limit of retried (%d) reached for %s, aborting",
		          retry_num_limit, data->download->name.c_str());
		return false;
	}
	if (data->retry_after_from_server > 30s) {
		LOG_ERROR("Server asked us to retry after %ds which is longer then max of 30s, aborting",
		          data->retry_after_from_server.count());
		return false;
	}
	auto backoff = retryAfter(data->retry_num, 100ms, 5s);
	if (data->retry_after_from_server > 0s) {
		backoff = data->retry_after_from_server;
	}
	data->next_retry = now + backoff;
	return true;
}

static unsigned getMaxReqsPerSecLimit() {
	unsigned long max_req_per_sec = 0; // unlimited
	const char* max_req_per_sec_env = std::getenv("PRD_MAX_HTTP_REQS_PER_SEC");
	if (max_req_per_sec_env != nullptr) {
		char* end;
		max_req_per_sec = std::strtoul(max_req_per_sec_env, &end, 10);
		if (max_req_per_sec == ULONG_MAX || *end != '\0') {
			LOG_ERROR("PRD_MAX_HTTP_REQS_PER_SEC env variable value is not valid.");
			return false;
		}
	}
	return max_req_per_sec;
}

bool CHttpDownloader::download(std::list<IDownload*>& download,
                               int max_parallel)
{
	// Prepare downloads from input.
	std::vector<std::unique_ptr<DownloadData>> downloads;
	DownloadDataPack download_pack;
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
		downloads.emplace_back(std::make_unique<DownloadData>());
		auto dlData = downloads.back().get();
		dlData->download = dl;
		dlData->data_pack = &download_pack;
		if (dl->size > 0) {
			dlData->approx_size = dl->size;
			download_pack.size += dl->size;
		} else {
			dlData->approx_size = dl->approx_size;
			download_pack.size += dl->approx_size;
		}
	}
	if (downloads.empty()) {
		LOG_DEBUG("Nothing to download!");
		return true;
	}

	// We sort the downloads by size and then pull files from both ends of the
	// queue. The goal is to have a one big file in progress and and multiple
	// smaller ones. We do this to try to maximize bandwidth usage and minimize
	// impact from latency and rate limiting. In other words, try to as much as
	// possible shift bottleneck on download speed from req/s to client
	// bandwidth.
	std::sort(downloads.begin(), downloads.end(),
	          [](const std::unique_ptr<DownloadData>& a,
	             const std::unique_ptr<DownloadData>& b) {
		return a->approx_size < b->approx_size;
	});
	auto small_file_it = downloads.begin();
	auto big_file_it = downloads.end();

	// Perform actual download using the Curl multi interface.
	CURLM* curlm = curl_multi_init();
	curl_multi_setopt(curlm, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
	curl_multi_setopt(curlm, CURLMOPT_MAX_TOTAL_CONNECTIONS, 5);

	std::vector<DownloadData*> to_retry;
	auto queue_comparator = [](DownloadData* a, DownloadData* b) {
		return a->next_retry < b->next_retry;
	};
	std::priority_queue<DownloadData*, std::vector<DownloadData*>,
	                    decltype(queue_comparator)> wait_queue(queue_comparator);
	unsigned max_req_per_sec = getMaxReqsPerSecLimit();
	Throttler throttler(max_req_per_sec,
	                    std::min((unsigned)max_parallel,
	                             std::max(max_req_per_sec / 10, 5U)));

	int running = 0;   // Number of currently running downloads + waiting for retry
	bool aborted = true;   // We use goto with aborted because we have nested loops.
	do {
		CURLMcode ret = curl_multi_perform(curlm, &running);
		if (ret != CURLM_OK) {
			LOG_ERROR("curl_multi_perform failed, code %d.\n", ret);
			goto abort;
		}
		to_retry.clear();
		if (!processMessages(curlm, &to_retry)) {
			goto abort;
		}

		// Add all requests that should be retried to the wait_queue with delay
		// or fail if we did too many retries already.
		for (DownloadData* data: to_retry) {
			if (!computeRetry(data)) {
				goto abort;
			}
			wait_queue.push(data);
		}

		throttler.refill_bucket();

		// If enough time passed for element in the wait_queue, retry the request.
		running += wait_queue.size();
		auto now = std::chrono::steady_clock::now();
		while (!wait_queue.empty() && wait_queue.top()->next_retry <= now &&
		       throttler.get_token()) {
			if (!setupDownload(curlm, wait_queue.top())) {
				goto abort;
			}
			wait_queue.pop();
		}

		// Start more new requests so we have up to max_parallel happening.
		for (; running < max_parallel && small_file_it < big_file_it &&
		       throttler.get_token(); ++running) {
			decltype(downloads)::iterator to_download;
			if (big_file_it == downloads.end() ||
			    (*big_file_it)->download->state == IDownload::STATE_FINISHED) {
				to_download = --big_file_it;
			} else {
				to_download = small_file_it++;
			}
			if (!setupDownload(curlm, (*to_download).get())) {
				goto abort;
			}
		}

		ret = curl_multi_poll(curlm, NULL, 0, 20, NULL);
		if (ret != CURLM_OK) {
			LOG_ERROR("curl_multi_poll, code %d.\n", ret);
			goto abort;
		}
	} while (running > 0 || small_file_it < big_file_it);
	aborted = false;
	LOG_DEBUG("download complete");
abort:

	// Cleanup
	for (auto& data: downloads) {
		cleanupDownload(curlm, data.get());
	}
	curl_multi_cleanup(curlm);
	return !aborted;
}
