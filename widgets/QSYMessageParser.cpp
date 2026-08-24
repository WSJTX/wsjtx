#include "widgets/QSYMessageParser.h"

#include <QChar>
#include <QStringList>
#include <QtGlobal>

namespace
{
  QStringList split_ws (QString const& s)
  {
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
    return s.split (' ', QString::SkipEmptyParts);
#else
    return s.split (' ', Qt::SkipEmptyParts);
#endif
  }

  bool all_digits (QString const& s)
  {
    if (s.isEmpty ()) return false;
    for (int i = 0; i < s.size (); ++i)
      if (!s.at (i).isDigit ()) return false;
    return true;
  }

  QString pad3 (int n)
  {
    return QString {"%1"}.arg (n, 3, 10, QLatin1Char ('0'));
  }

  struct ModeRow { char ch; char const* name; };
  ModeRow const kModes[] = {
    {'V', "SSB"},   {'W', "CW"},      {'L', "FT8"},     {'K', "MSK144"},
    {'J', "FT4"},   {'A', "JT9"},     {'B', "JT65"},    {'C', "FST4"},
    {'D', "Q65-30B"}, {'E', "Q65-60C"}, {'F', "Q65-60D"},
    {'G', "Q65-60E"}, {'H', "Q65-120D"},
  };

  bool mode_name (QChar m, QString * out)
  {
    if (m.isDigit ()) { if (out) *out = QStringLiteral ("FM"); return true; }
    if (!m.isLetter ()) return false;
    char const c = m.toLatin1 ();
    for (ModeRow const& r : kModes)
      if (r.ch == c) { if (out) *out = QString::fromLatin1 (r.name); return true; }
    return false;
  }

  char const* const kGeneral[] = {
    "You are in the log",
    "Please call me by phone",
    "Check your email",
    "You are not in Contest Mode",
    "Please check your PC Clock",
    "You are transmitting in the wrong time slot",
    "Please transmit above 1000 Hz",
    "Your transmitter is overmodulated",
    "Your audio is distorted",
    "You are transmitting on top of a rare DX station",
    "Please check HB9Q Logger",
    "Please check Ping Jockey",
    "Please check ON4KST",
    "Please QSL via LoTW",
  };
  int const kGeneralCount = int (sizeof kGeneral / sizeof kGeneral[0]); // 14

  bool is_vhf_subband (char b)
  {
    return b == 'A' || b == 'B' || b == 'C' || b == 'D' || b == 'E';
  }

  bool fm_digit_prefix (char band, QChar m, int region, QString * out)
  {
    int const d = m.digitValue ();
    if (d < 0) return false;
    int mhz;
    switch (band)
      {
      case 'A': if (d > 3) return false;            mhz = 50 + d;  break; // 50..53
      case 'B': if (d < 4 || d > 7) return false;   mhz = 140 + d; break; // 144..147
      case 'C':
        if (region == 2)
          {
            if (d < 2 || d > 3) return false;
            mhz = 220 + d;
          }
        else
          {
            if (d != 0) return false;
            mhz = 222;
          }
        break;
      case 'D': mhz = (region == 2 ? 440 : 430) + d; break;              // 0..9
      case 'E': mhz = 1290 + d;                      break;              // 0..9
      default:  return false;
      }
    if (out) *out = QString::number (mhz) + ".";
    return true;
  }

  bool simple_prefix (char band, int region, QString * out)
  {
    if (band == 'J') { if (out) *out = (region == 2 ? "24192." : "24048."); return true; }
    struct Row { char ch; char const* p; };
    static Row const simple[] = {
      {'F', "2304."}, {'G', "3400."}, {'H', "5760."}, {'I', "10368."},
      {'K', "903."},  {'X', "24048."},{'9', "902."},  {'4', "40."}, {'7', "70."},
      {'L', "0."},    {'M', "1."},    {'N', "3."},    {'O', "5."},  {'P', "7."},
      {'Q', "10."},   {'R', "14."},   {'S', "18."},   {'T', "21."}, {'U', "24."},
      {'V', "28."},   {'W', "29."},
    };
    for (Row const& r : simple)
      if (r.ch == band) { if (out) *out = QString::fromLatin1 (r.p); return true; }
    return false;
  }

  bool band_prefix (QChar band, QChar mode, int region, QString * out)
  {
    char const b = band.toLatin1 ();
    if (is_vhf_subband (b))
      {
        if (mode.isLetter ())   // letter (digital) mode -> base of the band
          {
            int const base = (b == 'A' ? 50 : b == 'B' ? 144 : b == 'C' ? 222
                              : b == 'D' ? 432 : 1296);
            if (out) *out = QString::number (base) + ".";
            return true;
          }
        return fm_digit_prefix (b, mode, region, out);
      }
    return simple_prefix (b, region, out);
  }

  QSYMessageParser::Message decode_reply (QString const& p)
  {
    QSYMessageParser::Message m;
    if (!p.startsWith (QLatin1Char ('$'))) return m;
    QStringList const f = split_ws (p);
    if (f.size () != 3) return m;
    if (f.at (0) != QLatin1String ("$")) return m;
    if (f.at (2) != QLatin1String ("OKQSY") && f.at (2) != QLatin1String ("NOQSY")) return m;
    m.type = QSYMessageParser::Type::Reply;
    m.call = f.at (1);
    m.response = f.at (2).left (2);
    return m;
  }

  QSYMessageParser::Message decode_general (QString const& p)
  {
    QSYMessageParser::Message m;
    if (p.size () != 5) return m;            // "ZA" + exactly 3 digits
    if (p.at (0) != QLatin1Char ('Z') || p.at (1) != QLatin1Char ('A')) return m;
    QString const digits = p.mid (2, 3);
    if (!all_digits (digits)) return m;
    int const code = digits.toInt ();
    if (code < 1 || code > kGeneralCount) return m;
    m.type = QSYMessageParser::Type::General;
    m.code = code;
    m.text = QString::fromLatin1 (kGeneral[code - 1]);
    return m;
  }

  QSYMessageParser::Message decode_frequency (QString const& p, int region)
  {
    QSYMessageParser::Message m;
    if (p.size () != 5) return m;            // band + mode + exactly 3 digits
    QChar const band = p.at (0);
    QChar const mode = p.at (1);
    QString const digits = p.mid (2, 3);
    if (!all_digits (digits)) return m;      // freq field must be numeric
    QString mode_str;
    if (!mode_name (mode, &mode_str)) return m;          // reject non-modes (I, R, ...)
    QString prefix;
    if (!band_prefix (band, mode, region, &prefix)) return m; // reject bad band / combo
    m.type = QSYMessageParser::Type::Frequency;
    m.band = band;
    m.mode_char = mode;
    m.kHz = digits.toInt ();
    m.frequency_mhz = prefix + digits;
    m.mode = mode_str;
    return m;
  }
}

namespace QSYMessageParser
{
  Message decode (QString const& payload, int region)
  {
    if (payload.startsWith (QLatin1Char ('$')))  return decode_reply (payload);
    if (payload.startsWith (QLatin1String ("ZA"))) return decode_general (payload);
    return decode_frequency (payload, region);
  }

  QString encode (Message const& m)
  {
    switch (m.type)
      {
      case Type::Frequency: return QString {m.band} + m.mode_char + pad3 (m.kHz);
      case Type::General:   return QStringLiteral ("ZA") + pad3 (m.code);
      case Type::Reply:
      case Type::Invalid:   break;
      }
    return QString {};
  }

  QString encodeFrequency (QChar band, QChar mode, int kHz, int region)
  {
    if (kHz < 0 || kHz > 999) return QString {};
    QString scratch;
    if (!mode_name (mode, &scratch)) return QString {};
    if (!band_prefix (band, mode, region, &scratch)) return QString {};
    return QString {band} + mode + pad3 (kHz);
  }

  QString encodeGeneral (int code)
  {
    if (code < 1 || code > kGeneralCount) return QString {};
    return QStringLiteral ("ZA") + pad3 (code);
  }

  QString encodeReplyWire (bool ok)
  {
    return ok ? QStringLiteral ("OKQSY") : QStringLiteral ("NOQSY");
  }

  bool mightContainMessage (QString const& line)
  {
    return line.contains (QLatin1Char ('.'));
  }

  LineResult decodeLine (QString const& line, int region)
  {
    LineResult result;
    for (QString const& token : split_ws (line))
      {
        int const dot = token.indexOf (QLatin1Char ('.'));
        if (dot <= 0 || dot >= token.size () - 1) continue;
        QString const payload = token.mid (dot + 1);
        Message const m = decode (payload, region);
        if (m)
          {
            result.call = token.left (dot);
            result.payload = payload;
            result.message = m;
            return result;
          }
      }
    return result;
  }
}
