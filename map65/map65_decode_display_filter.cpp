#include "map65_decode_display_filter.h"

#include <QtGlobal>

namespace
{
QString const passMarker {"<Map65DecodePass>"};
constexpr int duplicateFrequencyToleranceHz = 20;
}

bool Map65DecodeDisplayFilter::handleControlLine(QString const& line)
{
  if (!line.startsWith(passMarker)) return false;

  QString const pass = line.mid(passMarker.size()).trimmed().toLower();
  if (pass == "early")
    {
      pass_ = Pass::Early;
      earlyDecodes_.clear();
    }
  else if (pass == "final") pass_ = Pass::Final;
  else if (pass == "manual") pass_ = Pass::Manual;
  else if (pass == "disk") pass_ = Pass::Disk;
  else pass_ = Pass::Unknown;

  return true;
}

bool Map65DecodeDisplayFilter::shouldDisplay(QString const& decodeLine)
{
  if (pass_ == Pass::Manual || pass_ == Pass::Disk || pass_ == Pass::Unknown)
    return true;

  Identity identity;
  if (!parseIdentity(decodeLine, &identity)) return true;

  for (auto const& earlyDecode : earlyDecodes_)
    {
      if (pass_ == Pass::Final && matches(earlyDecode, identity)) return false;
    }

  if (pass_ == Pass::Early) earlyDecodes_.append(identity);
  return true;
}

void Map65DecodeDisplayFilter::completeEarlyPass()
{
  pass_ = Pass::Unknown;
}

void Map65DecodeDisplayFilter::completeCycle()
{
  pass_ = Pass::Unknown;
  earlyDecodes_.clear();
}

bool Map65DecodeDisplayFilter::parseIdentity(QString const& input, Identity * identity)
{
  QString line = input;
  if (line.startsWith('!')) line.remove(0, 1);
  if (line.size() < 53) return false;

  bool khzOk = false;
  bool dfOk = false;
  bool utcOk = false;
  int const khz = line.mid(0, 3).trimmed().toInt(&khzOk);
  int const df = line.mid(3, 5).trimmed().toInt(&dfOk);
  int const utc = line.mid(12, 6).trimmed().toInt(&utcOk);
  int const hour = utc / 100;
  int const minute = utc % 100;
  if (!khzOk || !dfOk || !utcOk || hour < 0 || hour > 23
      || minute < 0 || minute > 59)
    return false;

  QString const separator = line.mid(28, 3);
  if (separator != " : " && separator != " # ") return false;
  bool const q65 = separator == " : ";
  int const messageLength = q65 ? 28 : 22;
  if (line.size() < 31 + messageLength) return false;
  QString const message = line.mid(31, messageLength).simplified().toUpper();
  if (message.isEmpty()) return false;

  identity->q65 = q65;
  identity->utc = utc;
  identity->frequencyHz = 1000 * khz + df;
  identity->message = message;
  return true;
}

bool Map65DecodeDisplayFilter::matches(Identity const& left, Identity const& right)
{
  return left.q65 == right.q65 && left.utc == right.utc
      && left.message == right.message
      && qAbs(left.frequencyHz - right.frequencyHz) <= duplicateFrequencyToleranceHz;
}
