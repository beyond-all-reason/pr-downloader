#include "soap/soapPlasmaServiceSoap12Proxy.h"
#include "soap/PlasmaServiceSoap.nsmap"
#include "PlasmaDownloader.h"
#include "FileSystem.h"



CPlasmaDownloader::CPlasmaDownloader(){
	this->torrentPath==fileSystem->getSpringDir()+"/torrent/";
	fileSystem->create_subdirs(this->torrentPath);
}

std::list<IDownload>* CPlasmaDownloader::search(const std::string& name){
	printf("%s %s:%d \n",__FILE__, __FUNCTION__ ,__LINE__);
	PlasmaServiceSoap12Proxy service;
	_ns1__DownloadFile file;
	_ns1__DownloadFileResponse result;
	std::string tmpname=name;
	file.internalName=&tmpname;

	if (service.DownloadFile(&file, &result) == SOAP_OK)
		if (result.DownloadFileResult){
			printf("download ok\n");
			std::string fileName=this->torrentPath + *result.torrentFileName;

			printf("%s\n",fileName.c_str());
			xsd__base64Binary *torrent_buf=result.torrent;
			FILE* f=fopen(fileName.c_str(),"wb");
			fwrite(torrent_buf->__ptr, torrent_buf->__size, 1, f);
			fclose(f);

			std::vector<std::string>::iterator it;
			for(it=result.links->string.begin();it!=result.links->string.end(); it++){
				printf("%s\n",(*it).c_str());
			}
			for(it=result.dependencies->string.begin();it!=result.dependencies->string.end(); it++){
				printf("%s\n",(*it).c_str());
			}
		}else
			printf("Nothing found\n");
	else
      printf("soap!=ok\n");
}

void CPlasmaDownloader::start(IDownload* download){
	printf("%s %s:%d \n",__FILE__, __FUNCTION__ ,__LINE__);
}
const IDownload* CPlasmaDownloader::addDownload(const std::string& url, const std::string& filename){
	printf("%s %s:%d \n",__FILE__, __FUNCTION__ ,__LINE__);
	return NULL;
}
bool CPlasmaDownloader::removeDownload(IDownload& download){
	printf("%s %s:%d \n",__FILE__, __FUNCTION__ ,__LINE__);
	return true;
}