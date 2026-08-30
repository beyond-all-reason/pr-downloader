/* This file is part of pr-downloader (GPL v2 or later), see the LICENSE file */

#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct RapidTag {
	std::string tag;
	std::string name;
	std::size_t rank;
};

struct InstalledPackage {
	std::string md5;
	std::string name;
	std::vector<RapidTag> tags;
};

/**
 * The installed rapid packages under the write path. Names resolve against what is on disk instead
 * of against a repo index, so removing a package needs no network and keeps working after its tag
 * stops being published.
 */
class CRapidStore
{
public:
	enum class Resolution {
		OK,
		NOT_FOUND,
		AMBIGUOUS,
	};

	bool scan();

	/**
	 * Matches an md5, a rapid tag or an archive name, following the precedence the engine uses in
	 * ArchiveNameResolver. Fills matches with the one match on OK and with every candidate on
	 * AMBIGUOUS.
	 */
	Resolution resolve(const std::string& query,
	                   std::vector<const InstalledPackage*>& matches) const;

	/**
	 * Removes the sdp files, then the pool files left unreferenced by the packages that remain.
	 */
	bool remove(const std::vector<const InstalledPackage*>& to_remove);

private:
	std::vector<InstalledPackage> packages;
	std::vector<std::string> domain_order;

	std::size_t rankDomain(const std::string& domain) const;
};
