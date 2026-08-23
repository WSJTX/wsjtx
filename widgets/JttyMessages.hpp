// -*- Mode: C++ -*-
#ifndef JTTY_MESSAGES_HPP
#define JTTY_MESSAGES_HPP

#include <QString>

namespace Jtty
{
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

  inline QString sourceAlphabet ()
  {
    // Keep in sync with JTTY_ALPHABET in lib/jtty/jtty_mod.f90.
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
}

#endif
