// -*- Mode: C++ -*-
#ifndef JTTY_MESSAGES_HPP
#define JTTY_MESSAGES_HPP

#include <algorithm>
#include <cmath>
#include <string>

#include <QDateTime>
#include <QRegularExpression>
#include <QString>
#include <QTime>
#include <QVector>

#include "lib/jtty/JttyTransmit.hpp"

namespace Jtty
{
  // A content-free "keep transmitting" pattern (ordinary TEXT5-encoded text, nothing protocol-special) used to bridge a typing pause without PTT dropping/re-keying; '<' isn't used in normal chat text, so it's recognized and suppressed wherever JTTY text is displayed or logged.
  inline QString const jttyFillerText =
    QStringLiteral ("<<<<< <<<<<");

  inline bool isJttyFillerText (QString const& text)
  {
    QString const trimmed = text.trimmed ();
    if (trimmed.isEmpty ()) return false;
    for (QChar const c : trimmed) {
      if (c != QLatin1Char ('<') && c != QLatin1Char (' ')) return false;
    }
    return true;
  }

  // Collapses embedded filler runs (any internal spacing) to a single space, without trimming the string's own leading/trailing edge. For a growing-message delta fragment (e.g. ReceiveResultController's N1MM echo), a leading/trailing space is the word boundary against already-echoed text, not something filler-adjacent to discard.
  inline QString collapseJttyFillerRuns (QString text)
  {
    static QRegularExpression const pattern (
      QStringLiteral ("\\s*(?:<{5}\\s*)+"));
    text.replace (pattern, QStringLiteral (" "));
    return text;
  }

  // Filler travels gaplessly inside the same growing message as the real content around it, so it shows up embedded mid-string rather than as an isolated update; strips one-or-more 5-char '<' blocks (any internal spacing) and collapses the surrounding whitespace to a single space.
  inline QString stripJttyFillerText (QString text)
  {
    return collapseJttyFillerRuns (text).simplified ();
  }

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
  };

  struct NativeMacroContext
  {
    QString myCall;
    QString hisCall;
    int serialNumber {};
    NativeExchangeProfile exchangeProfile {NativeExchangeProfile::None};
    QString configuredExchange;
    QString grid;
    int snr {-10};  // SNR of received signal, for %R; stubbed until the decoder reports it

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

  // The transmit encoder's strings are the UTF-16 units of the GUI's.
  inline std::u16string toEncoder (QString const& text)
  {
    return text.toStdU16String ();
  }

  // Not QString::fromStdU16String, which reads a leading U+FEFF or U+FFFE as a byte order mark.
  inline QString fromEncoder (std::u16string const& text)
  {
    return QString (reinterpret_cast<QChar const *> (text.data ()), static_cast<int> (text.size ()));
  }

  inline Encoder::NativeMacroContext toEncoder (NativeMacroContext const& context)
  {
    Encoder::NativeMacroContext result;
    result.myCall = toEncoder (context.myCall);
    result.hisCall = toEncoder (context.hisCall);
    result.serialNumber = context.serialNumber;
    result.exchangeProfile = context.exchangeProfile;
    result.configuredExchange = toEncoder (context.configuredExchange);
    result.grid = toEncoder (context.grid);
    result.snr = context.snr;
    return result;
  }

  inline PreparedTransmitText prepareTransmitText (QString const& message)
  {
    auto const prepared = Encoder::prepareTransmitText (toEncoder (message));
    return {fromEncoder (prepared.text), prepared.substituted};
  }

  struct TransmitTextSegment
  {
    int offset {0};
    int length {0};
    QString text;
  };

  inline TransmitTextSegment nextTransmitTextSegment (
      QString const& message, int offset, int maximumLength = maxTransmitLength)
  {
    auto const segment = Encoder::nextTransmitTextSegment (toEncoder (message), offset, maximumLength);
    return {segment.offset, segment.length, fromEncoder (segment.text)};
  }

  inline QString transmitFrame (QString const& message)
  {
    return fromEncoder (Encoder::transmitFrame (toEncoder (message)));
  }

  // Number of receive samples in one complete JTTY frame: 59 symbols of 384
  // samples (lib/jtty/jtty.f90). Anything shorter is not a decodable interval.
  inline constexpr qint32 jttyFrameSamples = 59 * 384;

  // m_k0 starts at this sentinel and is only replaced once fastSink has real
  // audio, so it marks "nothing captured yet".
  inline constexpr qint32 invalidCaptureSamples = 9999999;

  // True when k0 describes a captured JTTY receive buffer worth saving: at
  // least one full frame, and not the uninitialised sentinel.
  inline bool wavCaptureValid (qint32 k0)
  {
    return k0 > jttyFrameSamples && k0 < invalidCaptureSamples;
  }

  inline QString formatSerialNumber (int serialNumber)
  {
    return fromEncoder (Encoder::formatSerialNumber (serialNumber));
  }

  // The native form each function key sends.
  inline QString nativeMacroTemplate (int functionKey)
  {
    using Form = Encoder::NativeForm;
    static Form const forms[] {
      Form::Cq,
      Form::CallExchange,
      Form::CallTuCq,
      Form::MyCall,
      Form::HisCall,
      Form::TuNowExchange,
      Form::CallAgn,
      Form::Exchange
    };
    if (functionKey < 1 || functionKey > 8) return {};
    return fromEncoder (Encoder::nativeFormTemplate (forms[functionKey - 1]));
  }

  inline QString legacyNativeMacroTemplate (int functionKey)
  {
    using Form = Encoder::NativeForm;
    static Form const forms[] {
      Form::Cq,
      Form::CallSerial,
      Form::CallTuCq,
      Form::MyCall,
      Form::HisCall,
      Form::TuNowSerial,
      Form::CallAgn,
      Form::Serial
    };
    if (functionKey < 1 || functionKey > 8) return {};
    return fromEncoder (Encoder::nativeFormTemplate (forms[functionKey - 1]));
  }

  // Chat/QSO-style templates for JTTY's "FT8 style" message preset (as opposed
  // to the contest-exchange-oriented templates above). F7/F8 are left blank.
  inline QString ft8StyleMacroTemplate (int functionKey)
  {
    static QString const templates[] {
      QStringLiteral ("%H %M %G"),
      QStringLiteral ("%H %M %R"),
      QStringLiteral ("%H %M R%R"),
      QStringLiteral ("%H %M RRR"),
      QStringLiteral ("%H %M 73"),
      QStringLiteral ("CQ %M %G"),
      QString {},
      QString {}
    };
    if (functionKey < 1 || functionKey > 8) return {};
    return templates[functionKey - 1];
  }

  enum class MessageStyle : qint8 { Contest = 0, Ft8 = 1 };

  // QSettings key for one function key's template under the given style, so
  // Contest- and FT8-style edits persist independently of each other.
  inline QString messageStyleSettingsKey (MessageStyle style, int functionKey)
  {
    return style == MessageStyle::Ft8
      ? QStringLiteral ("JTTY_FT8_msg%1").arg (functionKey)
      : QStringLiteral ("JTTY_msg%1").arg (functionKey);
  }

  inline QString messageStyleDefaultTemplate (MessageStyle style, int functionKey)
  {
    return style == MessageStyle::Ft8
      ? ft8StyleMacroTemplate (functionKey) : nativeMacroTemplate (functionKey);
  }

  inline QString formatSnr (int snr)
  {
    return fromEncoder (Encoder::formatSnr (snr));
  }

  inline QString expandLiteralMacro (QString const& macroTemplate,
                                     NativeMacroContext const& context)
  {
    return fromEncoder (Encoder::expandLiteralMacro (toEncoder (macroTemplate), toEncoder (context)));
  }

  inline QString migratedNativeMacroTemplate (int functionKey, QString const& savedValue)
  {
    if ((functionKey == 2 || functionKey == 6 || functionKey == 8)
        && savedValue == legacyNativeMacroTemplate (functionKey)) {
      return nativeMacroTemplate (functionKey);
    }
    return savedValue;
  }

  inline bool isNativeCall (QString const& rawCall)
  {
    return Encoder::isNativeCall (toEncoder (rawCall));
  }

  inline NativeAtomDescriptor nativeCallAtom (CallAction action, QString const& rawCall)
  {
    return Encoder::nativeCallAtom (action, toEncoder (rawCall));
  }

  inline NativeAtomDescriptor nativeLocationAtom (ExchangeRole role, LocationKind kind,
                                                   QString const& token)
  {
    return Encoder::nativeLocationAtom (role, kind, toEncoder (token));
  }

  inline NativeAtomDescriptor nativeControlAtom (int phraseId)
  {
    return Encoder::nativeControlAtom (phraseId);
  }

  inline NativeAtomDescriptor nativeGridAtom (ExchangeRole role, QString const& grid)
  {
    return Encoder::nativeGridAtom (role, toEncoder (grid));
  }

  inline bool isDecimal (QString const& value)
  {
    return Encoder::isDecimal (toEncoder (value));
  }

  inline QString normalizedFieldDayExchange (QString const& value)
  {
    return fromEncoder (Encoder::normalizedFieldDayExchange (toEncoder (value)));
  }

  inline NativeMacroContext nativeMacroContext (QString const& myCall, QString const& hisCall,
                                                int serialNumber, QString const& grid, int snr,
                                                NativeExchangeProfile profile,
                                                QString const& exchange)
  {
    auto const context = Encoder::nativeMacroContext (toEncoder (myCall), toEncoder (hisCall),
                                                      serialNumber, toEncoder (grid), snr, profile,
                                                      toEncoder (exchange));
    NativeMacroContext result {fromEncoder (context.myCall), fromEncoder (context.hisCall),
                               context.serialNumber, context.exchangeProfile,
                               fromEncoder (context.configuredExchange), fromEncoder (context.grid)};
    result.snr = context.snr;
    return result;
  }

  inline bool isCanonicalBase36Token (QString const& value)
  {
    return Encoder::isCanonicalBase36Token (toEncoder (value));
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
    return fromEncoder (Encoder::controlPhrase (phraseId));
  }

  inline int controlPhraseId (QString const& macroTemplate)
  {
    return Encoder::controlPhraseId (toEncoder (macroTemplate));
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
    auto const exchange = Encoder::nativeExchange (toEncoder (context));
    return {exchange.valid, exchange.atom, fromEncoder (exchange.text), fromEncoder (exchange.error)};
  }

  inline NativeMacroCompilation compileNativeMacro (
      QString const& macroTemplate, NativeMacroContext const& context)
  {
    auto const compiled = Encoder::compileNativeMacro (toEncoder (macroTemplate), toEncoder (context));
    NativeMacroCompilation result;
    result.status = compiled.status;
    for (auto const& atom : compiled.atoms) result.atoms.append (atom);
    result.text = fromEncoder (compiled.text);
    result.error = fromEncoder (compiled.error);
    return result;
  }

  inline NativeMacroCompilation compileNativeMacro (
      int functionKey, QString const& macroTemplate, NativeMacroContext const& context)
  {
    (void) functionKey;
    return compileNativeMacro (macroTemplate, context);
  }

  // Resolve a JTTY message start from the WAV anchor, preserving the raw-time fallback for test-generated files.
  inline QDateTime jttyLineStartTimeUtc (QDateTime const& diskDateTime,
                                         qint32 utcDiskRaw, float tsyncSeconds)
  {
    QDateTime anchor = diskDateTime;
    if (!anchor.isValid ()) {
      QTime const t = QTime::fromString (
        QString {"%1"}.arg (utcDiskRaw, 6, 10, QLatin1Char {'0'}), "hhmmss");
      if (!t.isValid ()) return {};
      anchor = QDateTime {QDate {2000, 1, 1}, t, Qt::UTC};
    }
    return anchor.addMSecs (qRound64 (1000.0 * tsyncSeconds)).toUTC ();
  }

  inline QString jttyLineTimeLabel (QDateTime const& timestampUtc)
  {
    return timestampUtc.isValid ()
      ? timestampUtc.toUTC ().toString ("hhmmss") : QString {};
  }

  // "Include Time" label; falls back to utcDiskRaw as a bare time-of-day when diskDateTime doesn't parse (e.g. sjtty's dummy-date filenames).
  inline QString jttyLineTimeLabel (QDateTime const& diskDateTime,
                                     qint32 utcDiskRaw, float tsyncSeconds)
  {
    return jttyLineTimeLabel (jttyLineStartTimeUtc (diskDateTime, utcDiskRaw,
                                                    tsyncSeconds));
  }

  // Matches a decode line's freq/SNR column layout with the SNR field left
  // blank (a sent message has no measured SNR), so Tx and Rx lines line up.
  inline QString formatJttyTxLine (float frequency, QString const& message)
  {
    QString const frequencyText = QStringLiteral ("%1").arg (qRound (frequency), 4);
    return frequencyText + QStringLiteral ("      ") + message;
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
    if (separator == 0) return result;

    bool frequencyOk {false};
    int const frequency = trimmed.left (separator).toInt (&frequencyOk);
    if (!frequencyOk) return result;

    result.frequency = frequency;
    result.message = separator < trimmed.size ()
        ? trimmed.mid (separator).trimmed () : QString {};
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
      for (int i = maxWidth; i > 0; --i) {
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

  // Format one JTTY decode line for display/logging: "Freq SNR  Message" (a
  // leading UTC time label, when wanted, is prepended by the caller). SNR is
  // plain right-justified, no zero-pad/forced sign -- matches the existing
  // decode-pane convention used by other modes (e.g. FT8's own line format,
  // lib/decoder_callbacks.f90, "i4" for its SNR field), deliberately
  // different from formatSnr's zero-padded convention for the %R Tx macro.
  inline QString formatJttyDecodeLine (float frequency, int snr, QString const& message)
  {
    QString const frequencyText = QStringLiteral ("%1").arg (qRound (frequency), 4);
    QString const snrText = QStringLiteral ("%1").arg (snr, 3);
    return message.isEmpty ()
      ? frequencyText
      : frequencyText + QStringLiteral (" ") + snrText + QStringLiteral ("  ") + message;
  }

  struct DecodeLineChange
  {
    bool messageChanged {false};
    bool extendsMessage {false};
    bool startsMessage {false};
    QString appendedText;
  };

  inline DecodeLineChange compareMessages (QString const& previous,
                                            QString const& current)
  {
    DecodeLineChange change;
    change.messageChanged = previous != current;
    change.extendsMessage = change.messageChanged
        && current.startsWith (previous);
    change.startsMessage = previous.isEmpty () && !current.isEmpty ();
    if (change.extendsMessage) {
      change.appendedText = current.mid (previous.size ());
    }
    return change;
  }

  struct MessageUpdate
  {
    qint64 messageId {0};
    float frequency {0.f};
    QString text;
    float sequenceStart {0.f};
    bool complete {false};
    int snr {-10};
  };

  inline bool shouldApplyToQsoHistory (bool alreadyPresent, float frequency,
                                       float rxFrequency, float tolerance)
  {
    // Later frames retain their admitted message identity despite decoder frequency drift.
    return alreadyPresent || std::abs (frequency - rxFrequency) < tolerance;
  }

  template<typename HistoryLine, typename Factory>
  bool mergeMessageUpdates (QVector<HistoryLine>& history,
                            QVector<MessageUpdate> const& updates,
                            Factory makeLine)
  {
    if (updates.isEmpty ()) return false;

    bool changed {false};
    bool orderChanged {false};
    for (auto const& update : updates) {
      auto const known = std::find_if (history.begin (), history.end (),
                                      [&update] (HistoryLine const& line) {
                                        return line.messageId == update.messageId;
                                      });
      if (known == history.end ()) {
        history.append (makeLine (update));
        changed = true;
        orderChanged = true;
      } else {
        bool const complete = known->complete || update.complete;
        if (known->text != update.text || known->frequency != update.frequency
            || known->complete != complete) {
          changed = true;
        }
        known->text = update.text;
        known->frequency = update.frequency;
        known->complete = complete;
      }
    }

    if (orderChanged) {
      std::sort (history.begin (), history.end (), [] (HistoryLine const& lhs,
                                                       HistoryLine const& rhs) {
        if (lhs.sequenceStart != rhs.sequenceStart) {
          return lhs.sequenceStart < rhs.sequenceStart;
        }
        return lhs.messageId < rhs.messageId;
      });
    }
    return changed;
  }

  inline bool mergeMessageUpdates (QVector<MessageUpdate>& history,
                                   QVector<MessageUpdate> const& updates)
  {
    return mergeMessageUpdates (history, updates,
                                [] (MessageUpdate const& update) { return update; });
  }
}

#endif
