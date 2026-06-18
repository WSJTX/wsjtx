// stdout_shared_memory.cpp
#include "stdout_shared_memory.h"
#include <cstring>
#include <string>


#ifdef _WIN32
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <errno.h>
#ifdef __APPLE__
    #include <sys/resource.h>
#endif
    #include <codecvt>
	#include <QString>
	#include <QByteArray>
#endif

namespace {

#ifndef _WIN32
std::string posixErrorMessage(const char* operation, int err)
{
    return std::string(operation) + " failed, errno=" + std::to_string(err)
        + " (" + std::strerror(err) + ")";
}

#ifdef __APPLE__
std::string rlimToString(rlim_t value)
{
    if (value == RLIM_INFINITY) return "infinity";
    return std::to_string(value);
}

std::string nofileLimitMessage()
{
    struct rlimit limit;
    if (getrlimit(RLIMIT_NOFILE, &limit) != 0) {
        return ", RLIMIT_NOFILE unavailable";
    }

    return ", RLIMIT_NOFILE soft=" + rlimToString(limit.rlim_cur)
        + " hard=" + rlimToString(limit.rlim_max);
}

bool raiseFileDescriptorLimit()
{
    struct rlimit limit;
    if (getrlimit(RLIMIT_NOFILE, &limit) != 0) return false;

    constexpr rlim_t target = 65536;
    if (limit.rlim_cur >= target) return false;

    rlim_t newSoftLimit = target;
    if (limit.rlim_max != RLIM_INFINITY && newSoftLimit > limit.rlim_max) {
        newSoftLimit = limit.rlim_max;
    }
    if (newSoftLimit <= limit.rlim_cur) return false;

    limit.rlim_cur = newSoftLimit;
    return setrlimit(RLIMIT_NOFILE, &limit) == 0;
}
#endif
#endif

}

StdoutSharedMemory::StdoutSharedMemory(const std::wstring &mappingName,
                                       std::size_t totalBytes)
    : handle_(nullptr), view_(nullptr), sizeBytes_(totalBytes)
{
#ifdef _WIN32
    ULONGLONG size64 = static_cast<ULONGLONG>(totalBytes);

    HANDLE hMap = ::CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        static_cast<DWORD>(size64 >> 32),
        static_cast<DWORD>(size64 & 0xFFFFFFFFULL),
        mappingName.c_str()
    );

    if (!hMap)
        throw std::runtime_error("CreateFileMappingW failed");

    void* view = ::MapViewOfFile(
        hMap,
        FILE_MAP_ALL_ACCESS,
        0, 0,
        static_cast<SIZE_T>(totalBytes)
    );

    if (!view) {
        ::CloseHandle(hMap);
        throw std::runtime_error("MapViewOfFile failed");
    }

    handle_ = hMap;
    view_   = view;

#else
   // Convert wide string to UTF‑8 for shm_open
    QString qname = QString::fromStdWString(mappingName);
    std::string name = "/" + qname.toUtf8().toStdString();

    // Best-effort cleanup of any stale mapping from a previous run.
    // If it doesn't exist, this just fails harmlessly.
    shm_unlink(name.c_str());

    int fd = shm_open(name.c_str(), O_CREAT | O_RDWR, 0600);
    if (fd < 0) {
        int err = errno;
#ifdef __APPLE__
        if ((err == EMFILE || err == ENFILE) && raiseFileDescriptorLimit()) {
            fd = shm_open(name.c_str(), O_CREAT | O_RDWR, 0600);
            if (fd >= 0) {
                err = 0;
            } else {
                err = errno;
            }
        }
#endif
        if (fd < 0) {
            std::string message = posixErrorMessage("shm_open", err);
#ifdef __APPLE__
            message += nofileLimitMessage();
#endif
            throw std::runtime_error(message);
        }
    }

    if (ftruncate(fd, totalBytes) != 0) {
        int err = errno;
        close(fd);
        shm_unlink(name.c_str());
        throw std::runtime_error(posixErrorMessage("ftruncate", err));
    }

    void* view = mmap(nullptr, totalBytes,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, 0);

    if (view == MAP_FAILED) {
        int err = errno;
        close(fd);
        shm_unlink(name.c_str());
        throw std::runtime_error(posixErrorMessage("mmap", err));
    }

    close(fd);
    shm_unlink(name.c_str());
    handle_ = nullptr;
    view_   = view;
#endif


    // Initialize header
    auto* region = getRegion();
    region->header.version    = 1;
    region->header.writeIndex = 0;
    region->header.dataSize   = 0;
    region->header.seq        = 0;
}

StdoutSharedMemory::~StdoutSharedMemory()
{
#ifdef _WIN32
    if (view_) {
        ::UnmapViewOfFile(view_);
        view_ = nullptr;
    }
    if (handle_) {
        ::CloseHandle((HANDLE)handle_);
        handle_ = nullptr;
    }
#else
    if (view_) {
        munmap(view_, sizeBytes_);
        view_ = nullptr;
    }
    if (handle_) {
        int fd = static_cast<int>(reinterpret_cast<intptr_t>(handle_));
        close(fd);
        handle_ = nullptr;
    }
#endif
}

StdoutSharedMemory::StdoutSharedMemory(StdoutSharedMemory&& other) noexcept
    : handle_(other.handle_), view_(other.view_), sizeBytes_(other.sizeBytes_)
{
    other.handle_ = nullptr;
    other.view_   = nullptr;
    other.sizeBytes_ = 0;
}

StdoutSharedMemory& StdoutSharedMemory::operator=(StdoutSharedMemory&& other) noexcept
{
    if (this != &other) {
        this->~StdoutSharedMemory();
        handle_ = other.handle_;
        view_   = other.view_;
        sizeBytes_ = other.sizeBytes_;
        other.handle_ = nullptr;
        other.view_   = nullptr;
        other.sizeBytes_ = 0;
    }
    return *this;
}
