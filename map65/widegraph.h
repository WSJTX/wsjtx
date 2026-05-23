#ifndef WIDEGRAPH_H
#define WIDEGRAPH_H

#include <QDialog>
#include <QList>
#include <QString>
#include <QTimer>

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
  void   rx570();
  void   tx570();
  void   updateFreqLabel();
  void   enableSetRxHardware(bool b);

  // Decoded-callsign overlay (N6NU 2026-05-12, port of QMAP feature).
  // mainwindow calls addDecodeLabel for each decoded line (after
  // parsing the freq + sender callsign out of the "!"-prefix line);
  // the list ages out after m_decodeLabelPeriods × TR period of no
  // refresh and gets pushed to the plotter for rendering.
  // mode_reliable=false: caller has no authoritative mode (e.g. "&"
  // bandmap line, where display.f90 writes no cmode). On dedup, the
  // existing label's is_jt65 is preserved. For brand-new labels the
  // caller's is_jt65 is used as a best-guess seed.
  void   addDecodeLabel(double freq_khz, const QString& callsign,
                        bool is_jt65, bool mode_reliable = true);
  void   clearDecodeLabels();
  bool   decodeLabelsEnabled() const { return m_decodeLabelsEnabled; }
  void   setDecodeLabelsEnabled(bool on);

  qint32 m_qsoFreq;

signals:
  void freezeDecode2(int n);
  void f11f12(int n);
  // Mirror toggle: WideGraph row checkbox ? MainWindow View menu.
  void   decodeLabelsEnabledChanged(bool on);

public slots:
  void wideFreezeDecode(int n);
  void initIQplus();

private slots:
  void ageDecodeLabels();

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

  // Decoded-callsign overlay state.
  QList<DecodeLabel> m_decodeLabels;
  bool   m_decodeLabelsEnabled {true};
  int    m_decodeLabelPeriods  {5};   // disappear after N×TRperiod of no decode
  QTimer m_ageTimer;
  static constexpr int kDecodeLabelMax = 200;
};

extern int set570(double freq_MHz);

#endif // WIDEGRAPH_H
