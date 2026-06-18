#ifndef PSK_REPORTER_IPFIX_HPP_
#define PSK_REPORTER_IPFIX_HPP_

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

#include "Radio.hpp"

namespace PSKReporterIPFIX
{
  int maxUdpIpfixPayloadBytes ();
  int maxTcpIpfixPayloadBytes ();

  struct Receiver
  {
    QString callsign;
    QString locator;
    QString program_info;
    QString antenna;
    QString rig_information;
  };

  struct Spot
  {
    QString callsign;
    QString locator;
    int snr;
    Radio::Frequency frequency;
    QString mode;
    QDateTime time;
  };

  struct Packet
  {
    QByteArray payload;
    int spot_count;
  };

  QList<Packet> buildPackets (Receiver const& receiver, QList<Spot> const& spots
                              , bool include_descriptors, quint32 sequence_number
                              , quint32 observation_id, quint32 export_time
                              , int max_payload_bytes);
}

#endif
