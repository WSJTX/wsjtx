#ifndef QMAP_DECODE_CLICK_COALESCER_H
#define QMAP_DECODE_CLICK_COALESCER_H

#include <functional>

#include <QByteArray>
#include <QString>
#include <QTimer>

class QObject;

enum class DecodeClickGesture
{
  Press,
  SingleClick,
  DoubleClick,
};

class DecodeClickCoalescer
{
public:
  using Dispatch = std::function<void (QByteArray const&, DecodeClickGesture)>;

  DecodeClickCoalescer (QObject * context, Dispatch dispatch, int interval = -1);

  void press (QString const& identity, QByteArray const& decodeRow);
  void doubleClick (QString const& identity, QByteArray const& decodeRow);

private:
  void dispatchPending ();

  Dispatch dispatch_;
  QTimer timer_;
  QString pendingIdentity_;
  QByteArray pending_;
};

#endif // QMAP_DECODE_CLICK_COALESCER_H
