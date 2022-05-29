/* This file is part of pr-downloader (GPL v2 or later), see the LICENSE file */

#ifndef DOWNLOADER_H
#define DOWNLOADER_H

#include <memory>
#include <string>
#include <vector>
#include <list>
#include <map>
#include <stdint.h>
#include "Rapid/Sdp.h"
#include "DownloadEnum.h"
#include "FileSystem/IHash.h"
#include "FileSystem/File.h"

class DownloadData;

class IDownload
{
public:
	DownloadEnum::Category cat;

	enum download_type { TYP_RAPID,
			     TYP_HTTP } dltype;

	IDownload(const std::string& filename = "", const std::string& orig_name = "",
		  DownloadEnum::Category cat = DownloadEnum::CAT_NONE,
		  download_type typ = TYP_HTTP);
	/**
   *
   *	add a mirror to the download specified
   */
	bool addMirror(const std::string& url);
	bool addDepend(const std::string& depend);
	bool isFinished() const { return state == STATE_FINISHED; }
	std::string name;	// name, in most cases the filename to save to
	std::string origin_name; // name of object. Not the filename

	std::list<std::string> depend; // list of all depends
				       /**
                                  *	returns first url
                                  */
	std::string getMirror(unsigned i = 0) const;
	int getMirrorCount() const;
	/**
  *	size of pieces, last piece size can be different
  */
	enum PIECE_STATE {
		STATE_NONE,	// nothing was done with this piece
		STATE_DOWNLOADING, // piece is currently downloaded, something
		                   // was writen to the file
		STATE_FAILED,    // piece failed to download or verify
		STATE_FINISHED,    // piece downloaded successfully and verified
	};
	// What the hash the download is supposed to have.
	std::unique_ptr<IHash> hash = nullptr;
	// To store the actual hash of the download. Need to be set by the
	// caller because we don't know the concrete type.
	std::unique_ptr<IHash> out_hash = nullptr;
	std::unique_ptr<CFile> file = nullptr;

	/**
   *	file size
   */
	int size = -1;

	/**
	 * Approximate file size, for cases where real size isn't entirely
	 * known, e.g. when downloading compressed files from spd.
	 * We default it to 1, as a simple file counter.
	 */
	int approx_size = 1;

	std::map<CSdp*, uint64_t> rapid_size;
	std::map<CSdp*, uint64_t> map_rapid_progress;

	/**
   *	state for whole file
   */
	PIECE_STATE state = IDownload::STATE_NONE;
	/**
   *	returns number of bytes downloaded
   */
	unsigned int getProgress() const;
	void updateProgress(unsigned int progress);
	std::string version;

	bool validateTLS = true;
private:
	int progress = 0;
	std::vector<std::string> mirrors;
	static void initCategories();
};

#endif
