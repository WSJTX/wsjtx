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
  void   rx570();
  void   tx570();
  void   updateFreqLabel();
  void   enableSetRxHardware(bool b);

  qint32 m_qsoFreq;

signals:
  void freezeDecode2(int n);
  void f11f12(int n);

public slots:
  void wideFreezeDecode(int n);
  void initIQplus();

protected:
  virtual void keyPressEvent( QKeyEvent *e );
  void resizeEvent(QResizeEvent* event);

private slots:
  void on_waterfallAvgSpinBox_valueChanged(int arg1);
  void on_freqSpanSpinBox_valueChanged(int arg1);
  void on_freqOffsetSpinBox_valueChanged(int arg1);
  void on_zeroSpinBox_valueChanged(int arg1);
  void on_gainSpinBox_valueChanged(int arg1);
  void on_autoZeroPushButton_clicked();
  void on_cbFcenter_stateChanged(int arg1);
  void on_fCenterLineEdit_editingFinished();
  void on_pbSetRxHardware_clicked();
  void on_cbSpec2d_toggled(bool checked);
  void on_cbLockTxRx_stateChanged(int arg1);

private:
  Ui::WideGraph * ui;
  QString m_settings_filename;
public:
  bool   m_bForceCenterFreq;
private:
  bool   m_bLockTxRx;
public:
  qint32 m_mult570;
  qint32 m_mult570Tx;
  double m_dForceCenterFreq;
  double m_cal570;
  double m_TxOffset;
private:
  bool   m_bIQxt;
  qint32 m_waterfallAvg;
  qint32 m_fCal;
  qint32 m_fSample;
  qint32 m_mode65;
  qint32 m_TRperiod=60;
};

extern int set570(double freq_MHz);

#endif // WIDEGRAPH_H
