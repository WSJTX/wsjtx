#ifndef TX_INHIBIT_LOGIC_HPP__
#define TX_INHIBIT_LOGIC_HPP__

// ---------------------------------------------------------------------------
// Pure, I/O-free TX Inhibit logic (parse + hold timeout)
//
// No sockets, no serial. Unit-testable; keep in sync with KEY-agent senders.
//
// Glossary: docs/TX_INHIBIT.md
//
//   assert PTT  ⇔  want_tx and not hold
//
//   WSJT-X station only: hold packets + hold_timeout_ms (wire ttl_ms) — safety
//   timeout if keepalives stop. Agent hang (CW anti-chatter after release
//   KEY, then release hold ttl_ms:0) is KEY-agent only — not here.
//   See docs/TX_INHIBIT.md §3.1.
//
//   1. Datagram: ttl_ms>0 hold/keepalive (hold_timeout_ms); ttl_ms==0 release.
//   2. hold active while now < hold_timeout_at_ms_.
//   3. line_inhibited => must not assert PTT (UDP hold only; no local CTS).
//
// SPDX-License-Identifier: GPL-3.0-or-later
// ---------------------------------------------------------------------------

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QtGlobal>

namespace TxInhibit {

// Project builds with --std=gnu++11 (-Werror); keep this header C++11-clean.
static constexpr char const * protocol_key = "tx_inhibit";
static constexpr int protocol_version = 1;
// Well-known WSJT-X station listen / agent send port (IPv4). Ephemeral fallback is
// handled by TxInhibitGate if bind fails.
static constexpr quint16 default_gate_port = 22372;
// Non-zero hold_timeout_ms (wire ttl_ms) must land in this range
// (0 is reserved for release hold).
static constexpr int hold_timeout_ms_min = 100;
static constexpr int hold_timeout_ms_max = 30000;
// Aliases for older call sites / tests.
static constexpr int ttl_ms_min = hold_timeout_ms_min;
static constexpr int ttl_ms_max = hold_timeout_ms_max;
static constexpr int max_datagram_bytes = 512;

// Parsed view of one KEY-agent UDP JSON body.
// valid == false means "ignore this packet; do not change hold state."
struct Datagram
{
  // Wire name ttl_ms; meaning hold_timeout_ms (>0) or 0 = release hold.
  int ttl_ms {0};
  QString station;      // badge: "held by …"
  QString band;         // informational (WSJT-X station does not filter by band today)
  qint64 seq {0};       // informational agent sequence
  bool valid {false};
};

// Validate and unpack one UDP payload. On any failure returns valid=false
// (empty Datagram) so callers can count invalids without partial updates.
inline Datagram parse_datagram (QByteArray const& data)
{
  Datagram out;
  if (data.isEmpty () || data.size () > max_datagram_bytes)
    {
      return out;
    }
  QJsonParseError err;
  auto doc = QJsonDocument::fromJson (data, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject ())
    {
      return out;
    }
  auto obj = doc.object ();
  // Protocol id must be exactly protocol_version (currently 1).
  if (obj.value (QLatin1String (protocol_key)).toInt (-1) != protocol_version)
    {
      return out;
    }
  // Accept JSON number (Qt stores as Double) or string digits (interop).
  if (!obj.contains (QLatin1String ("ttl_ms")))
    {
      return out;
    }
  int ttl = -1;
  {
    auto const ttl_v = obj.value (QLatin1String ("ttl_ms"));
    if (ttl_v.isDouble ())
      {
        ttl = ttl_v.toInt (-1);
      }
    else if (ttl_v.isString ())
      {
        bool ok = false;
        ttl = ttl_v.toString ().toInt (&ok);
        if (!ok)
          {
            return out;
          }
      }
    else
      {
        return out;
      }
  }
  // 0 = release hold; otherwise enforce hold_timeout_ms range.
  if (ttl != 0 && (ttl < hold_timeout_ms_min || ttl > hold_timeout_ms_max))
    {
      return out;
    }
  out.ttl_ms = ttl; // hold_timeout_ms
  out.station = obj.value (QLatin1String ("station")).toString ();
  out.band = obj.value (QLatin1String ("band")).toString ();
  out.seq = static_cast<qint64> (obj.value (QLatin1String ("seq")).toDouble (0));
  out.valid = true;
  return out;
}

// One UDP hold at a time. Last valid hold wins.
// now_ms is supplied by the caller (same time base as hold_timeout_at_ms_).
class GateLogic
{
public:
  // Apply one hold/release packet to the UDP-hold state machine.
  //
  // Updates hold timeout from the datagram; does not touch serial lines or
  // want_tx (those live in TxInhibitGate).
  //
  // Return value (important):
  //   true  = the hold *level* flipped (free↔held), i.e.
  //           inhibited(now) after != inhibited(now) before.
  //   false = level unchanged — including keepalives that only refresh
  //           hold_timeout_ms, invalid packets, and redundant releases.
  //
  // This is NOT "datagram says hold" (a hold while already held returns
  // false). It is NOT "RTS/DTR changed" (see line_inhibited / apply_line).
  // Callers may ignore the return and re-evaluate line_inhibited() instead;
  // TxInhibitGate does exactly that today.
  bool on_datagram (QByteArray const& data, qint64 now_ms)
  {
    auto msg = parse_datagram (data);
    if (!msg.valid)
      {
        ++invalid_;
        return false; // no state change
      }
    // Snapshot hold level *before* mutating hold timeout.
    bool before = inhibited (now_ms);
    if (msg.ttl_ms == 0)
      {
        // Explicit release hold (ttl_ms == 0; normal end after agent hang).
        ++release_rx_;
        hold_timeout_at_ms_ = -1;
      }
    else
      {
        // Hold or keepalive: set hold_timeout_ms and re-arm expiry.
        ++hold_rx_;
        hold_timeout_at_ms_ = now_ms + msg.ttl_ms;
        holder_ = msg.station;
      }
    return inhibited (now_ms) != before;
  }

  // Hold active? Also applies hold timeout: if it has passed, clear hold
  // and count an expiry (includes deadman / agent silence).
  // Side effect on expiry is intentional so a 50 Hz poll can clear hold
  // without another packet.
  bool inhibited (qint64 now_ms)
  {
    if (hold_timeout_at_ms_ >= 0 && now_ms >= hold_timeout_at_ms_)
      {
        hold_timeout_at_ms_ = -1;
        ++expiries_;
      }
    return hold_timeout_at_ms_ >= 0;
  }

  // Station string for the badge while a hold is active; empty if none.
  QString holding_station (qint64 now_ms)
  {
    return inhibited (now_ms) ? holder_ : QString {};
  }

  // True => must not assert PTT (regardless of want_tx).
  // UDP hold only (no local CTS in this build).
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
    if (inhibited (now_ms))
      {
        auto s = holding_station (now_ms);
        if (s.isEmpty ())
          {
            return QStringLiteral ("TX INHIBITED");
          }
        return QStringLiteral ("TX INHIBITED — held by %1").arg (s);
      }
    return {};
  }

  // Diagnostics (optional telemetry / InhibitStatus).
  quint32 hold_rx () const { return hold_rx_; }
  quint32 release_rx () const { return release_rx_; }
  quint32 expiries () const { return expiries_; }
  quint32 invalid () const { return invalid_; }

private:
  // Absolute time when the current hold ends (same epoch as now_ms args).
  // -1 means no hold. Set to now_ms + hold_timeout_ms (wire ttl_ms) on each
  // hold/keepalive packet.
  qint64 hold_timeout_at_ms_ {-1};
  QString holder_;           // last hold's station field
  quint32 hold_rx_ {0};
  quint32 release_rx_ {0};
  quint32 expiries_ {0};
  quint32 invalid_ {0};
};

} // namespace TxInhibit

#endif
