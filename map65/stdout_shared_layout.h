// stdout_shared_layout.h
#pragma once
#include <cstdint>

#pragma pack(push, 1)
struct StdoutSharedHeader
{
    std::uint32_t version;       // For compatibility
    std::uint32_t writeIndex;    // Next write position in buffer
    std::uint32_t dataSize;      // Bytes of new data just written
    std::uint32_t seq;           // Incremented each write (helps detect missed events)
};

struct StdoutSharedRegion
{
    StdoutSharedHeader header;
    // Text buffer, size chosen at creation (e.g. 8 KB / 32 KB).
    std::uint8_t       buffer[1];
};
#pragma pack(pop)