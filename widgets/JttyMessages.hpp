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

  inline PreparedTransmitText prepareTransmitText (QString const& message)
  {
    PreparedTransmitText result;
    int const maxLength {80};
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

  inline QString formatSerialNumber (int serialNumber)
  {
    return QString {"%1"}.arg (serialNumber, 3, 10, QLatin1Char {'0'});
  }
}

#endif
