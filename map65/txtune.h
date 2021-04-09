#ifndef TXTUNE_H
#define TXTUNE_H

#include <QDialog>

namespace Ui {
class TxTune;
}

class TxTune : public QDialog
{
    Q_OBJECT
    
public:
    explicit TxTune(QWidget *parent = 0);
    ~TxTune();
    
public slots:
  void accept();
  void reject();

private slots:
  void on_pwrSlider_valueChanged(int n);
  void on_ampSlider_valueChanged(int n);
  void on_phaSlider_valueChanged(int n);
  void on_cbTxImage_toggled(bool b);
  void on_pbTune_clicked();
  void on_fineAmpSlider_valueChanged(int n);
  void on_finePhaSlider_valueChanged(int n);

public:
  void set_iqAmp(int n);
  void set_iqPhase(int n);
  void set_txPower(int n);

private:
  qint32  m_iqAmp1;
  qint32  m_iqAmp2;
  qint32  m_iqPha1;
  qint32  m_iqPha2;
  qint32  m_saveAmp;
  qint32  m_savePha;
  qint32  m_saveTxPower;

  Ui::TxTune *ui;
};

#endif // TXTUNE_H
