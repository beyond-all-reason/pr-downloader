/* This file is part of pr-downloader (GPL v2 or later), see the LICENSE file */

#pragma once

#include <memory>
#include <zlib.h>

#include "IHash.h"

// Passes all the data to the subhash but decompresses it first.
class HashGzip : public IHash
{
public:
	explicit HashGzip(std::unique_ptr<IHash> hash);
	~HashGzip() override;

	void Init() override;
	void Final() override;
	void Update(const char* data, const int size) override;
	bool Set(const unsigned char* data, int size) override;
	unsigned char get(int pos) const override;
	int getSize() const override;

private:
	std::unique_ptr<IHash> subhash;
	z_stream strm;
	bool error = false;
	bool stream_done = false;
};
