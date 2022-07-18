#define BOOST_TEST_MODULE Float3
#include <boost/test/unit_test.hpp>
#include <string>
#include <memory>

#include "FileSystem/FileSystem.h"
#include "FileSystem/HashGzip.h"
#include "FileSystem/HashMD5.h"

BOOST_AUTO_TEST_CASE(EscapeFilenameTest)
{
	BOOST_CHECK("_____" == CFileSystem::EscapeFilename("/<|>/"));
	BOOST_CHECK("_____" == CFileSystem::EscapeFilename("/<|>\\"));
	BOOST_CHECK("abC123" == CFileSystem::EscapeFilename("abC123"));
}

BOOST_AUTO_TEST_CASE(HashGzipTest)
{
	// Uncompressed output is:
	//   b'a'*100000 + b'cnoi1au4enfygw7oem9xbm3xoMBV9wmXOwuodawdca'
	constexpr int input_size = 178 + 10;
	unsigned char input[input_size] = {
		31,139,8,0,176,70,139,98,2,255,237,193,61,14,69,64,24,0,192,51,9,137,
		104,245,162,19,237,231,231,189,40,214,86,178,220,222,53,20,51,19,1,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,124,213,122,230,163,
		138,171,217,207,223,243,47,109,222,83,119,47,169,190,243,208,79,93,73,
		243,88,174,188,69,217,214,120,1,52,234,240,146,202,134,1,0,
		// We add and allow some additional data past stream end as it's
		// supposed to be ignored by zlib.
		23,12,43,12,41,0,0,4,2,4
	};
	std::string input_hash = "9a3b049769e61efaced3b73adb6fc43e";

	HashMD5 md5_hash;
	BOOST_CHECK(md5_hash.IHash::Set(input_hash));

	HashGzip gzip_hash(std::make_unique<HashMD5>());
	gzip_hash.Init();
	for (int i = 0; i < input_size; ++i) {
		gzip_hash.Update(static_cast<char*>(&input[i]), 1);
	}
	gzip_hash.Final();
	BOOST_CHECK(gzip_hash.compare(&md5_hash));

	// When we corrupt input, we expect FFFF... hash.
	input[100] = 20;
	input[101] = 13;
	gzip_hash.Init();
	gzip_hash.Update(static_cast<char*>(input), input_size);
	gzip_hash.Final();
	BOOST_CHECK(gzip_hash.get(0) == 255);
}
