// -*- Mode: C++ -*-
#ifndef JTTY_MESSAGES_HPP
#define JTTY_MESSAGES_HPP

#include <algorithm>
#include <cstddef>
#include <type_traits>

#include <QByteArray>
#include <QDateTime>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTime>
#include <QVector>

namespace Jtty
{
  struct ParsedDecodeLine
  {
    int frequency {0};
    QString message;
    bool valid {false};
  };

  struct PreparedTransmitText
  {
    QString text;
    bool substituted {false};
    bool truncated {false};

    bool changed () const
    {
      return substituted || truncated;
    }
  };

  enum class NativeAtomKind : qint8
  {
    Call = 0,
    ExchangeNumber = 1,
    ExchangeLocation = 2,
    ExchangePair = 3,
    ExchangeNumberTime = 4,
    Control = 5,
    Grid4 = 6,
    Text5 = 7
  };

  enum class CallAction : qint8
  {
    Cq = 0,
    Call = 1,
    TuCq = 2,
    CallTu = 3,
    CallAgn = 4,
    TuNowCall = 5
  };

  enum class ExchangeRole : qint8
  {
    FieldOnly = 0,
    Full = 1
  };

  enum class NumberKind : qint8
  {
    Serial = 0,
    CqZone = 1,
    ItuZone = 2,
    Age = 3,
    Power = 4,
    Check = 5,
    FirstLicenseYear = 6,
    Generic = 7
  };

  enum class LocationKind : qint8
  {
    StateProvince = 0,
    ArrlRacSection = 1,
    CountryPrefix = 2,
    Qth = 3,
    LocalAdministrativeCode = 4
  };

  enum class PairSchema : qint8
  {
    ZoneLocation = 0,
    ClassSection = 1
  };

  enum class NativeExchangeProfile
  {
    None,
    FieldDay,
    RttyRoundup
  };

  struct NativeAtomDescriptor
  {
    qint8 kind {};
    qint8 subtype {};
    qint8 role {};
    qint8 reserved {};
    qint32 value {};
    char text[9] {};
  };

  static_assert (std::is_standard_layout<NativeAtomDescriptor>::value,
                 "NativeAtomDescriptor must remain C-compatible");
  static_assert (offsetof (NativeAtomDescriptor, kind) == 0, "atom ABI mismatch");
  static_assert (offsetof (NativeAtomDescriptor, subtype) == 1, "atom ABI mismatch");
  static_assert (offsetof (NativeAtomDescriptor, role) == 2, "atom ABI mismatch");
  static_assert (offsetof (NativeAtomDescriptor, reserved) == 3, "atom ABI mismatch");
  static_assert (offsetof (NativeAtomDescriptor, value) == 4, "atom ABI mismatch");
  static_assert (offsetof (NativeAtomDescriptor, text) == 8, "atom ABI mismatch");
  static_assert (sizeof (NativeAtomDescriptor) == 20, "atom ABI mismatch");

  enum class NativeEncodeStatus
  {
    Ok = 0,
    InvalidDescriptor = 1,
    UnknownSection = 2
  };

  enum class NativeMacroStatus
  {
    Native,
    LiteralFallback,
    InvalidRuntime
  };

  struct NativeMacroContext
  {
    QString myCall;
    QString hisCall;
    int serialNumber {};
    NativeExchangeProfile exchangeProfile {NativeExchangeProfile::None};
    QString configuredExchange;
    QString grid;

    NativeMacroContext () = default;

    NativeMacroContext (QString const& myCallValue, QString const& hisCallValue,
                        int serialNumberValue,
                        NativeExchangeProfile profile = NativeExchangeProfile::None,
                        QString const& exchangeValue = {}, QString const& gridValue = {})
      : myCall {myCallValue}
      , hisCall {hisCallValue}
      , serialNumber {serialNumberValue}
      , exchangeProfile {profile}
      , configuredExchange {exchangeValue}
      , grid {gridValue}
    {
    }
  };

  struct NativeMacroCompilation
  {
    NativeMacroStatus status {NativeMacroStatus::LiteralFallback};
    QVector<NativeAtomDescriptor> atoms;
    QString text;
    QString error;

    bool isNative () const
    {
      return status == NativeMacroStatus::Native;
    }
  };

  inline QString sourceAlphabet ()
  {
    // Keep in sync with ALPHABET in lib/jtty/jtty_source_codec.f90.
    return QStringLiteral ("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ +-./?!\"#$%,&*()_'=[]{}<>|:;");
  }

  inline bool isSourceCharacter (QChar c)
  {
    return sourceAlphabet ().contains (c)
        || (c >= QLatin1Char {'a'} && c <= QLatin1Char {'z'});
  }

  // Fixed width of a JTTY transmit frame; genjtty_ (lib/jtty/genjtty.f90)
  // expects exactly this many characters.
  inline constexpr int maxTransmitLength = 80;

  inline PreparedTransmitText prepareTransmitText (QString const& message)
  {
    PreparedTransmitText result;
    int const maxLength {maxTransmitLength};
    result.truncated = message.size () > maxLength;
    QString const bounded = message.left (maxLength);
    result.text.reserve (bounded.size ());

    for (QChar c : bounded) {
      if (c == QChar::Null || c == QLatin1Char {'~'}) {
        result.text.append (QLatin1Char {' '});
        result.substituted = true;
      } else if (isSourceCharacter (c)) {
        result.text.append (c);
      } else {
        result.text.append (QLatin1Char {'#'});
        result.substituted = true;
      }
    }

    return result;
  }

  // A message being queued behind one still transmitting (FIFO chaining,
  // see MainWindow::execute_jtty_tx) gets a leading space inserted, since
  // the user is unlikely to remember (or want) to type one themselves,
  // and unlikely to hit Return mid-word. Re-bounds to maxTransmitLength
  // in case the message was already at prepareTransmitText's limit.
  // No-op (returns message unchanged) when isChained is false.
  inline QString withChainedSpacing (QString const& message, bool isChained)
  {
    if (!isChained) return message;
    return (QLatin1Char {' '} + message).left (maxTransmitLength);
  }

  inline QString formatSerialNumber (int serialNumber)
  {
    return QString {"%1"}.arg (serialNumber, 3, 10, QLatin1Char {'0'});
  }

  inline QString nativeMacroTemplate (int functionKey)
  {
    static QString const templates[] {
      QStringLiteral ("CQ %M CQ"),
      QStringLiteral ("%H %E"),
      QStringLiteral ("%H TU CQ %M CQ"),
      QStringLiteral ("%M"),
      QStringLiteral ("%H"),
      QStringLiteral ("TU NOW %Q %E"),
      QStringLiteral ("%H AGN?"),
      QStringLiteral ("%E")
    };
    if (functionKey < 1 || functionKey > 8) return {};
    return templates[functionKey - 1];
  }

  inline QString legacyNativeMacroTemplate (int functionKey)
  {
    static QString const templates[] {
      QStringLiteral ("CQ %M CQ"),
      QStringLiteral ("%H 599 %N"),
      QStringLiteral ("%H TU CQ %M CQ"),
      QStringLiteral ("%M"),
      QStringLiteral ("%H"),
      QStringLiteral ("TU NOW %Q 599 %N"),
      QStringLiteral ("%H AGN?"),
      QStringLiteral ("599 %N")
    };
    if (functionKey < 1 || functionKey > 8) return {};
    return templates[functionKey - 1];
  }

  inline QString nativeGridMacroTemplate (int functionKey)
  {
    switch (functionKey) {
    case 2: return QStringLiteral ("%H %G");
    case 6: return QStringLiteral ("TU NOW %Q %G");
    case 8: return QStringLiteral ("%G");
    default: return {};
    }
  }

  inline QString normalizedMacroTemplate (QString const& macroTemplate)
  {
    return macroTemplate.simplified ().toUpper ();
  }

  inline int nativeMacroFunctionKey (QString const& macroTemplate)
  {
    QString const normalized = normalizedMacroTemplate (macroTemplate);
    if (normalized == QStringLiteral ("599 %G")) return 8;
    for (int functionKey = 1; functionKey <= 8; ++functionKey) {
      QString const gridTemplate = nativeGridMacroTemplate (functionKey);
      if (normalized == nativeMacroTemplate (functionKey)
          || macroTemplate == legacyNativeMacroTemplate (functionKey)
          || (!gridTemplate.isEmpty () && normalized == gridTemplate)) {
        return functionKey;
      }
    }
    return 0;
  }

  inline bool isNativeMacroTemplate (QString const& macroTemplate)
  {
    return nativeMacroFunctionKey (macroTemplate) != 0;
  }

  inline QString nativeExchangeFieldText (NativeMacroContext const& context)
  {
    QString const configured = context.configuredExchange.simplified ().toUpper ();
    if (context.exchangeProfile == NativeExchangeProfile::None
        || (context.exchangeProfile == NativeExchangeProfile::RttyRoundup
            && (configured == QStringLiteral ("DX") || configured == QStringLiteral ("#")))) {
      return formatSerialNumber (context.serialNumber);
    }
    return configured;
  }

  inline QString expandLiteralMacro (QString macroTemplate,
                                     NativeMacroContext const& context)
  {
    macroTemplate.replace (QStringLiteral ("%M"), context.myCall);
    macroTemplate.replace (QStringLiteral ("%H"), context.hisCall);
    macroTemplate.replace (QStringLiteral ("%Q"), context.hisCall);
    macroTemplate.replace (QStringLiteral ("%N"), formatSerialNumber (context.serialNumber));
    macroTemplate.replace (QStringLiteral ("%E"), nativeExchangeFieldText (context));
    macroTemplate.replace (QStringLiteral ("%G"), context.grid);
    return macroTemplate;
  }

  inline QString migratedNativeMacroTemplate (int functionKey, QString const& savedValue)
  {
    if ((functionKey == 2 || functionKey == 6 || functionKey == 8)
        && savedValue == legacyNativeMacroTemplate (functionKey)) {
      return nativeMacroTemplate (functionKey);
    }
    return savedValue;
  }

  inline QString normalizedNativeCall (QString const& call)
  {
    return call.trimmed ().toUpper ();
  }

  inline bool isNativeCall (QString const& rawCall)
  {
    QString const call = normalizedNativeCall (rawCall);
    if (call.size () < 3 || call.size () > 6 || call.startsWith (QLatin1Char {'Q'})) {
      return false;
    }

    int area = -1;
    for (int i = call.size () - 1; i >= 1; --i) {
      QChar const c = call.at (i);
      if (c >= QLatin1Char {'0'} && c <= QLatin1Char {'9'}) {
        area = i;
        break;
      }
    }
    if (area != 1 && area != 2) return false;

    bool prefixHasLetter {false};
    for (int i = 0; i < area; ++i) {
      QChar const c = call.at (i);
      bool const isLetter = c >= QLatin1Char {'A'} && c <= QLatin1Char {'Z'};
      bool const isDigit = c >= QLatin1Char {'0'} && c <= QLatin1Char {'9'};
      if (!isLetter && !isDigit) return false;
      prefixHasLetter = prefixHasLetter || isLetter;
    }
    if (!prefixHasLetter || area == call.size () - 1) return false;

    for (int i = area + 1; i < call.size (); ++i) {
      QChar const c = call.at (i);
      if (c < QLatin1Char {'A'} || c > QLatin1Char {'Z'}) return false;
    }
    return call.size () - area - 1 <= 3;
  }

  inline NativeAtomDescriptor nativeCallAtom (CallAction action, QString const& rawCall)
  {
    NativeAtomDescriptor atom;
    atom.kind = static_cast<qint8> (NativeAtomKind::Call);
    atom.subtype = static_cast<qint8> (action);
    QByteArray const call = normalizedNativeCall (rawCall).toLatin1 ();
    std::copy_n (call.cbegin (), std::min (call.size (), 8), atom.text);
    return atom;
  }

  inline NativeAtomDescriptor nativeSerialAtom (int serialNumber)
  {
    NativeAtomDescriptor atom;
    atom.kind = static_cast<qint8> (NativeAtomKind::ExchangeNumber);
    atom.subtype = static_cast<qint8> (NumberKind::Serial);
    atom.role = static_cast<qint8> (ExchangeRole::Full);
    atom.value = serialNumber;
    return atom;
  }

  inline void setNativeAtomText (NativeAtomDescriptor& atom, QString const& value)
  {
    QByteArray const text = value.toLatin1 ();
    std::copy_n (text.cbegin (), std::min (text.size (), 8), atom.text);
  }

  inline NativeAtomDescriptor nativeLocationAtom (ExchangeRole role, LocationKind kind,
                                                   QString const& token)
  {
    NativeAtomDescriptor atom;
    atom.kind = static_cast<qint8> (NativeAtomKind::ExchangeLocation);
    atom.subtype = static_cast<qint8> (kind);
    atom.role = static_cast<qint8> (role);
    setNativeAtomText (atom, token);
    return atom;
  }

  inline NativeAtomDescriptor nativeClassSectionAtom (int count, QChar classLetter,
                                                       QString const& section)
  {
    NativeAtomDescriptor atom;
    atom.kind = static_cast<qint8> (NativeAtomKind::ExchangePair);
    atom.subtype = static_cast<qint8> (PairSchema::ClassSection);
    atom.role = static_cast<qint8> (classLetter.unicode () - QLatin1Char {'A'}.unicode ());
    atom.value = count;
    setNativeAtomText (atom, section);
    return atom;
  }

  inline NativeAtomDescriptor nativeControlAtom (int phraseId)
  {
    NativeAtomDescriptor atom;
    atom.kind = static_cast<qint8> (NativeAtomKind::Control);
    atom.subtype = static_cast<qint8> (phraseId);
    return atom;
  }

  inline NativeAtomDescriptor nativeGridAtom (ExchangeRole role, QString const& grid)
  {
    NativeAtomDescriptor atom;
    atom.kind = static_cast<qint8> (NativeAtomKind::Grid4);
    atom.role = static_cast<qint8> (role);
    setNativeAtomText (atom, grid);
    return atom;
  }

  inline bool isDecimal (QString const& value)
  {
    if (value.isEmpty ()) return false;
    for (QChar const c : value) {
      if (c < QLatin1Char {'0'} || c > QLatin1Char {'9'}) return false;
    }
    return true;
  }

  inline QString normalizedFieldDayExchange (QString const& value)
  {
    QString exchange = value.simplified ().toUpper ();
    if (exchange.contains (QLatin1Char {' '})) return exchange;

    int classPosition {0};
    while (classPosition < exchange.size ()
           && exchange.at (classPosition) >= QLatin1Char {'0'}
           && exchange.at (classPosition) <= QLatin1Char {'9'}) {
      ++classPosition;
    }
    if (classPosition > 0 && classPosition + 1 < exchange.size ()
        && exchange.at (classPosition) >= QLatin1Char {'A'}
        && exchange.at (classPosition) <= QLatin1Char {'F'}) {
      exchange.insert (classPosition + 1, QLatin1Char {' '});
    }
    return exchange;
  }

  inline bool isCanonicalBase36Token (QString const& value)
  {
    if (value.size () != 2 && value.size () != 3) return false;
    if (value.size () == 3 && value.startsWith (QLatin1Char {'0'})) return false;
    for (QChar const c : value) {
      bool const digit = c >= QLatin1Char {'0'} && c <= QLatin1Char {'9'};
      bool const letter = c >= QLatin1Char {'A'} && c <= QLatin1Char {'Z'};
      if (!digit && !letter) return false;
    }
    return true;
  }

  inline bool isGrid4 (QString const& value)
  {
    if (value.size () != 4) return false;
    return value.at (0) >= QLatin1Char {'A'} && value.at (0) <= QLatin1Char {'R'}
        && value.at (1) >= QLatin1Char {'A'} && value.at (1) <= QLatin1Char {'R'}
        && value.at (2) >= QLatin1Char {'0'} && value.at (2) <= QLatin1Char {'9'}
        && value.at (3) >= QLatin1Char {'0'} && value.at (3) <= QLatin1Char {'9'};
  }

  inline QString controlPhrase (int phraseId)
  {
    static QString const phrases[] {
      QStringLiteral ("AGN?"),
      QStringLiteral ("CALL?"),
      QStringLiteral ("AGN CALL"),
      QStringLiteral ("NR?"),
      QStringLiteral ("AGN NR"),
      QStringLiteral ("EXCH?"),
      QStringLiteral ("STATE?"),
      QStringLiteral ("SECTION?"),
      QStringLiteral ("ZONE?"),
      QStringLiteral ("GRID?"),
      QStringLiteral ("RPRT?"),
      QStringLiteral ("QSL TU"),
      QStringLiteral ("TU"),
      QStringLiteral ("QRZ?"),
      QStringLiteral ("QSO B4"),
      QStringLiteral ("WAIT"),
      QStringLiteral ("NIL?"),
      QStringLiteral ("OK?")
    };
    if (phraseId < 0 || phraseId >= 18) return {};
    return phrases[phraseId];
  }

  inline int controlPhraseId (QString const& macroTemplate)
  {
    QString const normalized = normalizedMacroTemplate (macroTemplate);
    for (int phraseId = 0; phraseId < 18; ++phraseId) {
      if (normalized == controlPhrase (phraseId)) return phraseId;
    }
    return -1;
  }

  struct NativeExchangeCompilation
  {
    bool valid {false};
    NativeAtomDescriptor atom;
    QString text;
    QString error;
  };

  inline NativeExchangeCompilation nativeExchange (NativeMacroContext const& context)
  {
    NativeExchangeCompilation result;
    auto invalid = [&result] (QString const& error) {
      result.error = error;
      return result;
    };
    auto serial = [&result] (int value) {
      if (value < 0 || value >= (1 << 17)) return false;
      result.valid = true;
      result.atom = nativeSerialAtom (value);
      result.text = QStringLiteral ("599 %1").arg (formatSerialNumber (value));
      return true;
    };

    QString const configured = context.configuredExchange.simplified ().toUpper ();
    switch (context.exchangeProfile) {
    case NativeExchangeProfile::None:
      if (!serial (context.serialNumber)) {
        return invalid (QStringLiteral ("Serial number must be between 0 and 131071"));
      }
      return result;

    case NativeExchangeProfile::FieldDay: {
      QStringList const fields = configured.split (QLatin1Char {' '});
      if (fields.size () != 2 || fields.at (0).size () < 2) {
        return invalid (QStringLiteral ("Field Day exchange must be COUNTCLASS SECTION"));
      }
      QString const countClass = fields.at (0);
      QChar const classLetter = countClass.back ();
      QString const countText = countClass.left (countClass.size () - 1);
      bool countOk {false};
      int const count = countText.toInt (&countOk);
      if (!countOk || !isDecimal (countText) || count < 1 || count > 32) {
        return invalid (QStringLiteral ("Field Day transmitter count must be between 1 and 32"));
      }
      if (classLetter < QLatin1Char {'A'} || classLetter > QLatin1Char {'F'}) {
        return invalid (QStringLiteral ("Field Day class must be A through F"));
      }
      QString const section = fields.at (1);
      if (!isCanonicalBase36Token (section)) {
        return invalid (QStringLiteral ("Field Day section must be a canonical 2-3 character code"));
      }
      result.valid = true;
      result.atom = nativeClassSectionAtom (count, classLetter, section);
      result.text = QStringLiteral ("%1%2 %3").arg (count).arg (classLetter).arg (section);
      return result;
    }

    case NativeExchangeProfile::RttyRoundup:
      if (configured == QStringLiteral ("DX") || configured == QStringLiteral ("#")) {
        if (!serial (context.serialNumber)) {
          return invalid (QStringLiteral ("Serial number must be between 0 and 131071"));
        }
        return result;
      }
      if (isDecimal (configured)) {
        bool valueOk {false};
        int const value = configured.toInt (&valueOk);
        if (!valueOk || !serial (value)) {
          return invalid (QStringLiteral ("Configured serial must be between 0 and 131071"));
        }
        return result;
      }
      if (!isCanonicalBase36Token (configured)) {
        return invalid (QStringLiteral (
          "RTTY exchange must be DX, #, a decimal serial, or a canonical 2-3 character state/province"));
      }
      result.valid = true;
      result.atom = nativeLocationAtom (
        ExchangeRole::Full, LocationKind::StateProvince, configured);
      result.text = QStringLiteral ("599 %1").arg (configured);
      return result;
    }

    return invalid (QStringLiteral ("Unsupported exchange profile"));
  }

  inline NativeExchangeCompilation nativeGridExchange (NativeMacroContext const& context,
                                                         ExchangeRole role)
  {
    NativeExchangeCompilation result;
    QString const locator = context.grid.trimmed ().toUpper ();
    static QRegularExpression const locatorPattern {
      QStringLiteral ("^[A-R]{2}[0-9]{2}(?:[A-X]{2}(?:[0-9]{2})?)?$")};
    if (!locatorPattern.match (locator).hasMatch ()) {
      result.error = QStringLiteral ("Grid must be a valid four-, six-, or eight-character Maidenhead locator");
      return result;
    }
    QString const grid = locator.left (4);
    result.valid = true;
    result.atom = nativeGridAtom (role, grid);
    result.text = role == ExchangeRole::Full
      ? QStringLiteral ("599 %1").arg (grid) : grid;
    return result;
  }

  inline NativeMacroCompilation compileNativeMacro (
      QString const& macroTemplate, NativeMacroContext const& context)
  {
    NativeMacroCompilation result;
    QString const normalized = normalizedMacroTemplate (macroTemplate);
    int const phraseId = controlPhraseId (normalized);
    if (phraseId >= 0) {
      result.status = NativeMacroStatus::Native;
      result.atoms.append (nativeControlAtom (phraseId));
      result.text = controlPhrase (phraseId);
      return result;
    }

    int const functionKey = nativeMacroFunctionKey (macroTemplate);
    if (functionKey == 0) {
      result.text = expandLiteralMacro (macroTemplate, context);
      return result;
    }

    QString const myCall = normalizedNativeCall (context.myCall);
    QString const hisCall = normalizedNativeCall (context.hisCall);
    auto invalid = [&result] (QString const& error) {
      result.status = NativeMacroStatus::InvalidRuntime;
      result.atoms.clear ();
      result.text.clear ();
      result.error = error;
      return result;
    };
    auto appendCall = [&result] (CallAction action, QString const& call) {
      result.atoms.append (nativeCallAtom (action, call));
    };
    auto appendSerial = [&result, &context] {
      result.atoms.append (nativeSerialAtom (context.serialNumber));
    };
    auto appendExchange = [&result, &context, &normalized] {
      NativeExchangeCompilation exchange;
      if (normalized.contains (QStringLiteral ("%G"))) {
        ExchangeRole const role = normalized == QStringLiteral ("599 %G")
          ? ExchangeRole::Full : ExchangeRole::FieldOnly;
        exchange = nativeGridExchange (context, role);
      } else {
        exchange = nativeExchange (context);
      }
      if (exchange.valid) result.atoms.append (exchange.atom);
      return exchange;
    };

    switch (functionKey) {
    case 1:
      if (!isNativeCall (myCall)) return invalid (QStringLiteral ("Configured callsign is not a native Call8 callsign"));
      appendCall (CallAction::Cq, myCall);
      result.text = QStringLiteral ("CQ %1 CQ").arg (myCall);
      break;
    case 2:
      if (!isNativeCall (hisCall)) return invalid (QStringLiteral ("DX callsign is not a native Call8 callsign"));
      appendCall (CallAction::Call, hisCall);
      if (macroTemplate == legacyNativeMacroTemplate (2)) {
        if (context.serialNumber < 0 || context.serialNumber >= (1 << 17)) {
          return invalid (QStringLiteral ("Serial number must be between 0 and 131071"));
        }
        appendSerial ();
        result.text = QStringLiteral ("%1 599 %2").arg (
            hisCall, formatSerialNumber (context.serialNumber));
      } else {
        auto const exchange = appendExchange ();
        if (!exchange.valid) return invalid (exchange.error);
        result.text = QStringLiteral ("%1 %2").arg (hisCall, exchange.text);
      }
      break;
    case 3:
      if (!isNativeCall (hisCall)) return invalid (QStringLiteral ("DX callsign is not a native Call8 callsign"));
      if (!isNativeCall (myCall)) return invalid (QStringLiteral ("Configured callsign is not a native Call8 callsign"));
      appendCall (CallAction::CallTu, hisCall);
      appendCall (CallAction::Cq, myCall);
      result.text = QStringLiteral ("%1 TU CQ %2 CQ").arg (hisCall, myCall);
      break;
    case 4:
      if (!isNativeCall (myCall)) return invalid (QStringLiteral ("Configured callsign is not a native Call8 callsign"));
      appendCall (CallAction::Call, myCall);
      result.text = myCall;
      break;
    case 5:
      if (!isNativeCall (hisCall)) return invalid (QStringLiteral ("DX callsign is not a native Call8 callsign"));
      appendCall (CallAction::Call, hisCall);
      result.text = hisCall;
      break;
    case 6:
      if (!isNativeCall (hisCall)) return invalid (QStringLiteral ("Queued callsign is not a native Call8 callsign"));
      appendCall (CallAction::TuNowCall, hisCall);
      if (macroTemplate == legacyNativeMacroTemplate (6)) {
        if (context.serialNumber < 0 || context.serialNumber >= (1 << 17)) {
          return invalid (QStringLiteral ("Serial number must be between 0 and 131071"));
        }
        appendSerial ();
        result.text = QStringLiteral ("TU NOW %1 599 %2").arg (
            hisCall, formatSerialNumber (context.serialNumber));
      } else {
        auto const exchange = appendExchange ();
        if (!exchange.valid) return invalid (exchange.error);
        result.text = QStringLiteral ("TU NOW %1 %2").arg (hisCall, exchange.text);
      }
      break;
    case 7:
      if (!isNativeCall (hisCall)) return invalid (QStringLiteral ("DX callsign is not a native Call8 callsign"));
      appendCall (CallAction::CallAgn, hisCall);
      result.text = QStringLiteral ("%1 AGN?").arg (hisCall);
      break;
    case 8:
      if (macroTemplate == legacyNativeMacroTemplate (8)) {
        if (context.serialNumber < 0 || context.serialNumber >= (1 << 17)) {
          return invalid (QStringLiteral ("Serial number must be between 0 and 131071"));
        }
        appendSerial ();
        result.text = QStringLiteral ("599 %1").arg (formatSerialNumber (context.serialNumber));
      } else {
        auto const exchange = appendExchange ();
        if (!exchange.valid) return invalid (exchange.error);
        result.text = exchange.text;
      }
      break;
    default:
      return invalid (QStringLiteral ("Unsupported native macro"));
    }

    result.status = NativeMacroStatus::Native;
    return result;
  }

  inline NativeMacroCompilation compileNativeMacro (
      int functionKey, QString const& macroTemplate, NativeMacroContext const& context)
  {
    (void) functionKey;
    return compileNativeMacro (macroTemplate, context);
  }

  // "Include Time" label; falls back to utcDiskRaw as a bare time-of-day when diskDateTime doesn't parse (e.g. sjtty's dummy-date filenames).
  inline QString jttyLineTimeLabel (QDateTime const& diskDateTime,
                                     qint32 utcDiskRaw, float tsyncSeconds)
  {
    QDateTime anchor = diskDateTime;
    if (!anchor.isValid ()) {
      QTime const t = QTime::fromString (
        QString {"%1"}.arg (utcDiskRaw, 6, 10, QLatin1Char {'0'}), "hhmmss");
      if (!t.isValid ()) return {};
      anchor = QDateTime {QDate {2000, 1, 1}, t, Qt::UTC};
    }
    return anchor.addMSecs (qRound64 (1000.0 * tsyncSeconds)).toUTC ().toString ("hhmmss");
  }

  inline ParsedDecodeLine parseDecodeLine (QString const& line)
  {
    ParsedDecodeLine result;
    QString const trimmed = line.trimmed ();
    result.message = trimmed;

    int separator = 0;
    while (separator < trimmed.size () && !trimmed.at (separator).isSpace ()) {
      ++separator;
    }
    if (separator == 0 || separator == trimmed.size ()) return result;

    bool frequencyOk {false};
    int const frequency = trimmed.left (separator).toInt (&frequencyOk);
    if (!frequencyOk) return result;

    result.frequency = frequency;
    result.message = trimmed.mid (separator).trimmed ();
    result.valid = true;
    return result;
  }

  inline constexpr int maxDisplayWidth = 40;

  inline QString wrapMessage (QString const& text, int maxWidth = maxDisplayWidth)
  {
    if (text.size () <= maxWidth) return text;

    QString result;
    int start = 0;
    while (text.size () - start > maxWidth) {
      int breakAt = -1;
      for (int i = maxWidth; i >= 0; --i) {
        if (text.at (start + i) == QLatin1Char {' '}) {
          breakAt = i;
          break;
        }
      }
      if (breakAt < 0) {
        result += text.mid (start, maxWidth) + QLatin1String {"\n  "};
        start += maxWidth;
      } else {
        result += text.mid (start, breakAt) + QLatin1String {"\n  "};
        start += breakAt + 1;
      }
    }
    result += text.mid (start);
    return result;
  }

  inline QVector<int> decodeLineOrder (QVector<float> const& startTimes,
                                       QVector<int> const& slotIds)
  {
    int const count = qMin (startTimes.size (), slotIds.size ());
    QVector<int> order;
    order.reserve (count);
    for (int i = 0; i < count; ++i) order.append (i);
    std::stable_sort (order.begin (), order.end (),
                      [&startTimes, &slotIds] (int lhs, int rhs) {
      if (startTimes.at (lhs) != startTimes.at (rhs)) {
        return startTimes.at (lhs) < startTimes.at (rhs);
      }
      return slotIds.at (lhs) < slotIds.at (rhs);
    });
    return order;
  }
}

#endif
