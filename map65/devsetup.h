#ifndef DEVSETUP_H
#define DEVSETUP_H

#include <QDialog>
#include "ui_devsetup.h"

class DevSetup : public QDialog
{
  Q_OBJECT
public:
  DevSetup(QWidget *parent=0);
  ~DevSetup();

  void initDlg();
  qint32  m_idInt;
  qint32  m_pttPort;
  qint32  m_nDevIn;
  qint32  m_nDevOut;
  qint32  m_inDevList[100];
  qint32  m_outDevList[100];
  qint32  m_paInDevice;
  qint32  m_paOutDevice;
  qint32  m_timeout;
  qint32  m_dPhi;
  qint32  m_fCal;
  qint32  m_udpPort;
  qint32  m_astroFont;
  qint32  m_mult570;
  qint32  m_mult570Tx;

  double  m_fAdd;
  double  m_cal570;
  double  m_TxOffset;

  bool    m_xpolx;
  bool    m_network;
  bool    m_fs96000;
  bool    m_xpol;
  bool    m_IQswap;
  bool    m_restartSoundIn;
  bool    m_restartSoundOut;
  bool    m_10db;
  bool    m_initIQplus;
  bool    m_bIQxt;

  QString m_myCall;
  QString m_myGrid;
  QString m_saveDir;
  QString m_azelDir;
  QString m_dxccPfx;
  QString m_colors;
  QString m_editorCommand;

  QColor  m_colorBackground;

public slots:
  void accept();

private slots:
  void on_soundCardRadioButton_toggled(bool checked);
  void on_cbXpol_stateChanged(int arg1);
  void on_cal570SpinBox_valueChanged(double ppm);
  void on_mult570SpinBox_valueChanged(int mult);
  void on_sbBackgroundRed_valueChanged(int arg1);
  void on_sbBackgroundGreen_valueChanged(int arg1);
  void on_sbBackgroundBlue_valueChanged(int arg1);
  void updateColorLabels(void);
  void on_sbRed0_valueChanged(int arg1);
  void on_sbGreen0_valueChanged(int arg1);
  void on_sbBlue0_valueChanged(int arg1);
  void on_sbRed1_valueChanged(int arg1);
  void on_sbGreen1_valueChanged(int arg1);
  void on_sbBlue1_valueChanged(int arg1);
  void on_sbRed2_valueChanged(int arg1);
  void on_sbGreen2_valueChanged(int arg1);
  void on_sbBlue2_valueChanged(int arg1);
  void on_sbRed3_valueChanged(int arg1);
  void on_sbGreen3_valueChanged(int arg1);
  void on_sbBlue3_valueChanged(int arg1);
  void on_pushButton_5_clicked();
  void on_mult570TxSpinBox_valueChanged(int arg1);
  void on_rbIQXT_toggled(bool checked);  
  void on_sbTxOffset_valueChanged(double f);

private:
  int r,g,b,r0,g0,b0,r1,g1,b1,r2,g2,b2,r3,g3,b3;
  Ui::DialogSndCard ui;
};

#endif // DEVSETUP_H
