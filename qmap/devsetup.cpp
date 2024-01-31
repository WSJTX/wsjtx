#include "devsetup.h"
#include "mainwindow.h"
#include <QTextStream>
#include <QDebug>
#include <cstdio>

//----------------------------------------------------------- DevSetup()
DevSetup::DevSetup(QWidget *parent) :	QDialog(parent)
{
  ui.setupUi(this);	//setup the dialog form
  m_restartSoundIn=false;
}

DevSetup::~DevSetup()
{
}

void DevSetup::initDlg()
{
  ui.myCallEntry->setText(m_myCall);
  ui.myGridEntry->setText(m_myGrid);
  ui.astroFont->setValue(m_astroFont);
  ui.myCallColor->addItems({"","red","green","cyan"});
  ui.myCallColor->setCurrentIndex(m_myCallColor);
  ui.saveDirEntry->setText(m_saveDir);
  ui.azelDirEntry->setText(m_azelDir);
  ui.fCalSpinBox->setValue(m_fCal);
  ui.faddEntry->setText(QString::number(m_fAdd,'f',3));
  ui.sbPort->setValue(m_udpPort);
  ui.sb_dB->setValue(m_dB);
}

//------------------------------------------------------- accept()
void DevSetup::accept()
{
  // Called when OK button is clicked.
  // Check to see whether SoundInThread must be restarted,
  // and save user parameters.

  m_myCall=ui.myCallEntry->text();
  m_myGrid=ui.myGridEntry->text();
  m_astroFont=ui.astroFont->value();
  m_myCallColor=ui.myCallColor->currentIndex();
  m_saveDir=ui.saveDirEntry->text();
  m_azelDir=ui.azelDirEntry->text();
  m_fCal=ui.fCalSpinBox->value();
  m_fAdd=ui.faddEntry->text().toDouble();
  m_udpPort=ui.sbPort->value();
  m_dB=ui.sb_dB->value();
  QDialog::accept();
}
