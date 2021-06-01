#include "txtune.h"
#include "ui_txtune.h"
#include <QDebug>

extern int txPower;
extern int iqAmp;
extern int iqPhase;
extern bool bTune;

TxTune::TxTune(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::TxTune)
{
    ui->setupUi(this);
}

TxTune::~TxTune()
{
    delete ui;
}

void TxTune::accept()
{
  if(bTune) on_pbTune_clicked();
  QDialog::accept();
}

void TxTune::reject()
{
  if(bTune) on_pbTune_clicked();
  set_iqAmp(m_saveAmp);
  set_iqPhase(m_savePha);
  set_txPower(m_saveTxPower);
  QDialog::reject();
}

void TxTune::on_pwrSlider_valueChanged(int n)
{
  txPower=n;
  ui->labPower->setText (QString {"%1 %"}.arg (n));
}

void TxTune::on_ampSlider_valueChanged(int n)
{
  m_iqAmp1=n;
  iqAmp=10*m_iqAmp1 + m_iqAmp2;
  ui->labAmpReal->setText (QString::number (1.0 + 0.0001 * iqAmp, 'f', 4));
}

void TxTune::on_fineAmpSlider_valueChanged(int n)
{
  m_iqAmp2=n;
  iqAmp=10*m_iqAmp1 + m_iqAmp2;
  ui->labAmpReal->setText (QString::number (1.0 + 0.0001*iqAmp, 'f', 4));
}

void TxTune::on_phaSlider_valueChanged(int n)
{
  m_iqPha1=n;
  iqPhase=10*m_iqPha1 + m_iqPha2;
  ui->labPhaReal->setText (QString::number (0.01*iqPhase, 'f', 2));
}

void TxTune::on_finePhaSlider_valueChanged(int n)
{
  m_iqPha2=n;
  iqPhase=10*m_iqPha1 + m_iqPha2;
  ui->labPhaReal->setText (QString::number (0.01*iqPhase, 'f', 2));
}

void TxTune::set_iqAmp(int n)
{
  m_saveAmp=n;
  m_iqAmp1=n/10;
  m_iqAmp2=n%10;
  ui->ampSlider->setValue(m_iqAmp1);
  ui->fineAmpSlider->setValue(m_iqAmp2);
}

void TxTune::set_iqPhase(int n)
{
  m_savePha=n;
  m_iqPha1=n/10;
  m_iqPha2=n%10;
  ui->phaSlider->setValue(m_iqPha1);
  ui->finePhaSlider->setValue(m_iqPha2);
}

void TxTune::set_txPower(int n)
{
  m_saveTxPower=n;
  ui->pwrSlider->setValue(n);
}

void TxTune::on_cbTxImage_toggled(bool b)
{
  ui->ampSlider->setEnabled(b);
  ui->fineAmpSlider->setEnabled(b);
  ui->labAmp->setEnabled(b);
  ui->labFineAmp->setEnabled(b);
  ui->phaSlider->setEnabled(b);
  ui->finePhaSlider->setEnabled(b);
  ui->labPha->setEnabled(b);
  ui->labFinePha->setEnabled(b);
}

void TxTune::on_pbTune_clicked()
{
  bTune = !bTune;
  if(bTune) {
    QString style="QPushButton{background-color: red;}";
    ui->pbTune->setStyleSheet(style);
  } else {
    ui->pbTune->setStyleSheet("");
  }
}
