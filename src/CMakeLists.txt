add_subdirectory(lib)

add_library(Downloader OBJECT
    Downloader/Rapid/RapidDownloader.cpp
    Downloader/Rapid/Repo.cpp
    Downloader/Rapid/Sdp.cpp
    Downloader/Http/DownloadData.cpp
    Downloader/Http/ETag.cpp
    Downloader/Http/HttpDownloader.cpp
    Downloader/Http/IOThreadPool.cpp
    Downloader/Http/Throttler.cpp
    Downloader/CurlWrapper.cpp
    Downloader/Download.cpp
    Downloader/IDownloader.cpp
    Downloader/DownloadEnum.cpp
    FileSystem/FileSystem.cpp
    FileSystem/File.cpp
    FileSystem/HashGzip.cpp
    FileSystem/HashMD5.cpp
    FileSystem/HashSHA1.cpp
    FileSystem/IHash.cpp
    FileSystem/SevenZipArchive.cpp
    FileSystem/ZipArchive.cpp
    Util.cpp
    Version.cpp
    Logger.cpp
    pr-downloader.cpp
    lib/base64/base64.cpp
)
target_include_directories(Downloader
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    PRIVATE ${READERWRITERQUEUE_INCLUDE_DIRS}
)
set_source_files_properties(Version.cpp PROPERTIES COMPILE_DEFINITIONS "PR_DOWNLOADER_VERSION=${PR_DOWNLOADER_VERSION}")
set_property(TARGET Downloader PROPERTY POSITION_INDEPENDENT_CODE ON)

IF (WIN32)
    SET(WIN32LIBS ws2_32 mswsock)
ENDIF ()

target_link_libraries(Downloader
    CURL::libcurl
    ${WIN32LIBS}
    JsonCpp::JsonCpp
    ZLIB::ZLIB
    pr-md5
    pr-sha1
    7zip
    ${MINIZIP_TARGET}
)

add_library(pr-downloader_shared SHARED)
target_link_libraries(pr-downloader_shared PUBLIC Downloader)

add_library(pr-downloader_static STATIC)
target_link_libraries(pr-downloader_static PUBLIC Downloader)

add_executable(pr-downloader main.cpp)

if (PRD_LINK_STATIC)
    target_link_libraries(pr-downloader pr-downloader_static)
else ()
    target_link_libraries(pr-downloader pr-downloader_shared)
endif ()
if (WIN32)
    set_target_properties(pr-downloader PROPERTIES LINK_FLAGS "-Wl,-subsystem,console")
endif ()
