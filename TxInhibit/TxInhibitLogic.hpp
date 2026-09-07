#ifndef TX_INHIBIT_LOGIC_HPP__
#define TX_INHIBIT_LOGIC_HPP__

// ---------------------------------------------------------------------------
// Pure, I/O-free TX Inhibit logic (parse + per-controller leases)
//
// No sockets, no serial. Unit-testable; keep in sync with KEY-agent senders.
//
// Glossary: docs/TX_INHIBIT.md
//
//   assert PTT  ⇔  want_tx  and  not hold
//
//   Hold = logical OR of per-controller leases. Each NetworkMessage::TxInhibit
//   (type 18) refreshes or releases only that Controller ID's row. Missed
//   refresh → that row expires (deadman). Agent hang is KEY-agent only.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// ---------------------------------------------------------------------------

#include <exception>

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include "Network/NetworkMessage.hpp"

namespace TxInhibit {

// Project builds with --std=gnu++11 (-Werror); keep this header C++11-clean.
// Non-zero TTL ms must land in this range (0 is reserved for release hold).
static constexpr int hold_timeout_ms_min = 100;
static constexpr int hold_timeout_ms_max = 30000;
static constexpr int ttl_ms_min = hold_timeout_ms_min;
static constexpr int ttl_ms_max = hold_timeout_ms_max;
static constexpr int max_datagram_bytes = 512;

// Parsed view of one KEY-agent UDP type-18 body.
// valid == false means "ignore this packet; do not change hold state."
struct Datagram
{
  QString target_id;      // NetworkMessage Id; empty = any instance at this port
  QString controller_id;  // lease key; required non-empty when valid
  // Wire TTL ms; meaning hold_timeout_ms (>0) or 0 = release this controller.
  int ttl_ms {0};
  QString station;        // badge: "held by …"
  bool valid {false};
};

// Build one type-18 datagram (agents / tests). Empty target_id = any instance.
inline QByteArray build_datagram (QString const& controller_id
                                  , quint32 ttl_ms
                                  , QString const& station = QString {}
                                  , QString const& target_id = QString {})
{
  QByteArray message;
  NetworkMessage::Builder out {&message, NetworkMessage::TxInhibit, target_id
                               , NetworkMessage::Builder::schema_number};
  out << controller_id.toUtf8 () << ttl_ms << station.toUtf8 ();
  return message;
}

// Validate and unpack one UDP payload. On any failure returns valid=false
// (empty Datagram) so callers can count invalids without partial updates.
inline Datagram parse_datagram (QByteArray const& data)
{
  Datagram out;
  if (data.isEmpty () || data.size () > max_datagram_bytes)
    {
      return out;
    }
  try
    {
      NetworkMessage::Reader in {data};
      if (in.type () != NetworkMessage::TxInhibit)
        {
          return out;
        }
      QByteArray controller_id;
      quint32 ttl = 0;
      QByteArray station;
      in >> controller_id >> ttl >> station;
      if (in.status () != QDataStream::Ok)
        {
          return out;
        }
      if (controller_id.isEmpty ())
        {
          return out;
        }
      // 0 = release this controller; otherwise enforce hold_timeout_ms range.
      if (ttl != 0
          && (ttl < static_cast<quint32> (hold_timeout_ms_min)
              || ttl > static_cast<quint32> (hold_timeout_ms_max)))
        {
          return out;
        }
      out.target_id = in.id ();
      out.controller_id = QString::fromUtf8 (controller_id);
      out.ttl_ms = static_cast<int> (ttl);
      out.station = QString::fromUtf8 (station);
      out.valid = true;
      return out;
    }
  catch (std::exception const&)
    {
      return out;
    }
  catch (...)
    {
      return out;
    }
}

// Per-controller leases, OR'd. One expiring row per Controller ID.
// now_ms is supplied by the caller (same time base as lease expiry times).
class GateLogic
{
public:
  // NetworkMessage Id of this WSJT-X instance. Non-empty target Id on a
  // type-18 datagram must match; empty target Id is accepted (any instance
  // at this UDP address/port).
  void set_instance_id (QString const& id) { instance_id_ = id; }
  QString instance_id () const { return instance_id_; }

  // Apply one hold/release packet to the lease map.
  //
  // Return value:
  //   true  = the hold *level* flipped (free↔held)
  //   false = level unchanged — keepalives, invalid packets, Id mismatch,
  //           redundant releases, or another controller still holding.
  bool on_datagram (QByteArray const& data, qint64 now_ms)
  {
    auto msg = parse_datagram (data);
    if (!msg.valid)
      {
        ++invalid_;
        return false;
      }
    if (!msg.target_id.isEmpty () && msg.target_id != instance_id_)
      {
        ++invalid_;
        return false;
      }
    bool const before = inhibited (now_ms);
    if (msg.ttl_ms == 0)
      {
        ++release_rx_;
        leases_.remove (msg.controller_id);
      }
    else
      {
        ++hold_rx_;
        Lease row;
        row.expires_at_ms = now_ms + msg.ttl_ms;
        row.station = msg.station;
        leases_.insert (msg.controller_id, row);
      }
    return inhibited (now_ms) != before;
  }

  // Hold active? Also applies per-row hold timeout (deadman / agent silence).
  bool inhibited (qint64 now_ms)
  {
    purge_expired (now_ms);
    return !leases_.isEmpty ();
  }

  // Station strings for live leases (sorted by controller id); empty if none.
  QStringList holding_stations (qint64 now_ms)
  {
    purge_expired (now_ms);
    QStringList names;
    for (auto it = leases_.constBegin (); it != leases_.constEnd (); ++it)
      {
        if (!it.value ().station.isEmpty ())
          {
            names.append (it.value ().station);
          }
      }
    return names;
  }

  // True => must not assert PTT (regardless of want_tx).
  bool line_inhibited (qint64 now_ms)
  {
    return inhibited (now_ms);
  }

  // Pin formula (unit-test friendly): assert PTT ⇔ want_tx && !hold.
  bool radiate (bool intent, qint64 now_ms)
  {
    return intent && !line_inhibited (now_ms);
  }

  // Status-bar text; empty means no hold (hide the badge).
  QString badge_text (qint64 now_ms)
  {
    if (!inhibited (now_ms))
      {
        return {};
      }
    auto names = holding_stations (now_ms);
    if (names.isEmpty ())
      {
        return QStringLiteral ("TX INHIBITED");
      }
    return QStringLiteral ("TX INHIBITED — held by %1").arg (names.join (QStringLiteral (", ")));
  }

  quint32 hold_rx () const { return hold_rx_; }
  quint32 release_rx () const { return release_rx_; }
  quint32 expiries () const { return expiries_; }
  quint32 invalid () const { return invalid_; }
  int lease_count (qint64 now_ms)
  {
    purge_expired (now_ms);
    return leases_.size ();
  }

private:
  struct Lease
  {
    qint64 expires_at_ms {-1};
    QString station;
  };

  void purge_expired (qint64 now_ms)
  {
    for (auto it = leases_.begin (); it != leases_.end (); )
      {
        if (it.value ().expires_at_ms >= 0 && now_ms >= it.value ().expires_at_ms)
          {
            it = leases_.erase (it);
            ++expiries_;
          }
        else
          {
            ++it;
          }
      }
  }

  QString instance_id_;
  QMap<QString, Lease> leases_;
  quint32 hold_rx_ {0};
  quint32 release_rx_ {0};
  quint32 expiries_ {0};
  quint32 invalid_ {0};
};

} // namespace TxInhibit

#endif
