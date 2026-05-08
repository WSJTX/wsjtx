#include "Transceiver/HRDMessage.hpp"

#include <QVector>

namespace
{
  int constexpr header_size_value = 16;
  int constexpr max_message_size_value = 1024 * 1024;
  quint32 constexpr magic_1_value = 0x1234ABCD;
  quint32 constexpr magic_2_value = 0xABCD1234;

  void append_u32_le (QByteArray * bytes, quint32 value)
  {
    bytes->append (static_cast<char> (value & 0xffu));
    bytes->append (static_cast<char> ((value >> 8) & 0xffu));
    bytes->append (static_cast<char> ((value >> 16) & 0xffu));
    bytes->append (static_cast<char> ((value >> 24) & 0xffu));
  }

  quint32 read_u32_le (QByteArray const& bytes, int offset)
  {
    return static_cast<quint32> (static_cast<quint8> (bytes[offset]))
      | (static_cast<quint32> (static_cast<quint8> (bytes[offset + 1])) << 8)
      | (static_cast<quint32> (static_cast<quint8> (bytes[offset + 2])) << 16)
      | (static_cast<quint32> (static_cast<quint8> (bytes[offset + 3])) << 24);
  }

  HRDMessage::ParseResult valid_partial (int required_size)
  {
    return {true, false, required_size, {}, {}};
  }

  HRDMessage::ParseResult invalid (QString const& error)
  {
    return {false, false, 0, {}, error};
  }
}

namespace HRDMessage
{
  int header_size ()
  {
    return header_size_value;
  }

  int max_message_size ()
  {
    return max_message_size_value;
  }

  QByteArray make (QString const& payload)
  {
    QByteArray message;
    auto const payload_units = payload.size () + 1;
    auto const payload_bytes = payload_units * 2;
    auto const message_size = header_size_value + payload_bytes;

    append_u32_le (&message, static_cast<quint32> (message_size));
    append_u32_le (&message, magic_1_value);
    append_u32_le (&message, magic_2_value);
    append_u32_le (&message, 0u);

    auto const utf16 = payload.utf16 ();
    for (int i = 0; i != payload_units; ++i)
      {
        auto const unit = static_cast<quint16> (utf16[i]);
        message.append (static_cast<char> (unit & 0xffu));
        message.append (static_cast<char> ((unit >> 8) & 0xffu));
      }

    return message;
  }

  ParseResult parse (QByteArray const& message)
  {
    if (message.size () < header_size_value)
      {
        return valid_partial (header_size_value);
      }

    auto const message_size = read_u32_le (message, 0);
    auto const magic_1 = read_u32_le (message, 4);
    auto const magic_2 = read_u32_le (message, 8);

    if (magic_1 != magic_1_value || magic_2 != magic_2_value)
      {
        return invalid ("invalid magic values");
      }

    if (message_size < static_cast<quint32> (header_size_value))
      {
        return invalid ("message size is smaller than the header");
      }

    if (message_size > static_cast<quint32> (max_message_size_value))
      {
        return invalid ("message size is too large");
      }

    if (message.size () < static_cast<int> (message_size))
      {
        return valid_partial (static_cast<int> (message_size));
      }

    auto const payload_bytes = static_cast<int> (message_size) - header_size_value;
    if (payload_bytes < 2)
      {
        return invalid ("payload is missing the terminator");
      }

    if (payload_bytes % 2)
      {
        return invalid ("payload is not valid UTF-16");
      }

    QVector<ushort> payload;
    payload.reserve (payload_bytes / 2);
    for (int offset = header_size_value; offset != static_cast<int> (message_size); offset += 2)
      {
        payload.append (static_cast<ushort> (
            static_cast<quint16> (static_cast<quint8> (message[offset]))
            | (static_cast<quint16> (static_cast<quint8> (message[offset + 1])) << 8)));
      }

    if (payload.isEmpty () || payload.back () != 0u)
      {
        return invalid ("payload is missing the terminator");
      }

    return {true, true, static_cast<int> (message_size), QString::fromUtf16 (payload.constData (), payload.size () - 1), {}};
  }
}
