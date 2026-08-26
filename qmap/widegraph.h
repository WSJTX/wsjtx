#ifndef WIDEGRAPH_H
#define WIDEGRAPH_H

#include <QDialog>
#include <QList>
#include "decode_label.h"

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

  void   addDecodeLabel(QMapDecodeRecord const& record);

  qint32 m_qsoFreq;

signals:
  void freezeDecode2(int n);
  void f11f12(int n);
  void decodeLabelClicked2(QByteArray decodeRow, bool doubleClick);
  // Emitted once per average cycle, right after the Horizontal Waterfall's
  // own top-row draw(), so VertWaterfall can show exactly the same
  // spectral data instead of an independently-decimated copy of it.
  void spectrumReady(const float swide[], int n, double startFreqKHz, double fSpanKHz,
                     int plotZero, int plotGain);

public slots:
  void wideFreezeDecode(int n);
  void wideDecodeLabelClicked(QByteArray decodeRow, bool doubleClick);

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

  QList<QMapDecodeLabel> m_decodeLabels;
};

#endif // WIDEGRAPH_H
