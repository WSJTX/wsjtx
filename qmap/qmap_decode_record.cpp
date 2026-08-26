#include "qmap_decode_record.h"

#include "Decoder/decodedtext.h"

std::optional<QMapDecodeRecord> parseQMapDecodeRecord (QByteArray const& row)
{
  if (row.size () != static_cast<int> (QMapDecodeRowSize)) return std::nullopt;

  auto text = QString::fromLatin1 (row.constData (), row.size ());
  auto const terminator = text.indexOf (QChar::Null);
  if (terminator >= 0) text.truncate (terminator);

  bool timeOk {};
  bool receiveOk {};
  bool scheduledOk {};
  bool snrOk {};
  auto const hhmmss = text.left (6).toInt (&timeOk);
  auto const hour = hhmmss / 10000;
  auto const minute = (hhmmss / 100) % 100;
  auto const second = hhmmss % 100;

  QMapDecodeRecord record;
  record.raw = row;
  record.time = text.left (6);
  record.receiveFrequencyKHz = text.mid (6, 9).toDouble (&receiveOk);
  record.scheduledFrequencyKHz = text.mid (15, 7).toDouble (&scheduledOk);
  record.snr = text.mid (29, 5).toInt (&snrOk);
  record.submode = text.mid (36, 3).trimmed ();
  auto const fields = parseDecodedMessage (text.mid (41));
  record.callsign = fields.sender;
  record.grid = fields.grid;
  record.secondsSinceMidnight = hour * 3600 + minute * 60 + second;
  record.secondHalf = second == 30;
  record.cq = fields.cq;

  if (!timeOk || !receiveOk || !scheduledOk || !snrOk
      || hour > 23 || minute > 59 || second > 59 || record.submode.isEmpty ()) {
    return std::nullopt;
  }
  return record;
}
