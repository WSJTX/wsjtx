// -*- Mode: C++ -*-
#ifndef JTTY_MESSAGES_HPP
#define JTTY_MESSAGES_HPP

#include <QString>

namespace Jtty
{
  inline QString formatSerialNumber (int serialNumber)
  {
    return QString {"%1"}.arg (serialNumber, 3, 10, QLatin1Char {'0'});
  }
}

#endif
