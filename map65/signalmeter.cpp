// Simple bargraph dB meter
// Implemented by Edson Pereira PY2SDR
//
// Limits and geometry are hardcded for now.

#include "signalmeter.h"

SignalMeter::SignalMeter(QWidget *parent) :
    QWidget(parent)
{
    resize(parent->size());

    m_meter = new MeterWidget(this);
    m_meter->setGeometry(10, 10, 10, 120);

    m_label = new QLabel(this);
    m_label->setGeometry(10, 135, 20, 20);

    QLabel *dbLabel = new QLabel(this);
    dbLabel->setText("dB");
    dbLabel->setGeometry(30, 135, 20, 20);
}

SignalMeter::~SignalMeter()
{

}

void SignalMeter::paintEvent( QPaintEvent * )
{
    QPainter p;
    p.begin(this);
    p.drawLine(22, 10, 22, 130);

    for ( int i = 0; i <= 60; i += 10 ) {
        p.drawLine(22, i*2 + 10, 25, i*2 + 10);
    }

    for ( int i = 10; i < 60; i += 10 ) {
        p.drawText(30, i*2 + 15, QString::number(60 - i));
    }
}

void SignalMeter::setValue(int value)
{
    m_meter->setValue(value);
    m_label->setText(QString::number(value));
}

void SignalMeter::resizeEvent(QResizeEvent *s)
{
    resize(s->size());
}
