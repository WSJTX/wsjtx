#include "PSKReporterIPFIX.hpp"

#include <QBuffer>
#include <QDataStream>
#include <QtGlobal>

namespace
{
  constexpr int MAX_UDP_IP_PACKET_BYTES = 1000;
  constexpr int IPV6_HEADER_BYTES = 40;
  constexpr int UDP_HEADER_BYTES = 8;
  constexpr int MAX_UDP_IPFIX_PAYLOAD_BYTES = MAX_UDP_IP_PACKET_BYTES - IPV6_HEADER_BYTES - UDP_HEADER_BYTES;
  constexpr int MAX_TCP_IPFIX_PAYLOAD_BYTES = 0xffff;
  constexpr bool ALIGNMENT_PADDING = true;

  constexpr int CALLSIGN_LIMIT = 32;
  constexpr int LOCATOR_LIMIT = 16;
  constexpr int MODE_LIMIT = 16;
  constexpr int PROGRAM_INFO_LIMIT = 80;
  constexpr int ANTENNA_LIMIT = 128;
  constexpr int RIG_INFORMATION_LIMIT = 128;

  // PSK Reporter uses enterprise-specific IPFIX elements under PEN 30351.
  // Template 0x50e3 describes sender/spot records; template 0x50e2 describes
  // receiver metadata, with receiverCallsign as the options-template scope.
  constexpr quint32 PSK_REPORTER_PEN = 30351;
  constexpr quint16 SENDER_TEMPLATE_ID = 0x50e3;
  constexpr quint16 RECEIVER_TEMPLATE_ID = 0x50e2;

  int num_pad_bytes (int len)
  {
    return ALIGNMENT_PADDING ? (4 - len % 4) % 4 : 0;
  }

  QByteArray boundedUtf8 (QString const& value, int max_bytes)
  {
    auto utf8 = value.toUtf8 ();
    if (utf8.size () <= max_bytes)
      {
        return utf8;
      }

    auto bounded_size = qMax (0, max_bytes);
    while (bounded_size > 0
           && (static_cast<quint8> (utf8[bounded_size]) & 0xc0) == 0x80)
      {
        --bounded_size;
      }
    return utf8.left (bounded_size);
  }

  void writeUtfString (QDataStream& out, QString const& value, int max_bytes)
  {
    auto const utf8 = boundedUtf8 (value, max_bytes);
    out << quint8 (utf8.size ());
    out.writeRawData (utf8.constData (), utf8.size ());
  }

  void setLength (QDataStream& out, QByteArray& bytes, int offset = sizeof (quint16))
  {
    auto const pad_len = num_pad_bytes (bytes.size ());
    if (pad_len)
      {
        out.writeRawData (QByteArray {pad_len, '\0'}.constData (), pad_len);
      }

    auto const pos = out.device ()->pos ();
    out.device ()->seek (offset);
    out << static_cast<quint16> (bytes.size ());
    out.device ()->seek (pos);
  }

  QByteArray descriptorSets ()
  {
    QByteArray sets;
    {
      // Sender Information template: callsign, frequency, SNR, mode, locator,
      // information source, and spot timestamp.
      QByteArray descriptor;
      QDataStream out {&descriptor, QIODevice::WriteOnly};
      out
        << quint16 (2u)
        << quint16 (0u)
        << quint16 (SENDER_TEMPLATE_ID)
        << quint16 (7u)
        << quint16 (0x8000 + 1u)  // senderCallsign
        << quint16 (0xffff)
        << PSK_REPORTER_PEN
        << quint16 (0x8000 + 5u)  // frequency, 5-byte unsigned integer
        << quint16 (5u)
        << PSK_REPORTER_PEN
        << quint16 (0x8000 + 6u)  // sNR
        << quint16 (1u)
        << PSK_REPORTER_PEN
        << quint16 (0x8000 + 10u) // mode
        << quint16 (0xffff)
        << PSK_REPORTER_PEN
        << quint16 (0x8000 + 3u)  // senderLocator
        << quint16 (0xffff)
        << PSK_REPORTER_PEN
        << quint16 (0x8000 + 11u) // informationSource
        << quint16 (1u)
        << PSK_REPORTER_PEN
        << quint16 (150u)         // dateTimeSeconds
        << quint16 (4u);
      setLength (out, descriptor);
      sets.append (descriptor);
    }
    {
      // Receiver Information options template. Scope Field Count must remain 1:
      // receiverCallsign scopes receiver locator, software, antenna, and rig.
      QByteArray descriptor;
      QDataStream out {&descriptor, QIODevice::WriteOnly};
      out
        << quint16 (3u)
        << quint16 (0u)
        << quint16 (RECEIVER_TEMPLATE_ID)
        << quint16 (5u)
        << quint16 (1u)
        << quint16 (0x8000 + 2u)  // receiverCallsign, scope field
        << quint16 (0xffff)
        << PSK_REPORTER_PEN
        << quint16 (0x8000 + 4u)  // receiverLocator
        << quint16 (0xffff)
        << PSK_REPORTER_PEN
        << quint16 (0x8000 + 8u)  // decodingSoftware
        << quint16 (0xffff)
        << PSK_REPORTER_PEN
        << quint16 (0x8000 + 9u)  // antennaInformation
        << quint16 (0xffff)
        << PSK_REPORTER_PEN
        << quint16 (0x8000 + 13u) // rigInformation
        << quint16 (0xffff)
        << PSK_REPORTER_PEN;
      setLength (out, descriptor);
      sets.append (descriptor);
    }
    return sets;
  }

  QByteArray receiverSet (PSKReporterIPFIX::Receiver const& receiver)
  {
    QByteArray data;
    QDataStream out {&data, QIODevice::WriteOnly};
    out << quint16 (RECEIVER_TEMPLATE_ID) << quint16 (0u);
    writeUtfString (out, receiver.callsign, CALLSIGN_LIMIT);
    writeUtfString (out, receiver.locator, LOCATOR_LIMIT);
    writeUtfString (out, receiver.program_info, PROGRAM_INFO_LIMIT);
    writeUtfString (out, receiver.antenna, ANTENNA_LIMIT);
    writeUtfString (out, receiver.rig_information, RIG_INFORMATION_LIMIT);
    setLength (out, data);
    return data;
  }

  int senderSetLength (int record_bytes)
  {
    auto const len = 2 * sizeof (quint16) + record_bytes;
    return len + num_pad_bytes (len);
  }

  int messageLength (int set_bytes)
  {
    auto const len = 2 * sizeof (quint16) + 3 * sizeof (quint32) + set_bytes;
    return len + num_pad_bytes (len);
  }

  QByteArray spotRecord (PSKReporterIPFIX::Spot const& spot)
  {
    QByteArray data;
    QDataStream out {&data, QIODevice::WriteOnly};
    // Field order must match the sender template above.
    writeUtfString (out, spot.callsign, CALLSIGN_LIMIT);

    out
      << static_cast<quint8> ((spot.frequency >> 32) & 0xff)
      << static_cast<quint32> (spot.frequency & 0xffffffffu)
      << static_cast<qint8> (spot.snr);

    writeUtfString (out, spot.mode, MODE_LIMIT);
    writeUtfString (out, spot.locator, LOCATOR_LIMIT);
    out
      << quint8 (1u)
      << static_cast<quint32> (
#if QT_VERSION >= QT_VERSION_CHECK(5, 8, 0)
                               spot.time.toSecsSinceEpoch ()
#else
                               spot.time.toMSecsSinceEpoch () / 1000
#endif
                               );
    return data;
  }

  QByteArray senderSet (QList<QByteArray> const& records)
  {
    QByteArray data;
    QDataStream out {&data, QIODevice::WriteOnly};
    out << quint16 (SENDER_TEMPLATE_ID) << quint16 (0u);
    for (auto const& record : records)
      {
        out.writeRawData (record.constData (), record.size ());
      }
    setLength (out, data);
    return data;
  }

  QByteArray message (QByteArray const& sets, quint32 sequence_number, quint32 observation_id, quint32 export_time)
  {
    QByteArray data;
    QDataStream out {&data, QIODevice::WriteOnly};
    out
      << quint16 (10u)
      << quint16 (0u)
      << export_time
      << sequence_number
      << observation_id;
    out.writeRawData (sets.constData (), sets.size ());
    setLength (out, data);
    return data;
  }
}

namespace PSKReporterIPFIX
{
  int maxUdpIpfixPayloadBytes ()
  {
    return MAX_UDP_IPFIX_PAYLOAD_BYTES;
  }

  int maxTcpIpfixPayloadBytes ()
  {
    return MAX_TCP_IPFIX_PAYLOAD_BYTES;
  }

  QList<Packet> buildPackets (Receiver const& receiver, QList<Spot> const& spots
                              , bool include_descriptors, quint32 sequence_number
                              , quint32 observation_id, quint32 export_time
                              , int max_payload_bytes)
  {
    QList<Packet> packets;
    QByteArray first_base_sets;
    if (include_descriptors)
      {
        first_base_sets.append (descriptorSets ());
      }
    auto const receiver_set = receiverSet (receiver);
    first_base_sets.append (receiver_set);

    auto const base_payload = message (first_base_sets, sequence_number, observation_id, export_time);
    Q_ASSERT (base_payload.size () <= max_payload_bytes);
    if (spots.isEmpty ())
      {
        packets.append ({base_payload, 0});
        return packets;
      }

    QList<QByteArray> records;
    int record_count = 0;
    int record_bytes = 0;
    auto base_sets = first_base_sets;
    for (auto const& spot : spots)
      {
        auto const record = spotRecord (spot);
        auto const candidate_record_bytes = record_bytes + record.size ();
        auto const candidate_payload_size = messageLength (base_sets.size () + senderSetLength (candidate_record_bytes));
        if (!records.isEmpty () && candidate_payload_size > max_payload_bytes)
          {
            auto const payload = message (base_sets + senderSet (records), sequence_number, observation_id, export_time);
            Q_ASSERT (payload.size () <= max_payload_bytes);
            packets.append ({payload, record_count});
            sequence_number += record_count;
            records.clear ();
            record_count = 0;
            record_bytes = 0;
            // Follow-on split packets keep receiver data but do not repeat
            // descriptors until the caller schedules the next descriptor send.
            base_sets = receiver_set;
          }

        records.append (record);
        ++record_count;
        record_bytes += record.size ();
        Q_ASSERT (messageLength (base_sets.size () + senderSetLength (record_bytes)) <= max_payload_bytes);
      }

    if (!records.isEmpty ())
      {
        auto const payload = message (base_sets + senderSet (records), sequence_number, observation_id, export_time);
        Q_ASSERT (payload.size () <= max_payload_bytes);
        packets.append ({payload, record_count});
      }

    return packets;
  }
}
