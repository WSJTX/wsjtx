// stdout_shared_memory.h
#pragma once
#include "stdout_shared_layout.h"
#include <windows.h>
#include <string>
#include <stdexcept>

class StdoutSharedMemory
{
public:
    StdoutSharedMemory(const std::wstring &mappingName,
                       std::size_t totalBytes)
        : hMap_(NULL), view_(nullptr), sizeBytes_(totalBytes)
    {
        hMap_ = ::CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            static_cast<DWORD>((totalBytes >> 32) & 0xFFFFFFFFu),
            static_cast<DWORD>( totalBytes        & 0xFFFFFFFFu),
            mappingName.c_str()
        );

        if (!hMap_)
            throw std::runtime_error("CreateFileMappingW failed for stdout buffer");

        view_ = ::MapViewOfFile(
            hMap_,
            FILE_MAP_ALL_ACCESS,
            0, 0,
            totalBytes
        );

        if (!view_) {
            ::CloseHandle(hMap_);
            hMap_ = NULL;
            throw std::runtime_error("MapViewOfFile failed for stdout buffer");
        }

        // Initialize header once
        auto *region = getRegion();
        region->header.version    = 1;
        region->header.writeIndex = 0;
        region->header.dataSize   = 0;
        region->header.seq        = 0;
    }

    ~StdoutSharedMemory()
    {
        if (view_) {
            ::UnmapViewOfFile(view_);
            view_ = nullptr;
        }
        if (hMap_) {
            ::CloseHandle(hMap_);
            hMap_ = NULL;
        }
    }

    StdoutSharedMemory(const StdoutSharedMemory&) = delete;
    StdoutSharedMemory& operator=(const StdoutSharedMemory&) = delete;

    StdoutSharedMemory(StdoutSharedMemory&& other) noexcept
        : hMap_(other.hMap_), view_(other.view_), sizeBytes_(other.sizeBytes_)
    {
        other.hMap_ = NULL;
        other.view_ = nullptr;
        other.sizeBytes_ = 0;
    }

    StdoutSharedMemory& operator=(StdoutSharedMemory&& other) noexcept
    {
        if (this != &other) {
            this->~StdoutSharedMemory();
            hMap_ = other.hMap_;
            view_ = other.view_;
            sizeBytes_ = other.sizeBytes_;
            other.hMap_ = NULL;
            other.view_ = nullptr;
            other.sizeBytes_ = 0;
        }
        return *this;
    }

    StdoutSharedRegion* getRegion()
    {
        return static_cast<StdoutSharedRegion*>(view_);
    }

    const StdoutSharedRegion* getRegion() const
    {
        return static_cast<const StdoutSharedRegion*>(view_);
    }

    void* getBufferPtr()
    {
        return getRegion()->buffer;
    }

    std::size_t getBufferSize() const
    {
        return sizeBytes_ - sizeof(StdoutSharedHeader);
    }

    HANDLE getHandle() const { return hMap_; }

private:
    HANDLE hMap_;
    void*  view_;
    std::size_t sizeBytes_;
};