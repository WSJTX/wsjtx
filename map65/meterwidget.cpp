// Simple bargraph meter
// Implemented by Edson Pereira PY2SDR

#include "meterwidget.h"

MeterWidget::MeterWidget(QWidget *parent) :
    QWidget(parent),
    m_signal(0)
{
    for ( int i = 0; i < 10; i++ ) {
        signalQueue.enqueue(0);
    }
}

void MeterWidget::setValue(int value)
{
    m_signal = value;
    signalQueue.enqueue(value);
    signalQueue.dequeue();

    // Get signal peak
    int tmp = 0;
    for (int i = 0; i < signalQueue.size(); ++i) {
        if (signalQueue.at(i) > tmp)
            tmp = signalQueue.at(i);
    }
    m_sigPeak = tmp;

    update();
}

void MeterWidget::paintEvent( QPaintEvent * )
{
    int pos;
    QPainter p;

    p.begin(this);

    // Sanitize
    m_signal = m_signal < 0 ? 0 : m_signal;
    m_signal = m_signal > 60 ? 60 : m_signal;

    pos = m_signal * 2;
    QRect r(0, height() - pos, width(), pos );
    p.fillRect(r, QColor( 255, 150, 0 ));

    // Draw peak hold indicator
    p.setPen(Qt::black);
    pos = m_sigPeak * 2;
    p.drawLine(0, height() - pos, 10, height() - pos);
}
