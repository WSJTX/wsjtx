#include "devsetup.h"
#include "mainwindow.h"
#include <QTextStream>
#include <QDebug>
#include <cstdio>
#include <portaudio.h>
#include <vector>
#include <QDir>
#include <QStringList>
#include <QSerialPortInfo>
#include <QRegularExpression>

#if !defined(Q_OS_WIN)
extern "C" {
    void ptt_set_override(const char *path);
}
#endif

static QStringList enumeratePorts()
{
    QStringList result;

    QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();

    for (auto const& p : ports)
    {
        if (!p.portName().contains("NULL"))
        {
            QString loc = p.systemLocation();
            loc.remove(QRegularExpression{R"(^\\\\\.\\)"});
            result << loc;
        }
    }

    std::sort(result.begin(), result.end(), [](QString const& a, QString const& b) {
        QRegularExpression re{"(\\d+)$"};
        auto ma = re.match(a);
        auto mb = re.match(b);

        if (ma.hasMatch() && mb.hasMatch())
            return ma.captured(1).toInt() < mb.captured(1).toInt();

        return a < b;
    });

    return result;
}

#define MAXDEVICES 1024 // was 200

//----------------------------------------------------------- DevSetup()
DevSetup::DevSetup(MainWindow *parent)
    : QDialog(parent),
      mw(parent)
{
  ui.setupUi(this);	//setup the dialog form
  m_restartSoundIn=false;
  m_restartSoundOut=false;

  QButtonGroup *buttonGroup = new QButtonGroup(this);
  buttonGroup->addButton(ui.w3szBut);
  buttonGroup->addButton(ui.otherBut);
  
connect(buttonGroup,
        QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
        this,
        [this](QAbstractButton *) {
            onButtonClicked();
        });

}

DevSetup::~DevSetup()
{
}

void DevSetup::initDlg()
{
  int k,id;
  
  // Use heap-allocated vectors instead of stack arrays
  std::vector<int> minChan(MAXDEVICES);
  std::vector<int> maxChan(MAXDEVICES);
  std::vector<int> minSpeed(MAXDEVICES);
  std::vector<int> maxSpeed(MAXDEVICES);
  
  // Use a unique pointer or vector for the 2D char array
  struct DeviceName {
    char name[50];
};
  std::vector<DeviceName> hostAPI_DeviceName(MAXDEVICES);

  char s[256];
  int numDevices = Pa_GetDeviceCount();
  
  if (numDevices > MAXDEVICES) {
      numDevices = MAXDEVICES;
  }

  // Pass the pointers to the data inside the vectors
getDev(&numDevices,
       reinterpret_cast<char (*)[50]>(hostAPI_DeviceName.data()),
       minChan.data(), maxChan.data(), minSpeed.data(), maxSpeed.data());
  
k = 0;
for (id = 0; id < numDevices; id++) {

    if (!(96000 >= minSpeed[id] && 96000 <= maxSpeed[id]))
        continue;

#ifdef _WIN32
    if (!QString(hostAPI_DeviceName[id].name).contains("MME"))
        continue;
#endif

    // Now safe to add to list
    m_inDevList[k] = id;

    snprintf(s, sizeof(s), "%2d   %d  %-49.49s",
             id, maxChan[id], hostAPI_DeviceName[id].name);

    ui.comboBoxSndIn->addItem(QString(s));
    k++;
}

  const PaDeviceInfo *pdi;
  int nchout;
  char p2[256];

  k=0;
 for (id = 0; id < numDevices; id++) {

    pdi = Pa_GetDeviceInfo(id);
    if (!pdi) continue;

    nchout = pdi->maxOutputChannels;
    if (nchout < 1)
        continue;

    QString devName = QString(pdi->name);

#ifdef __linux__
    QString lower = devName.toLower();

    // Only allow plug/resampling devices for TX
    bool isPlug =
        lower.contains("default")  ||
        lower.contains("dmix")     ||
        lower.contains("pulse")    ||
        lower.contains("pipewire") ||
        !lower.contains("hw:");    // reject raw hw: devices

    if (!isPlug)
        continue;
#endif

    const char* api = Pa_GetHostApiInfo(pdi->hostApi)->name;

    // Skip WASAPI and WDM-KS for TX (Windows only)
    if (strstr(api, "WASAPI") || strstr(api,"WDM-KS"))
        continue;

    // Now safe to add to list
    m_outDevList[k++] = id;

    // Determine label
    const char* p1 = "";
    if (strstr(api, "MME"))     p1 = "MME";
    if (strstr(api, "Direct"))  p1 = "DirectX";
    if (strstr(api, "ASIO"))    p1 = "ASIO";
    if (strstr(api, "ALSA"))    p1 = "ALSA";

    snprintf(p2, sizeof(p2), "%2d   %-8.8s  %-39.39s",
             id, p1, pdi->name);

    ui.comboBoxSndOut->addItem(QString(p2));
}
  ui.myCallEntry->setText(mw->m_myCall);
  ui.myGridEntry->setText(mw->m_myGrid);
  ui.idIntSpinBox->setValue(mw->m_idInt);

  ui.pttComboBox->clear();
  ui.pttComboBox->addItem("NONE");

  // unified cross-platform enumeration
  QStringList ports = enumeratePorts();
  for (auto const& p : ports)
      ui.pttComboBox->addItem(p);

  // restore saved selection
  QString saved = mw->m_pttPath;
  int idx = ui.pttComboBox->findText(saved);
  if (idx >= 0)
      ui.pttComboBox->setCurrentIndex(idx);

  // backend override (Linux/macOS only)
  #if !defined(Q_OS_WIN)
  if (ui.pttComboBox->currentText() != "NONE")
      ptt_set_override(ui.pttComboBox->currentText().toUtf8().constData());
  else
      ptt_set_override(nullptr);
  #endif

  ui.astroFont->setValue(mw->m_astroFont);
  ui.cbXpol->setChecked(mw->m_xpol);
  ui.rbAntennaX->setChecked(mw->m_xpolx);
  ui.saveDirEntry->setText(mw->m_saveDir);
  ui.azelDirEntry->setText(mw->m_azelDir);
  ui.editorEntry->setText(mw->m_editorCommand);
  ui.dxccEntry->setText(mw->m_dxccPfx);
  ui.timeoutSpinBox->setValue(mw->m_timeout);
  ui.dPhiSpinBox->setValue(mw->m_dPhi);
  ui.fCalSpinBox->setValue(mw->m_fCal);
  ui.faddEntry->setText(QString::number(mw->m_fAdd,'f',3));
  ui.networkRadioButton->setChecked(mw->m_network);
  ui.soundCardRadioButton->setChecked(!mw->m_network);
  ui.rb96000->setChecked(mw->m_fs96000);
  ui.rb95238->setChecked(!mw->m_fs96000);
  ui.rbIQXT->setChecked(mw->m_bIQxt);
  ui.rbSi570->setChecked(!mw->m_bIQxt);
  ui.mult570TxSpinBox->setEnabled(mw->m_bIQxt);
  ui.comboBoxSndIn->setEnabled(!mw->m_network);
  ui.comboBoxSndIn->setCurrentIndex(mw->m_nDevIn);
  ui.comboBoxSndOut->setCurrentIndex(mw->m_nDevOut);
  ui.sbPort->setValue(mw->m_udpPort);
  ui.cbIQswap->setChecked(mw->m_IQswap);
  ui.cbInitIQplus->setChecked(mw->m_initIQplus);
  ui.sb_dB->setValue(mw->m_dB);
  ui.mult570SpinBox->setValue(mw->m_mult570);
  ui.mult570TxSpinBox->setValue(mw->m_mult570Tx);
  ui.cal570SpinBox->setValue(mw->m_cal570);
  ui.sbTxOffset->setValue(mw->m_TxOffset);
  ::sscanf (mw->m_colors.toLatin1(),"%2x%2x%2x%2x%2x%2x%2x%2x%2x%2x%2x%2x%2x%2x%2x",
            &r,&g,&b,&r0,&g0,&b0,&r1,&g1,&b1,&r2,&g2,&b2,&r3,&g3,&b3);
  updateColorLabels();
  ui.sbBackgroundRed->setValue(r);
  ui.sbBackgroundGreen->setValue(g);
  ui.sbBackgroundBlue->setValue(b);
  ui.sbRed0->setValue(r0);
  ui.sbRed1->setValue(r1);
  ui.sbRed2->setValue(r2);
  ui.sbRed3->setValue(r3);
  ui.sbGreen0->setValue(g0);
  ui.sbGreen1->setValue(g1);
  ui.sbGreen2->setValue(g2);
  ui.sbGreen3->setValue(g3);
  ui.sbBlue0->setValue(b0);
  ui.sbBlue1->setValue(b1);
  ui.sbBlue2->setValue(b2);
  ui.sbBlue3->setValue(b3);

  mw->m_paInDevice=m_inDevList[mw->m_nDevIn];
  mw->m_paOutDevice=m_outDevList[mw->m_nDevOut];

  ui.otherUrlBox->setText(mw->m_otherUrl);
  if(mw->m_w3szUrl) ui.w3szBut->setChecked(true);
  else ui.otherBut->setChecked(true);
  
  ui.pskBox->setChecked(mw->m_spot_to_psk_reporter);
  ui.pskReporterTcpIpBox->setChecked(mw->m_psk_reporter_tcpip);
}

//------------------------------------------------------- accept()
void DevSetup::accept()
{
  // Called when OK button is clicked.
  // Check to see whether SoundInThread must be restarted,
  // and save user parameters.

  if(mw->m_network!=ui.networkRadioButton->isChecked() or
     mw->m_nDevIn!=ui.comboBoxSndIn->currentIndex() or
     mw->m_paInDevice!=m_inDevList[mw->m_nDevIn] or
     mw->m_xpol!=ui.cbXpol->isChecked() or
     mw->m_udpPort!=ui.sbPort->value()) m_restartSoundIn=true;

  if(mw->m_nDevOut!=ui.comboBoxSndOut->currentIndex() or
     mw->m_paOutDevice!=m_outDevList[mw->m_nDevOut]) m_restartSoundOut=true;

  mw->m_myCall=ui.myCallEntry->text();
  mw->m_myGrid=ui.myGridEntry->text();
  mw->m_idInt=ui.idIntSpinBox->value();
  
  mw->m_pttPath = ui.pttComboBox->currentText();
      if (mw->m_pttPath.startsWith("COM"))
          mw->m_pttPortNumber = mw->m_pttPath.mid(3).toInt();
      else
        mw->m_pttPortNumber = 1;

#if !defined(Q_OS_WIN)
    if (ui.pttComboBox->currentText() != "NONE")
        ptt_set_override(ui.pttComboBox->currentText().toUtf8().constData());
    else
        ptt_set_override(nullptr);
#endif

  mw->m_astroFont=ui.astroFont->value();
  mw->m_xpol=ui.cbXpol->isChecked();
  mw->m_xpolx=ui.rbAntennaX->isChecked();
  mw->m_saveDir=ui.saveDirEntry->text();
  mw->m_azelDir=ui.azelDirEntry->text();
  mw->m_editorCommand=ui.editorEntry->text();
  mw->m_dxccPfx=ui.dxccEntry->text();
  mw->m_timeout=ui.timeoutSpinBox->value();
  mw->m_dPhi=ui.dPhiSpinBox->value();
  mw->m_fCal=ui.fCalSpinBox->value();
  mw->m_fAdd=ui.faddEntry->text().toDouble();
  mw->m_network=ui.networkRadioButton->isChecked();
  mw->m_fs96000=ui.rb96000->isChecked();
  mw->m_bIQxt=ui.rbIQXT->isChecked();
  mw->m_nDevIn=ui.comboBoxSndIn->currentIndex();
  mw->m_paInDevice=m_inDevList[mw->m_nDevIn];
  mw->m_nDevOut=ui.comboBoxSndOut->currentIndex();
  mw->m_paOutDevice=m_outDevList[mw->m_nDevOut];
  mw->m_udpPort=ui.sbPort->value();
  mw->m_IQswap=ui.cbIQswap->isChecked();
  mw->m_initIQplus=ui.cbInitIQplus->isChecked();
  mw->m_dB=ui.sb_dB->value();
  mw->m_mult570=ui.mult570SpinBox->value();
  mw->m_mult570Tx=ui.mult570TxSpinBox->value();
  mw->m_cal570=ui.cal570SpinBox->value();
  mw->m_TxOffset=ui.sbTxOffset->value();
  mw->m_otherUrl=ui.otherUrlBox->text();
  mw->m_w3szUrl = ui.w3szBut->isChecked();  
  
  mw->m_spot_to_psk_reporter = ui.pskBox->isChecked();
  mw->m_psk_reporter_tcpip = ui.pskReporterTcpIpBox->isChecked();

  QDialog::accept();
}

void DevSetup::on_soundCardRadioButton_toggled(bool checked)
{
  ui.comboBoxSndIn->setEnabled(ui.soundCardRadioButton->isChecked());
  ui.rb96000->setChecked(checked);
  ui.rb95238->setEnabled(!checked);
  ui.label_InputDev->setEnabled(checked);
  ui.label_Port->setEnabled(!checked);
  ui.sbPort->setEnabled(!checked);
  ui.cbIQswap->setEnabled(checked);
  ui.sb_dB->setEnabled(checked);
}

void DevSetup::onButtonClicked()
{
    if(ui.w3szBut->isChecked())
    {
        mw->m_w3szUrl = true;
    }
    else
    {
        mw->m_w3szUrl = false;
    }
}

void DevSetup::on_cbXpol_stateChanged(int n)
{
  mw->m_xpol = (n!=0);
  ui.rbAntenna->setEnabled(mw->m_xpol);
  ui.rbAntennaX->setEnabled(mw->m_xpol);
  ui.dPhiSpinBox->setEnabled(mw->m_xpol);
  ui.labelDphi->setEnabled(mw->m_xpol);
}

void DevSetup::on_cal570SpinBox_valueChanged(double ppm)
{
  mw->m_cal570=ppm;
}

void DevSetup::on_mult570SpinBox_valueChanged(int mult)
{
  mw->m_mult570=mult;
}

void DevSetup::on_sb_dB_valueChanged(int n)
{
  mw->m_dB=n;
}

void DevSetup::updateColorLabels()
{
  QString t;
  int r=ui.sbBackgroundRed->value();
  int g=ui.sbBackgroundGreen->value();
  int b=ui.sbBackgroundBlue->value();
  int r0=ui.sbRed0->value();
  int r1=ui.sbRed1->value();
  int r2=ui.sbRed2->value();
  int r3=ui.sbRed3->value();
  int g0=ui.sbGreen0->value();
  int g1=ui.sbGreen1->value();
  int g2=ui.sbGreen2->value();
  int g3=ui.sbGreen3->value();
  int b0=ui.sbBlue0->value();
  int b1=ui.sbBlue1->value();
  int b2=ui.sbBlue2->value();
  int b3=ui.sbBlue3->value();

  ui.lab0->setStyleSheet (
                          QString {"QLabel{background-color: #%1%2%3; color: #%4%5%6}"}
                             .arg (r, 2, 16, QLatin1Char {'0'})
                             .arg (g, 2, 16, QLatin1Char {'0'})
                             .arg (b, 2, 16, QLatin1Char {'0'})
                             .arg (r0, 2, 16, QLatin1Char {'0'})
                             .arg (g0, 2, 16, QLatin1Char {'0'})
                             .arg (b0, 2, 16, QLatin1Char {'0'})
                          );
  ui.lab1->setStyleSheet(
                         QString {"QLabel{background-color: #%1%2%3; color: #%4%5%6}"}
                            .arg (r, 2, 16, QLatin1Char {'0'})
                            .arg (g, 2, 16, QLatin1Char {'0'})
                            .arg (b, 2, 16, QLatin1Char {'0'})
                            .arg (r1, 2, 16, QLatin1Char {'0'})
                            .arg (g1, 2, 16, QLatin1Char {'0'})
                            .arg (b1, 2, 16, QLatin1Char {'0'})
                         );
  ui.lab2->setStyleSheet(
                         QString {"QLabel{background-color: #%1%2%3; color: #%4%5%6}"}
                            .arg (r, 2, 16, QLatin1Char {'0'})
                            .arg (g, 2, 16, QLatin1Char {'0'})
                            .arg (b, 2, 16, QLatin1Char {'0'})
                            .arg (r2, 2, 16, QLatin1Char {'0'})
                            .arg (g2, 2, 16, QLatin1Char {'0'})
                            .arg (b2, 2, 16, QLatin1Char {'0'})
                         );
  ui.lab3->setStyleSheet(
                         QString {"QLabel{background-color: #%1%2%3; color: #%4%5%6}"}
                            .arg (r, 2, 16, QLatin1Char {'0'})
                            .arg (g, 2, 16, QLatin1Char {'0'})
                            .arg (b, 2, 16, QLatin1Char {'0'})
                            .arg (r3, 2, 16, QLatin1Char {'0'})
                            .arg (g3, 2, 16, QLatin1Char {'0'})
                            .arg (b3, 2, 16, QLatin1Char {'0'})
                         );

  mw->m_colors.clear ();
  QTextStream ots {&mw->m_colors, QIODevice::WriteOnly};
  ots.setIntegerBase (16);
  ots.setFieldWidth (2);
  ots.setPadChar ('0');
  ots << r << g << b << r0 << g0 << b0 << r1 << g1 << b1 << r2 << g2 << b2 << r3 << g3 << b3;
}

void DevSetup::on_sbBackgroundRed_valueChanged(int /*r*/)
{
  updateColorLabels();
}

void DevSetup::on_sbBackgroundGreen_valueChanged(int /*g*/)
{
  updateColorLabels();
}

void DevSetup::on_sbBackgroundBlue_valueChanged(int /*b*/)
{
  updateColorLabels();
}


void DevSetup::on_sbRed0_valueChanged(int /*arg1*/)
{
  updateColorLabels();
}

void DevSetup::on_sbGreen0_valueChanged(int /*arg1*/)
{
  updateColorLabels();
}

void DevSetup::on_sbBlue0_valueChanged(int /*arg1*/)
{
  updateColorLabels();
}

void DevSetup::on_sbRed1_valueChanged(int /*arg1*/)
{
   updateColorLabels();
}

void DevSetup::on_sbGreen1_valueChanged(int /*arg1*/)
{
  updateColorLabels();
}

void DevSetup::on_sbBlue1_valueChanged(int /*arg1*/)
{
   updateColorLabels();
}

void DevSetup::on_sbRed2_valueChanged(int /*arg1*/)
{
   updateColorLabels();
}

void DevSetup::on_sbGreen2_valueChanged(int /*arg1*/)
{
   updateColorLabels();
}

void DevSetup::on_sbBlue2_valueChanged(int /*arg1*/)
{
   updateColorLabels();
}

void DevSetup::on_sbRed3_valueChanged(int /*arg1*/)
{
  updateColorLabels();
}

void DevSetup::on_sbGreen3_valueChanged(int /*arg1*/)
{
  updateColorLabels();
}

void DevSetup::on_sbBlue3_valueChanged(int /*arg1*/)
{
  updateColorLabels();
}

void DevSetup::on_pushButton_5_clicked()
{
  QColor color = QColorDialog::getColor(Qt::green, this);
  if (color.isValid()) {
  }
}

void DevSetup::on_mult570TxSpinBox_valueChanged(int n)
{
  mw->m_mult570Tx=n;
}

void DevSetup::on_rbIQXT_toggled(bool checked)
{
  mw->m_bIQxt=checked;
  ui.mult570TxSpinBox->setEnabled(mw->m_bIQxt);
  ui.label_25->setEnabled(mw->m_bIQxt);
  ui.sbTxOffset->setEnabled(mw->m_bIQxt);
  ui.label_26->setEnabled(mw->m_bIQxt);
}

void DevSetup::on_sbTxOffset_valueChanged(double f)
{
  mw->m_TxOffset=f;
}
