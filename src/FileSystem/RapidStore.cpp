/* This file is part of pr-downloader (GPL v2 or later), see the LICENSE file */

#include "RapidStore.h"
#include "FileData.h"
#include "FileSystem.h"
#include "HashMD5.h"
#include "Logger.h"
#include "Tracer.h"
#include "Util.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

namespace
{

const std::string SPRINGRTS_DOMAIN = "repos.springrts.com";
const std::string SDP_EXT = ".sdp";
const std::string INCOMPLETE_EXT = ".sdp.incomplete";

std::string packagesDir()
{
	return fileSystem->getSpringDir() + PATH_DELIMITER + "packages";
}

std::string toLower(std::string str)
{
	std::transform(str.begin(), str.end(), str.begin(),
	               [](unsigned char c) { return std::tolower(c); });
	return str;
}

bool endsWith(const std::string& str, const std::string& suffix)
{
	return str.size() >= suffix.size() &&
	       str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool isPackageMD5(const std::string& str)
{
	return str.size() == 32 && std::all_of(str.begin(), str.end(),
	                                       [](unsigned char c) { return std::isxdigit(c) != 0; });
}

bool collectPoolFiles(const std::string& sdp_path, std::unordered_set<std::string>& out)
{
	std::vector<FileData> files;
	if (!fileSystem->parseSdp(sdp_path, files)) {
		return false;
	}
	HashMD5 md5;
	for (const FileData& fd : files) {
		md5.Set(fd.md5, sizeof(fd.md5));
		out.insert(md5.toString());
	}
	return true;
}

}  // namespace

const std::string& InstalledPackage::getName() const
{
	static const std::string empty;
	return tags.empty() ? empty : tags.front().name;
}

std::size_t CRapidStore::rankDomain(const std::string& domain) const
{
	const auto it = std::find(domain_order.begin(), domain_order.end(), domain);

	return it == domain_order.end() ? domain_order.size() : it - domain_order.begin();
}

bool CRapidStore::scanPackagesDir()
try {
	const auto dir = u8ToPath(packagesDir());
	if (!std::filesystem::exists(dir)) {
		return true;
	}
	for (const auto& entry : std::filesystem::directory_iterator(dir)) {
		if (!entry.is_regular_file() ||
		    entry.path().extension() != std::filesystem::path(SDP_EXT)) {
			continue;
		}
		const std::string md5 = toLower(pathToU8(entry.path().stem()));
		if (!isPackageMD5(md5)) {
			LOG_WARN("Ignoring unexpected file in packages dir: %s",
			         pathToU8(entry.path().filename()).c_str());
			continue;
		}
		packages.push_back({md5, {}});
	}

	return true;
} catch (const std::filesystem::filesystem_error& ex) {
	LOG_ERROR("Failed to read installed packages: %s", ex.what());
	return false;
}

bool CRapidStore::scanVersionsCache()
try {
	const auto dir = u8ToPath(fileSystem->getSpringDir() + PATH_DELIMITER + "rapid");
	if (!std::filesystem::exists(dir)) {
		return true;
	}

	std::unordered_map<std::string, InstalledPackage*> by_md5;
	for (InstalledPackage& pkg : packages) {
		by_md5[pkg.md5] = &pkg;
	}

	for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
		if (!entry.is_regular_file() ||
		    entry.path().filename() != std::filesystem::path("versions.gz")) {
			continue;
		}
		// pr-downloader lays these out as rapid/<domain>/<repo>/versions.gz, which is also what
		// the engine assumes when it ranks tag resolution by domain.
		const std::string repo = pathToU8(entry.path().parent_path().filename());
		const std::string domain = pathToU8(entry.path().parent_path().parent_path().filename());
		const std::size_t rank = rankDomain(domain);

		CFileSystem::readGzLines(pathToU8(entry.path()), [&](const std::string& line) {
			const std::vector<std::string> items = tokenizeString(line, ',');
			if (items.size() < 4) {
				return true;
			}
			const auto it = by_md5.find(toLower(items[1]));
			if (it != by_md5.end()) {
				it->second->tags.push_back({domain, repo, items[0], items[3], rank});
			}
			return true;
		});
	}

	for (InstalledPackage& pkg : packages) {
		std::stable_sort(pkg.tags.begin(), pkg.tags.end(),
		                 [](const RapidTag& a, const RapidTag& b) { return a.rank < b.rank; });
	}

	return true;
} catch (const std::filesystem::filesystem_error& ex) {
	LOG_ERROR("Failed to read the rapid cache: %s", ex.what());
	return false;
}

bool CRapidStore::scan()
{
	TRACE();
	packages.clear();

	domain_order.clear();
	if (const auto order = getEnvVar("PRD_RAPID_TAG_RESOLUTION_ORDER"); order.has_value()) {
		for (const std::string& domain : tokenizeString(*order, ';')) {
			if (!domain.empty()) {
				domain_order.push_back(domain);
			}
		}
	}
	if (std::find(domain_order.begin(), domain_order.end(), SPRINGRTS_DOMAIN) ==
	    domain_order.end()) {
		domain_order.push_back(SPRINGRTS_DOMAIN);
	}

	return scanPackagesDir() && scanVersionsCache();
}

CRapidStore::Resolution CRapidStore::resolve(const std::string& query,
                                             std::vector<const InstalledPackage*>& matches) const
{
	matches.clear();
	const std::string wanted = stripRapidUri(query);

	if (isPackageMD5(wanted)) {
		const std::string md5 = toLower(wanted);
		for (const InstalledPackage& pkg : packages) {
			if (pkg.md5 == md5) {
				matches.push_back(&pkg);

				return Resolution::OK;
			}
		}

		return Resolution::NOT_FOUND;
	}

	std::size_t best_rank = domain_order.size() + 1;
	for (const InstalledPackage& pkg : packages) {
		for (const RapidTag& tag : pkg.tags) {
			if (tag.tag == wanted) {
				best_rank = std::min(best_rank, tag.rank);
			}
		}
	}
	for (const InstalledPackage& pkg : packages) {
		const bool matched =
			std::any_of(pkg.tags.begin(), pkg.tags.end(), [&](const RapidTag& tag) {
				return tag.tag == wanted && tag.rank == best_rank;
			});
		if (matched) {
			matches.push_back(&pkg);
		}
	}

	if (matches.empty()) {
		for (const InstalledPackage& pkg : packages) {
			const bool matched =
				std::any_of(pkg.tags.begin(), pkg.tags.end(),
			                [&](const RapidTag& tag) { return tag.name == wanted; });
			if (matched) {
				matches.push_back(&pkg);
			}
		}
	}

	if (matches.empty()) {
		return Resolution::NOT_FOUND;
	}

	return matches.size() == 1 ? Resolution::OK : Resolution::AMBIGUOUS;
}

bool CRapidStore::remove(const std::vector<const InstalledPackage*>& to_remove)
try {
	TRACE();
	const std::string packages_dir = packagesDir();

	std::unordered_set<std::string> orphan_candidates;
	bool ok = true;
	for (const InstalledPackage* pkg : to_remove) {
		const std::string sdp_path = packages_dir + PATH_DELIMITER + pkg->md5 + SDP_EXT;
		if (!collectPoolFiles(sdp_path, orphan_candidates)) {
			LOG_WARN("Could not read %s, its pool files are left in place", sdp_path.c_str());
		}
		// The sdp goes first so that an interrupted removal leaves an uninstalled package rather
		// than an installed one with files missing underneath it.
		if (!CFileSystem::removeFile(sdp_path)) {
			ok = false;
			continue;
		}
		LOG_INFO("Uninstalled %s%s%s", pkg->md5.c_str(), pkg->getName().empty() ? "" : " ",
		         pkg->getName().c_str());
	}

	if (orphan_candidates.empty()) {
		return ok;
	}

	std::unordered_set<std::string> still_needed;
	for (const auto& entry : std::filesystem::directory_iterator(u8ToPath(packages_dir))) {
		const std::string filename = pathToU8(entry.path().filename());
		if (!entry.is_regular_file() ||
		    (!endsWith(filename, SDP_EXT) && !endsWith(filename, INCOMPLETE_EXT))) {
			continue;
		}
		if (!collectPoolFiles(pathToU8(entry.path()), still_needed)) {
			LOG_ERROR("Could not read %s, skipping pool cleanup to avoid deleting files it needs",
			          filename.c_str());
			return false;
		}
	}

	std::unordered_set<std::string> touched_dirs;
	for (const std::string& md5 : orphan_candidates) {
		if (still_needed.count(md5) > 0) {
			continue;
		}
		const std::string pool_file = fileSystem->getPoolFilename(md5);
		if (!CFileSystem::fileExists(pool_file)) {
			continue;
		}
		if (CFileSystem::removeFile(pool_file)) {
			touched_dirs.insert(CFileSystem::DirName(pool_file));
		} else {
			ok = false;
		}
	}

	for (const std::string& dir : touched_dirs) {
		if (std::filesystem::is_empty(u8ToPath(dir))) {
			CFileSystem::removeDir(dir);
		}
	}

	return ok;
} catch (const std::filesystem::filesystem_error& ex) {
	LOG_ERROR("Failed to clean up the pool: %s", ex.what());
	return false;
}
