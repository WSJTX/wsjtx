#ifndef HEARD_BY_REPORT_HPP__
#define HEARD_BY_REPORT_HPP__

#include <QByteArray>
#include <QString>

//
// HeardByReport - one reception report of our signal.
//
struct HeardByReport
{
  QString receiver;
  QString locator;            // receiver's locator
  QString mode;               // as PSK Reporter names it
  quint64 frequency {0};      // Hz
  int snr {0};
  qint64 when {0};            // epoch seconds
  qint64 id {0};              // from receiver, time and frequency

  // The same report seen twice (a reconnect, say) gets the same id.
  static qint64 id_of (QString const& receiver, qint64 when, quint64 frequency)
  {
    // FNV-1a over the three values
    quint64 h = 1469598103934665603ULL;
    auto mix = [&h] (QByteArray const& bytes) {
      for (char c : bytes)
        {
          h ^= static_cast<quint8> (c);
          h *= 1099511628211ULL;
        }
    };
    mix (receiver.toUtf8 ());
    mix ("|" + QByteArray::number (when) + "|" + QByteArray::number (frequency));
    h &= (1ULL << 52) - 1;
    return h ? static_cast<qint64> (h) : 1;
  }
};

#endif
