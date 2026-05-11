// stdout_shared_memory.h
#pragma once
#include "stdout_shared_layout.h"
#include <string>
#include <stdexcept>
#include <cstddef>

class StdoutSharedMemory
{
public:
    StdoutSharedMemory(const std::wstring &mappingName,
                       std::size_t totalBytes);

    ~StdoutSharedMemory();

    StdoutSharedMemory(const StdoutSharedMemory&) = delete;
    StdoutSharedMemory& operator=(const StdoutSharedMemory&) = delete;

    StdoutSharedMemory(StdoutSharedMemory&& other) noexcept;
    StdoutSharedMemory& operator=(StdoutSharedMemory&& other) noexcept;

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

    void* getHandle() const { return handle_; }

private:
    void* handle_;      // HANDLE on Windows, fd on Linux
    void* view_;        // mapped region
    std::size_t sizeBytes_;
};
