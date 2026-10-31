#ifndef DISPLAYTEXT_H
#define DISPLAYTEXT_H

#include <QTextBrowser>
#include <QByteArray>

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

signals:
  void selectCallsign(bool ctrl);
  void decodeLineClicked(QByteArray decodeRow, DecodeClickGesture gesture);

public slots:

protected:
  void mousePressEvent(QMouseEvent *e) override;
  void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
  bool hitTestDecodeLine(QPoint const& pos, QString& callsign, QByteArray& decodeRow) const;

  DecodeClickCoalescer m_decodeClickCoalescer;
};

#endif // DISPLAYTEXT_H
