#include "decode_click_coalescer.h"

#include <utility>

#include <QApplication>
#include <QObject>

DecodeClickCoalescer::DecodeClickCoalescer (QObject * context, Dispatch dispatch, int interval)
  : dispatch_ {std::move (dispatch)}
{
  timer_.setSingleShot (true);
  timer_.setTimerType (Qt::PreciseTimer);
  timer_.setInterval (interval >= 0 ? interval : QApplication::doubleClickInterval ());
  QObject::connect (&timer_, &QTimer::timeout, context, [this] { dispatchPending (); });
}

void DecodeClickCoalescer::press (QString const& identity, QByteArray const& decodeRow)
{
  // Qt reports the first press before deciding whether the gesture is a double-click.
  if (timer_.isActive () && pendingIdentity_ != identity) {
    timer_.stop ();
    dispatchPending ();
  }
  dispatch_ (decodeRow, DecodeClickGesture::Press);
  pendingIdentity_ = identity;
  pending_ = decodeRow;
  timer_.start ();
}

void DecodeClickCoalescer::doubleClick (QString const& identity, QByteArray const& decodeRow)
{
  if (timer_.isActive ()) {
    timer_.stop ();
    if (pendingIdentity_ != identity) dispatchPending ();
    else {
      pendingIdentity_.clear ();
      pending_.clear ();
    }
  }
  dispatch_ (decodeRow, DecodeClickGesture::DoubleClick);
}

void DecodeClickCoalescer::dispatchPending ()
{
  if (pending_.isEmpty ()) return;
  auto const decodeRow = pending_;
  pendingIdentity_.clear ();
  pending_.clear ();
  dispatch_ (decodeRow, DecodeClickGesture::SingleClick);
}
