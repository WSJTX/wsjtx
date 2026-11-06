#ifndef DISPLAYTEXT_H
#define DISPLAYTEXT_H

#include <QTextBrowser>
#include <QByteArray>
#include <QPoint>

#include "decode_click_coalescer.h"
#include "qmap_decode_record.h"

class DisplayText : public QTextBrowser
{
    Q_OBJECT
public:
    explicit DisplayText(QWidget *parent = 0);

    // Attach a decode to the most recently appended line, making that line
    // clickable in the same way as a callsign label on the waterfalls.
    void setLastLineDecode(QMapDecodeRecord const& record);

    // Lines stay clickable only as long as the matching waterfall label
    // would exist: older ones are pruned, and all are cleared together with
    // the waterfall labels (Monitor, opening a file) or by Erase.
    void pruneDecodeLines(int nowSeconds);
    void clearDecodeLines();

signals:
  void selectCallsign(bool ctrl);
  void decodeLineClicked(QByteArray decodeRow, DecodeClickGesture gesture);

public slots:

protected:
  void mousePressEvent(QMouseEvent *e) override;
  void mouseReleaseEvent(QMouseEvent *e) override;
  void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
  bool hitTestDecodeLine(QPoint const& pos, QString& callsign, QByteArray& decodeRow) const;

  DecodeClickCoalescer m_decodeClickCoalescer;
  int m_firstLiveBlock {0};     // blocks before this one carry no decode
  bool m_pressOnDecode {false}; // left press landed on a clickable line
  QPoint m_pressPos;
  QString m_pressCallsign;
  QByteArray m_pressDecodeRow;
};

#endif // DISPLAYTEXT_H
