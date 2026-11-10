#ifndef DEVSETUP_H
#define DEVSETUP_H

#include <QDialog>
#include <QString>
#include <set>
#include <tuple>
#include <vector>
#include "sample_rates.h"
#include "ui_devsetup.h"
#include "commons.h"


class MainWindow;

// Before SoundIn opens device, asks PortAudio which of 96, 128, 192 and
// 256 kHz it accepts with 2 and 4 channels, so Settings can list it while
// SoundIn has it open. Only ALSA devices are asked; other host APIs answer
// while a device is open. Stops at the first "busy" answer.
void cacheInputDeviceRates(int device);

class DevSetup : public QDialog
{
  Q_OBJECT
public:
    explicit DevSetup(MainWindow *parent = nullptr);
  ~DevSetup();

  void initDlg();

  qint32  m_inDevList[1024];
  qint32  m_outDevList[1024];
  bool    m_restartSoundIn;
  bool    m_restartSoundOut;
  bool    m_stopSoundIn = false;
  int     m_pendingFs96000;

signals:
  void sampleRateChanged(int newRate);

public slots:
  void accept() override;
  void done(int r) override;
  void onButtonClicked();

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
  void on_sb_dB_valueChanged(int n);

private:
  struct InputDevice {
    int id;
    int maxChan;
    int minSpeed;
    int maxSpeed;
    QString name;
    QString label;
  };
  InputFit inputFit(int device, int rate, int channels);
  InputFit fillInputList(int preferredDevice);
  void rebuildInputList();
  int selectedSampleRateCode() const;
  int inDevice(int index) const;
  int outDevice(int index) const;
  MainWindow *mw;
  std::vector<InputDevice> m_inputDevices;
  bool m_inputListReady = false;
  int  m_listRate = 0;
  int  m_listChannels = 0;
  int  m_wantedInDevice = -1;   // last device chosen; kept while it isn't listed
  // devices found busy; not asked again while this dialog is open
  std::set<std::tuple<int, int, int>> m_busyProbes;
  int r,g,b,r0,g0,b0,r1,g1,b1,r2,g2,b2,r3,g3,b3;
  Ui::DialogSndCard ui;
};

#endif // DEVSETUP_H
