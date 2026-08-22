#ifndef WIDEGRAPH_H
#define WIDEGRAPH_H

#include <QDialog>

namespace Ui {
  class WideGraph;
}

class WideGraph : public QDialog
{
  Q_OBJECT

public:
  explicit WideGraph (QString const& settings_filename, QWidget * parent = nullptr);
  ~WideGraph();

  void   dataSink2(float s[], int nkhz, int ihsym, int ndiskdata,
                   uchar lstrong[]);
  int    QSOfreq();
  int    nSpan();
  int    nStartFreq();
  float  fSpan();
  void   saveSettings();
  void   setDF(int n);
  int    DF();
  int    Tol();
  void   setTol(int n);
  void   setFcal(int n);
  void   setPalette(QString palette);
  void   setFsample(int n);
  void   setMode65(int n);
  void   setPeriod(int n);
  void   setDecodeFinished();
  double fGreen();
  void   updateFreqLabel();
  void   enableSetRxHardware(bool b);

  qint32 m_qsoFreq;

signals:
  void freezeDecode2(int n);
  void f11f12(int n);
  // Emitted once per average cycle, right after the wideband waterfall's
  // own top-row draw(), so VertWaterfall can show exactly the same
  // spectral data instead of an independently-decimated copy of it.
  void spectrumReady(const float swide[], int n, double startFreqKHz, double fSpanKHz,
                     int plotZero, int plotGain);

public slots:
  void wideFreezeDecode(int n);

protected:
  virtual void keyPressEvent( QKeyEvent *e );
  void resizeEvent(QResizeEvent* event);

private slots:
  void on_waterfallAvgSpinBox_valueChanged(int arg1);
  void on_zeroSpinBox_valueChanged(int arg1);
  void on_gainSpinBox_valueChanged(int arg1);
  void on_autoZeroPushButton_clicked();
  void on_cbSpec2d_toggled(bool checked);

private:
  Ui::WideGraph * ui;
  QString m_settings_filename;
  bool   m_bLockTxRx;
public:
  double m_TxOffset;
private:
  qint32 m_waterfallAvg;
  qint32 m_fCal;
  qint32 m_fSample;
  qint32 m_mode65;
  qint32 m_TRperiod=60;
};

#endif // WIDEGRAPH_H
