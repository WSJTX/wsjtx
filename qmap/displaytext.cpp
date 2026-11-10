#include "displaytext.h"
#include <QApplication>
#include <QDebug>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBlockUserData>
#include <QAbstractTextDocumentLayout>

#include "decode_label.h"

namespace
{
  // Per-line payload: the raw 80-byte decoder row, exactly as the waterfall
  // labels hold it, so a click here reaches WSJT-X by the same route.
  class DecodeLineData final : public QTextBlockUserData
  {
  public:
    DecodeLineData (QString const& callsign, QByteArray const& decodeRow, int secondsSinceMidnight)
      : callsign_ {callsign}
      , decodeRow_ {decodeRow}
      , secondsSinceMidnight_ {secondsSinceMidnight}
    {
    }

    QString callsign_;
    QByteArray decodeRow_;
    int secondsSinceMidnight_;
  };

  // Same age test, including the midnight wrap, as pruneQMapDecodeLabels().
  bool decodeExpired (int decodeSeconds, int nowSeconds)
  {
    int delta = nowSeconds - decodeSeconds;
    if (delta < -43200) delta += 86400;
    else if (delta > 43200) delta -= 86400;
    return delta > QMapDecodeLabelLifetimeSecs;
  }
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
    block.setUserData (new DecodeLineData {record.callsign, record.raw,
                                           record.secondsSinceMidnight});
  }
}

void DisplayText::pruneDecodeLines(int nowSeconds)
{
  // Erase empties the document; start again from the top.
  if (m_firstLiveBlock >= document ()->blockCount ()) m_firstLiveBlock = 0;
  // Lines are appended in time order, so stop at the first one still live.
  for (auto block = document ()->findBlockByNumber (m_firstLiveBlock);
       block.isValid (); block = block.next ()) {
    if (auto const * data = dynamic_cast<DecodeLineData const *> (block.userData ())) {
      if (!decodeExpired (data->secondsSinceMidnight_, nowSeconds)) break;
      block.setUserData (nullptr);
    }
    m_firstLiveBlock = block.blockNumber () + 1;
  }
}

void DisplayText::clearDecodeLines()
{
  for (auto block = document ()->begin (); block.isValid (); block = block.next ()) {
    if (block.userData ()) block.setUserData (nullptr);
  }
  m_firstLiveBlock = 0;
  m_pressOnDecode = false;
}

bool DisplayText::hitTestDecodeLine(QPoint const& pos, QString& callsign, QByteArray& decodeRow) const
{
  auto const block = cursorForPosition (pos).block ();
  if (!block.isValid ()) return false;
  auto const * data = dynamic_cast<DecodeLineData const *> (block.userData ());
  if (!data) return false;     // separator, status line or expired decode

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
  // Only remember the press: a drag to select text must not reach WSJT-X,
  // so the click is dispatched on release if the mouse hasn't moved.
  m_pressOnDecode = e->button () == Qt::LeftButton
    && hitTestDecodeLine (e->pos (), m_pressCallsign, m_pressDecodeRow);
  m_pressPos = e->pos ();
  QTextBrowser::mousePressEvent(e);
}

void DisplayText::mouseReleaseEvent(QMouseEvent *e)
{
  if (e->button () == Qt::LeftButton && m_pressOnDecode
      && (e->pos () - m_pressPos).manhattanLength () < QApplication::startDragDistance ()) {
    m_decodeClickCoalescer.press (m_pressCallsign, m_pressDecodeRow);
  }
  if (e->button () == Qt::LeftButton) m_pressOnDecode = false;
  QTextBrowser::mouseReleaseEvent(e);
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
