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
  qint32  m_fCal;
  qint32  m_udpPort;
  qint32  m_astroFont;
  qint32  m_dB;

  double  m_fAdd;
  double  m_TxOffset;

  bool    m_network;
  bool    m_restartSoundIn;

  int     m_myCallColor;

  QString m_myCall;
  QString m_myGrid;
  QString m_saveDir;
  QString m_azelDir;

public slots:
  void accept();

private:
  int r,g,b,r0,g0,b0,r1,g1,b1,r2,g2,b2,r3,g3,b3;
  Ui::DialogSndCard ui;
};

#endif // DEVSETUP_H
