#ifndef LINRAD_PACKET_HPP
#define LINRAD_PACKET_HPP

#include <cstddef>
#include <QUdpSocket>

template<std::size_t PayloadDoubles>
struct LinradPacketBuffer
{
  double cfreq;
  int msec;
  float userfreq;
  int iptr;
  quint16 iblk;
  qint8 nrx;
  char iusb;
  double d8[PayloadDoubles];

  // The header and packed frames must be whole; optional padding is silence.
  int receive (QUdpSocket& socket, bool pad_short_audio = true)
  {
    *this = {};
    auto const bytes = socket.readDatagram (reinterpret_cast<char *> (this), sizeof *this);
    constexpr auto header_size = offsetof (LinradPacketBuffer, d8);
    if (bytes < static_cast<qint64> (header_size)) return 0;
    int frame_bytes;
    switch (nrx)
      {
      case 1: frame_bytes = 4; break;
      case -1:
      case 2: frame_bytes = 8; break;
      case -2: frame_bytes = 16; break;
      default: return 0;
      }
    if ((bytes - header_size) % frame_bytes) return 0;
    return (pad_short_audio ? sizeof d8 : bytes - header_size) / frame_bytes;
  }
};

using LinradPacket = LinradPacketBuffer<174>;

static_assert (offsetof (LinradPacket, d8) == 24 && sizeof (LinradPacket) == 1416,
               "Linrad packet layout must match the wire format");

#endif
