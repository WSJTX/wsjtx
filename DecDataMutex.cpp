#include "DecDataMutex.hpp"

#include <QMutex>

QMutex& dec_data_mutex()
{
  static QMutex mutex;
  return mutex;
}
