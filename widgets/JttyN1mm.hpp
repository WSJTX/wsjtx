// -*- Mode: C++ -*-
#ifndef JTTY_N1MM_HPP
#define JTTY_N1MM_HPP

#include <QString>
#include <QStringList>
#include <QVector>

#include "JttyMessages.hpp"

namespace Jtty
{
  enum class N1mmCompileStatus
  {
    Literal,
    Native,
    Error
  };

  struct N1mmCompilation
  {
    N1mmCompileStatus status {N1mmCompileStatus::Literal};
    QVector<NativeAtomDescriptor> atoms;
    QString literalText;
    QString canonicalText;
    QString error;

    bool isNative () const { return status == N1mmCompileStatus::Native; }
  };

  namespace N1mmDetail
  {
    inline N1mmCompilation failure (QString const& reason)
    {
      N1mmCompilation result;
      result.status = N1mmCompileStatus::Error;
      result.error = reason;
      return result;
    }

    inline bool appendCall (QVector<NativeAtomDescriptor>& atoms, CallAction action,
                            QString const& call, QString& error)
    {
      if (!isNativeCall (call)) {
        error = QStringLiteral ("invalid callsign in tagged payload");
        return false;
      }
      atoms.append (nativeCallAtom (action, call));
      return true;
    }

    inline bool parseSerial (QString const& payload, int& serial)
    {
      if (!isDecimal (payload)) return false;
      bool ok {false};
      serial = payload.toInt (&ok);
      return ok && serial >= 0 && serial < (1 << 17);
    }

    inline NativeExchangeCompilation expandedExchange (
        QString const& payload, NativeExchangeProfile profile)
    {
      NativeMacroContext context;
      context.exchangeProfile = profile;
      context.configuredExchange = payload;

      int serial {0};
      if (profile == NativeExchangeProfile::None) {
        if (!parseSerial (payload, serial)) {
          NativeExchangeCompilation result;
          result.error = QStringLiteral (
              "serial exchange must be a decimal value between 0 and 131071");
          return result;
        }
        context.serialNumber = serial;
      } else if (profile == NativeExchangeProfile::RttyRoundup) {
        if (parseSerial (payload, serial)) {
          context.serialNumber = serial;
        } else if (isCanonicalBase36Token (payload)) {
          NativeExchangeCompilation result;
          result.valid = true;
          result.atom = nativeLocationAtom (
              ExchangeRole::Full, LocationKind::StateProvince, payload);
          result.text = QStringLiteral ("599 %1").arg (payload);
          return result;
        } else {
          NativeExchangeCompilation result;
          result.error = QStringLiteral (
              "tagged RTTY exchange must contain a decimal serial or state/province");
          return result;
        }
      }
      return nativeExchange (context);
    }
  }

  // A tag is recognized only at the first non-space character. Its payload is
  // the value text already expanded by N1MM, never N1MM macro syntax.
  inline N1mmCompilation compileN1mmMessage (
      QString const& message, NativeExchangeProfile profile = NativeExchangeProfile::None)
  {
    int first {0};
    while (first < message.size () && message.at (first).isSpace ()) ++first;
    QString const marker = QStringLiteral ("[[JTTY:");
    if (message.mid (first, marker.size ()).compare (marker, Qt::CaseInsensitive) != 0) {
      QString const tagStem = QStringLiteral ("[[JTTY");
      if (message.mid (first, tagStem.size ()).compare (
            tagStem, Qt::CaseInsensitive) == 0) {
        return N1mmDetail::failure (QStringLiteral ("malformed JTTY action tag"));
      }
      N1mmCompilation result;
      result.literalText = message;
      return result;
    }

    int const close = message.indexOf (QStringLiteral ("]]"), first + marker.size ());
    if (close < 0) return N1mmDetail::failure (QStringLiteral ("malformed JTTY action tag"));
    QString const action = message.mid (first + marker.size (),
                                        close - first - marker.size ()).toUpper ();
    if (action.isEmpty () || action.contains (QLatin1Char {'['})
        || action.contains (QLatin1Char {']'}) || action.contains (QLatin1Char {' '})
        || action.contains (QLatin1Char {'\t'})) {
      return N1mmDetail::failure (QStringLiteral ("malformed JTTY action tag"));
    }

    static QStringList const actions {
      QStringLiteral ("CQ"), QStringLiteral ("CALL_EXCH"),
      QStringLiteral ("CALL_TU_CQ"), QStringLiteral ("MYCALL"),
      QStringLiteral ("HISCALL"), QStringLiteral ("TU_NOW_EXCH"),
      QStringLiteral ("CALL_MY"), QStringLiteral ("CALL_TU_MY"),
      QStringLiteral ("EXCH"), QStringLiteral ("GRID"), QStringLiteral ("CONTROL")
    };
    if (!actions.contains (action)) {
      return N1mmDetail::failure (QStringLiteral ("unknown JTTY action: %1").arg (action));
    }

    QString const payload = message.mid (close + 2).simplified ().toUpper ();
    if (payload.isEmpty ()) return N1mmDetail::failure (QStringLiteral ("tagged payload is empty"));
    QStringList const fields = payload.split (QLatin1Char {' '}, Qt::SkipEmptyParts);
    N1mmCompilation result;
    result.status = N1mmCompileStatus::Native;
    QString error;

    if (action == QStringLiteral ("CQ")) {
      if (fields.size () != 1
          || !N1mmDetail::appendCall (result.atoms, CallAction::Cq,
                                      fields.value (0), error)) {
        if (error.isEmpty ()) error = QStringLiteral ("CQ payload must contain one callsign");
      } else {
        result.canonicalText = QStringLiteral ("CQ %1 CQ").arg (fields.first ());
      }
    } else if (action == QStringLiteral ("CALL_EXCH")
               || action == QStringLiteral ("TU_NOW_EXCH")) {
      if (fields.size () < 2) {
        error = QStringLiteral ("call/exchange payload is incomplete");
      } else {
        QString const call = fields.first ();
        CallAction const callAction = action == QStringLiteral ("CALL_EXCH")
            ? CallAction::Call : CallAction::TuNowCall;
        if (N1mmDetail::appendCall (result.atoms, callAction, call, error)) {
          auto const exchange = N1mmDetail::expandedExchange (
              fields.mid (1).join (QLatin1Char {' '}), profile);
          if (!exchange.valid) {
            error = exchange.error;
            result.atoms.clear ();
          } else {
            result.atoms.append (exchange.atom);
            result.canonicalText = action == QStringLiteral ("CALL_EXCH")
                ? call + QLatin1Char {' '} + exchange.text
                : QStringLiteral ("TU NOW %1 %2").arg (call, exchange.text);
          }
        }
      }
    } else if (action == QStringLiteral ("CALL_TU_CQ")) {
      if (fields.size () != 2
          || !N1mmDetail::appendCall (result.atoms, CallAction::CallTu,
                                      fields.value (0), error)
          || !N1mmDetail::appendCall (result.atoms, CallAction::Cq,
                                      fields.value (1), error)) {
        if (error.isEmpty ()) {
          error = QStringLiteral ("CALL_TU_CQ payload must contain two callsigns");
        }
      } else {
        result.canonicalText = QStringLiteral ("%1 TU CQ %2 CQ").arg (
            fields.at (0), fields.at (1));
      }
    } else if (action == QStringLiteral ("MYCALL") || action == QStringLiteral ("HISCALL")) {
      if (fields.size () != 1
          || !N1mmDetail::appendCall (result.atoms, CallAction::Call,
                                      fields.value (0), error)) {
        if (error.isEmpty ()) error = QStringLiteral ("call payload must contain one callsign");
      } else {
        result.canonicalText = fields.first ();
      }
    } else if (action == QStringLiteral ("CALL_MY")
               || action == QStringLiteral ("CALL_TU_MY")) {
      bool const withTu = action == QStringLiteral ("CALL_TU_MY");
      if (fields.size () != 2
          || !N1mmDetail::appendCall (result.atoms,
                                      withTu ? CallAction::CallTu : CallAction::Call,
                                      fields.value (0), error)
          || !N1mmDetail::appendCall (result.atoms, CallAction::Call,
                                      fields.value (1), error)) {
        if (error.isEmpty ()) error = QStringLiteral ("two-call payload must contain two callsigns");
      } else {
        result.canonicalText = withTu
            ? QStringLiteral ("%1 TU %2").arg (fields.at (0), fields.at (1))
            : QStringLiteral ("%1 %2").arg (fields.at (0), fields.at (1));
      }
    } else if (action == QStringLiteral ("EXCH")) {
      auto const exchange = N1mmDetail::expandedExchange (payload, profile);
      if (!exchange.valid) {
        error = exchange.error;
      } else {
        result.atoms.append (exchange.atom);
        result.canonicalText = exchange.text;
      }
    } else if (action == QStringLiteral ("GRID")) {
      if (fields.size () != 1 || !isGrid4 (fields.first ())) {
        error = QStringLiteral ("GRID payload must contain one valid four-character grid");
      } else {
        result.atoms.append (nativeGridAtom (ExchangeRole::FieldOnly, fields.first ()));
        result.canonicalText = fields.first ();
      }
    } else if (action == QStringLiteral ("CONTROL")) {
      int const phraseId = controlPhraseId (payload);
      if (phraseId < 0) {
        error = QStringLiteral ("unknown CONTROL phrase");
      } else {
        result.atoms.append (nativeControlAtom (phraseId));
        result.canonicalText = controlPhrase (phraseId);
      }
    }

    if (!error.isEmpty ()) return N1mmDetail::failure (error);
    if (result.atoms.isEmpty ()) return N1mmDetail::failure (QStringLiteral ("tag produced no atoms"));
    return result;
  }

  inline N1mmCompilation compileN1mmMessage (
      QString const& message, NativeMacroContext const& context)
  {
    return compileN1mmMessage (message, context.exchangeProfile);
  }
}

#endif
