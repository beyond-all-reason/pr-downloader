#include "pr-downloader/Download.h"
#include <string>
#include <list>
#include <stdio.h>

IDownload::IDownload(const std::string& name, category cat)
{
	this->name=name;
	this->cat=cat;
	this->downloaded=false;
}

const std::string& IDownload::getCat(category cat)
{
	const char* cats[]= {"none","maps","mods","luawidgets","aibots","lobbyclients","media","other","replays","springinstallers","tools"};
	return cats[cat];
}

const std::string& IDownload::getUrl()
{
	const std::string empty="";
	if (!mirror.empty())
		return mirror.front();
	return empty;
}

const std::string& IDownload::getMirror(const int i)
{
	int pos=0;
	std::list<std::string>::iterator it;
	for(it=mirror.begin();it!=mirror.end(); it++){
		if(pos==i)
			return *it;
		pos++;
	}
	printf("invalid index in getMirror: %d\n", i);
	return mirror.front();
}

int IDownload::getMirrorCount()
{
	return mirror.size();
}
