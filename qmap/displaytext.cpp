#include "displaytext.h"
#include <QDebug>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBlockUserData>
#include <QAbstractTextDocumentLayout>

namespace
{
  // Per-line payload: the raw 80-byte decoder row, exactly as the waterfall
  // labels hold it, so a click here reaches WSJT-X by the same route.
  class DecodeLineData final : public QTextBlockUserData
  {
  public:
    DecodeLineData (QString const& callsign, QByteArray const& decodeRow)
      : callsign_ {callsign}
      , decodeRow_ {decodeRow}
    {
    }

    QString callsign_;
    QByteArray decodeRow_;
  };
}

DisplayText::DisplayText(QWidget *parent) :
    QTextBrowser(parent),
    m_decodeClickCoalescer {this, [this] (QByteArray const& decodeRow, DecodeClickGesture gesture) {
      emit decodeLineClicked (decodeRow, gesture);
    }}
{
}

void DisplayText::setLastLineDecode(QMapDecodeRecord const& record)
{
  auto block = document ()->lastBlock ();
  if (block.isValid ()) {
    // The document takes ownership and deletes the data when the line goes.
    block.setUserData (new DecodeLineData {record.callsign, record.raw});
  }
}

bool DisplayText::hitTestDecodeLine(QPoint const& pos, QString& callsign, QByteArray& decodeRow) const
{
  auto const block = cursorForPosition (pos).block ();
  if (!block.isValid ()) return false;
  auto const * data = dynamic_cast<DecodeLineData const *> (block.userData ());
  if (!data) return false;     // separator or status line

  // cursorForPosition() snaps to the nearest line, so make sure the click
  // really landed on this line and not in the empty space below the text.
  auto const rect = document ()->documentLayout ()->blockBoundingRect (block)
    .translated (-horizontalScrollBar ()->value (), -verticalScrollBar ()->value ());
  if (pos.y () < rect.top () || pos.y () >= rect.bottom ()) return false;

  callsign = data->callsign_;
  decodeRow = data->decodeRow_;
  return true;
}

void DisplayText::mousePressEvent(QMouseEvent *e)
{
  if (e->button () == Qt::LeftButton) {
    QString callsign;
    QByteArray decodeRow;
    if (hitTestDecodeLine (e->pos (), callsign, decodeRow)) {
      m_decodeClickCoalescer.press (callsign, decodeRow);
    }
  }
  QTextBrowser::mousePressEvent(e);
}

void DisplayText::mouseDoubleClickEvent(QMouseEvent *e)
{
  bool ctrl = (e->modifiers() & 0x4000000);
  emit(selectCallsign(ctrl));
  if (e->button () == Qt::LeftButton) {
    QString callsign;
    QByteArray decodeRow;
    if (hitTestDecodeLine (e->pos (), callsign, decodeRow)) {
      m_decodeClickCoalescer.doubleClick (callsign, decodeRow);
    }
  }
  QTextBrowser::mouseDoubleClickEvent(e);
}
