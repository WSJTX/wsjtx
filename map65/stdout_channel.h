// stdout_channel.h
#pragma once
#include "stdout_shared_memory.h"
#include <string>
#include <stdexcept>

// Cross-platform event API implemented in win_event_shim.c or posix_event_shim.c
extern "C" {
    void* win_create_event();
    int   win_set_event(void*);
    int   win_reset_event(void*);
    int   win_wait_for_single_object(void*);
    void  win_close_event(void*);
}

struct StdoutChannel
{
    StdoutSharedMemory shared;
    void*              eventHandle;

    StdoutChannel(const std::wstring &mappingName,
                  const std::wstring & /*eventName*/,
                  std::size_t bufferBytes)
        : shared(mappingName, sizeof(StdoutSharedHeader) + bufferBytes),
          eventHandle(nullptr)
    {
        // Cross-platform event creation
        eventHandle = win_create_event();
        if (!eventHandle)
            throw std::runtime_error("win_create_event failed for stdout channel");
    }

    ~StdoutChannel()
    {
        if (eventHandle) {
            win_close_event(eventHandle);
            eventHandle = nullptr;
        }
    }

    StdoutChannel(const StdoutChannel&) = delete;
    StdoutChannel& operator=(const StdoutChannel&) = delete;
};
