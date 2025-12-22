#ifndef METERWIDGET_H
#define METERWIDGET_H

#include <QWidget>
#include <QtGui>
#include <QQueue>

class MeterWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MeterWidget(QWidget *parent = 0);
    
signals:
    
public slots:
    void setValue(int value);

private:
    QQueue<int> signalQueue;

    int m_signal;
    int m_sigPeak;

protected:
    void paintEvent( QPaintEvent * );
    
};

#endif // METERWIDGET_H
