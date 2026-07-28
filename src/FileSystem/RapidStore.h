/* This file is part of pr-downloader (GPL v2 or later), see the LICENSE file */

#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct RapidTag {
	std::string domain;
	std::string repo;
	std::string tag;
	std::string name;
	std::size_t rank;
};

struct InstalledPackage {
	std::string md5;
	std::vector<RapidTag> tags;

	/**
	 * empty when no local versions.gz mentions this package
	 */
	const std::string& getName() const;
};

/**
 * Read/write access to the installed rapid packages under the write path, without any network
 * access. Names are resolved against what is on disk rather than against a repo index, so a package
 * stays removable after its tag stops being published.
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
	 * Matches an md5, a rapid tag or an archive name against the installed packages, following the
	 * precedence the engine uses in ArchiveNameResolver. Fills matches with the single match on OK
	 * and with every candidate on AMBIGUOUS.
	 */
	Resolution resolve(const std::string& query,
	                   std::vector<const InstalledPackage*>& matches) const;

	/**
	 * Removes the sdp files and then every pool file left unreferenced by the packages that remain.
	 */
	bool remove(const std::vector<const InstalledPackage*>& to_remove);

private:
	std::vector<InstalledPackage> packages;
	std::vector<std::string> domain_order;

	std::size_t rankDomain(const std::string& domain) const;
	bool scanPackagesDir();
	bool scanVersionsCache();
};
