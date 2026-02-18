// stdout_channel.h
#pragma once
#include "stdout_shared_memory.h"
#include <windows.h>
#include <string>
#include <stdexcept>

struct StdoutChannel
{
    StdoutSharedMemory shared;
    HANDLE             eventHandle;

    StdoutChannel(const std::wstring &mappingName,
                  const std::wstring &eventName,
                  std::size_t bufferBytes)
        : shared(mappingName, sizeof(StdoutSharedHeader) + bufferBytes),
          eventHandle(NULL)
    {
        eventHandle = ::CreateEventW(
            /*lpEventAttributes*/ nullptr,
            /*bManualReset*/      FALSE,   // auto-reset on wait
            /*bInitialState*/     FALSE,
            eventName.c_str()
        );

        if (!eventHandle)
            throw std::runtime_error("CreateEventW failed for stdout channel");
    }

    ~StdoutChannel()
    {
        if (eventHandle) {
            ::CloseHandle(eventHandle);
            eventHandle = NULL;
        }
    }

    StdoutChannel(const StdoutChannel&) = delete;
    StdoutChannel& operator=(const StdoutChannel&) = delete;
};