#include "devsetup.h"
#include "mainwindow.h"
#include "sample_rates.h"
#include <QTextStream>
#include <QDebug>
#include <cstdio>
#include <portaudio.h>
#if defined(__linux__) && __has_include(<pa_linux_alsa.h>)
#include <pa_linux_alsa.h>
#define MAP65_ALSA_RETRIES 1
#endif
#include <map>
#include <set>
#include <tuple>
#include <vector>
#include <QDir>
#include <QStringList>
#include <QSerialPortInfo>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSettings>
#include "SettingsGroup.hpp"
#include "libm65/ptt.h"

// PortAudio 19.6 (ALSA) retries opening a busy device 100 times, about 1 s.
// Settings asks about every device, including the one SoundIn has open and
// the ALSA plugins on its card, so it asks without retries and restores the
// default afterwards for SoundIn.
constexpr int kAlsaDefaultBusyRetries = 100;

static void setAlsaBusyRetries(int retries)
{
#ifdef MAP65_ALSA_RETRIES
  PaAlsa_SetRetriesBusy(retries);
#else
  (void)retries;
#endif
}

// Definite answers, kept for the rest of the run. An ALSA hw: device cannot
// be asked while it is open, so the device SoundIn uses is asked before
// SoundIn opens it; see cacheInputDeviceRates.
static std::map<std::tuple<int, int, int>, RateProbe> knownRateProbes;

// Asks PortAudio whether the device accepts the rate with the channel count,
// sample format and latency SoundIn opens the stream with.
static RateProbe inputSupportsRate(int device, int channels, int rate,
                                   int busyRetries = 0)
{
  auto const key = std::make_tuple(device, channels, rate);
  auto const known = knownRateProbes.find(key);
  if (known != knownRateProbes.end()) return known->second;

  PaDeviceInfo const* info = Pa_GetDeviceInfo(device);
  if (!info) return RateProbe::NotSupported;
  PaStreamParameters params {};
  params.device = device;
  params.channelCount = channels;
  params.sampleFormat = paFloat32;
  params.suggestedLatency = info->defaultHighInputLatency;
  setAlsaBusyRetries(busyRetries);
  PaError const result = Pa_IsFormatSupported(&params, nullptr, double(rate));
  setAlsaBusyRetries(kAlsaDefaultBusyRetries);
  switch (result) {
  case paFormatIsSupported:
    return knownRateProbes[key] = RateProbe::Supported;
  case paInvalidSampleRate:
  case paInvalidChannelCount:
  case paSampleFormatNotSupported:
    return knownRateProbes[key] = RateProbe::NotSupported;
  case paDeviceUnavailable:
    return RateProbe::Busy;
  default:        // e.g. a host error: not kept, asked again next time
    return RateProbe::NotSupported;
  }
}

void cacheInputDeviceRates(int device)
{
  PaDeviceInfo const* info = Pa_GetDeviceInfo(device);
  if (!info) return;
  PaHostApiInfo const* api = Pa_GetHostApiInfo(info->hostApi);
  if (!api || api->type != paALSA) return;
  for (int rate : {96000, 128000, 192000, 256000}) {
    for (int channels : {2, 4}) {
      if (channels > info->maxInputChannels) continue;
      // Open elsewhere: SoundIn can't open it either.
      if (inputSupportsRate(device, channels, rate, kAlsaDefaultBusyRetries)
          == RateProbe::Busy) return;
    }
  }
}

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

  {
    QSettings settings(mw->m_settings_filename, QSettings::IniFormat);
    SettingsGroup g {&settings, "DevSetup"};
    restoreGeometry(settings.value("geometry").toByteArray());
  }

  m_restartSoundIn=false;
  m_restartSoundOut=false;
  m_pendingFs96000=-1;

  QButtonGroup *buttonGroup = new QButtonGroup(this);
  buttonGroup->addButton(ui.w3szBut);
  buttonGroup->addButton(ui.otherBut);

  for (QRadioButton* rb : {ui.rb95238, ui.rb96000, ui.rb128000, ui.rb192000, ui.rb256000}) {
    connect(rb, &QRadioButton::toggled, this, [this](bool checked) {
      if (checked) rebuildInputList();
    });
  }

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

void DevSetup::done(int r)
{
  // accept()/reject() (OK, Cancel, and the title-bar close button, which
  // triggers reject() internally) all funnel through here -- unlike
  // closeEvent(), which only fires for the title-bar close button, not
  // for accept()/reject() called directly from button clicks.
  QSettings settings(mw->m_settings_filename, QSettings::IniFormat);
  SettingsGroup g {&settings, "DevSetup"};
  settings.setValue("geometry", saveGeometry());
  QDialog::done(r);
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

  int numDevices = Pa_GetDeviceCount();
  
  if (numDevices > MAXDEVICES) {
      numDevices = MAXDEVICES;
  }

  // Pass the pointers to the data inside the vectors
setAlsaBusyRetries(0);
getDev(&numDevices,
       reinterpret_cast<char (*)[50]>(hostAPI_DeviceName.data()),
       minChan.data(), maxChan.data(), minSpeed.data(), maxSpeed.data());
setAlsaBusyRetries(kAlsaDefaultBusyRetries);
  
m_inputDevices.clear();
for (id = 0; id < numDevices; id++) {

    // getDev can't ask an ALSA device a stream has open and leaves it blank.
    // For the device SoundIn has open, use the channel count PortAudio found
    // at start-up and no rate range: the answers kept from before SoundIn
    // opened it decide at every rate.
    if (maxChan[id] == 0 && id == mw->m_soundInDevice) {
      PaDeviceInfo const* info = Pa_GetDeviceInfo(id);
      PaHostApiInfo const* api = info ? Pa_GetHostApiInfo(info->hostApi) : nullptr;
      if (!api || api->type != paALSA || info->maxInputChannels < 1) continue;
      maxChan[id] = info->maxInputChannels;
    }

#ifdef _WIN32
    if (!QString(hostAPI_DeviceName[id].name).contains("MME"))
        continue;
#endif

    // getDev cuts names to 39 characters, which drops the "(hw:N,M)" that
    // tells ALSA devices apart; keep only its host API column.
    PaDeviceInfo const* info = Pa_GetDeviceInfo(id);
    QString const name = info ? QString(info->name).trimmed()
                              : QString(hostAPI_DeviceName[id].name).mid(9).trimmed();
    QString const label = QString("%1   %2  %3 %4")
        .arg(id, 2).arg(maxChan[id])
        .arg(QString(hostAPI_DeviceName[id].name).left(8), -8).arg(name);

    m_inputDevices.push_back({id, maxChan[id], minSpeed[id], maxSpeed[id], name, label});
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

    snprintf(p2, sizeof(p2), "%2d   %-8.8s  %s",
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
  if (idx < 0) {
      ui.pttComboBox->addItem(saved);
      idx = ui.pttComboBox->count() - 1;
  }
  if (idx >= 0)
      ui.pttComboBox->setCurrentIndex(idx);

  int const shownSampleRateCode = dialogSampleRateCode(mw->m_fs96000, mw->m_pendingFs96000);

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
  {
    QSignalBlocker blockNetwork {ui.networkRadioButton};
    QSignalBlocker blockSoundCard {ui.soundCardRadioButton};
    ui.networkRadioButton->setChecked(mw->m_network);
    ui.soundCardRadioButton->setChecked(!mw->m_network);
  }
  ui.rb256000->setEnabled(sampleRateCodeFitsAddressSpace(4));
  ui.rb256000->setChecked(shownSampleRateCode == 4);
  ui.rb192000->setChecked(shownSampleRateCode == 2);
  ui.rb128000->setChecked(shownSampleRateCode == 3);
  ui.rb96000->setChecked(shownSampleRateCode == 1);
  ui.rb95238->setChecked(shownSampleRateCode == 0);
  on_soundCardRadioButton_toggled(ui.soundCardRadioButton->isChecked());
  ui.rbIQXT->setChecked(mw->m_bIQxt);
  ui.rbSi570->setChecked(!mw->m_bIQxt);
  ui.mult570TxSpinBox->setEnabled(mw->m_bIQxt);
  ui.comboBoxSndIn->setEnabled(!mw->m_network);
  m_inputListReady = true;
  m_wantedInDevice = savedInputDevice(mw->m_paInDevice, mw->m_pendingPaInDevice);
  fillInputList(m_wantedInDevice);
  connect(ui.comboBoxSndIn, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int row) {
            int const device = inDevice(row);
            if (device >= 0) m_wantedInDevice = device;
          });
  // If the saved output device is no longer listed, show the first one; OK
  // then saves it.
  // No output device (the saved one wasn't found): leave it unselected so an
  // unrelated OK doesn't replace it. One that exists but isn't listed shows
  // the first output device, which OK then saves.
  int const outRow = rowOfDevice(m_outDevList, ui.comboBoxSndOut->count(), mw->m_paOutDevice);
  ui.comboBoxSndOut->setCurrentIndex(outRow >= 0 || mw->m_paOutDevice < 0 ? outRow : 0);
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

  if (ui.soundCardRadioButton->isChecked() &&
      inDevice(ui.comboBoxSndIn->currentIndex()) < 0) {
    QMessageBox::warning(this, "Input Device",
        ui.comboBoxSndIn->count() > 0
            ? QString("Select an input device.")
            : QString("No input device can be used at %1 kHz with %2 input channels.\n"
                      "Choose another sample rate%3 or use network input.")
                  .arg(m_listRate / 1000.0, 0, 'f', 0).arg(m_listChannels)
                  .arg(m_listChannels == 4 ? ", untick Xpol," : ""));
    return;
  }

  int const newFs96000 = selectedSampleRateCode();
  int const newSampleRate = sampleRateForCode(newFs96000);
  double const newFAdd = ui.faddEntry->text().toDouble();

  int const previousPending = mw->m_pendingFs96000;
  m_pendingFs96000 = pendingSampleRateCode(mw->m_fs96000, newFs96000);
  if (m_pendingFs96000 < 0) {
    emit sampleRateChanged(newSampleRate);
  }

  int const newInIndex = ui.comboBoxSndIn->currentIndex();
  int const newOutIndex = ui.comboBoxSndOut->currentIndex();
  int const newInDev = inDevice(newInIndex);
  int const newOutDev = outDevice(newOutIndex);

  // SoundIn restarts at the running rate, so in sound-card mode a device that
  // was listed for a pending rate but can't run at the running one (or not
  // with the new channel count) waits for the restart, and input stops until
  // then.
  bool const soundCard = ui.soundCardRadioButton->isChecked();
  int const channels = ui.cbXpol->isChecked() ? 4 : 2;
  bool const runsNow = !soundCard ||
      inputFit(newInDev, inputListSampleRate(mw->m_fs96000), channels) == InputFit::Listed;
  int const previousPendingIn = mw->m_pendingPaInDevice;
  int pendingInDev;
  int const inDev = inputDeviceToApply(mw->m_paInDevice, newInDev, runsNow,
                                       m_pendingFs96000 >= 0, previousPendingIn, pendingInDev);

  if(mw->m_network!=ui.networkRadioButton->isChecked() or
     mw->m_paInDevice!=inDev or
     mw->m_xpol!=ui.cbXpol->isChecked() or
     mw->m_fAdd!=newFAdd or
     mw->m_udpPort!=ui.sbPort->value() or
     mw->m_soundInStopped or
     !mw->soundInRunning()) m_restartSoundIn=true;   // e.g. its open or start failed
  if(!runsNow) {
    m_restartSoundIn=false;
    m_stopSoundIn=!mw->m_soundInStopped;
  }

  if(newOutDev >= 0 and
     mw->m_paOutDevice!=newOutDev) m_restartSoundOut=true;

  mw->m_myCall=ui.myCallEntry->text();
  mw->m_myGrid=ui.myGridEntry->text();
  mw->m_idInt=ui.idIntSpinBox->value();
  
  mw->m_pttPath = ui.pttComboBox->currentText();
  if (mw->m_pttPath == "NONE")
      mw->m_pttPortNumber = 0;
  else if (mw->m_pttPath.startsWith("COM"))
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
  mw->m_fAdd=newFAdd;
  mw->m_network=ui.networkRadioButton->isChecked();
  mw->m_bIQxt=ui.rbIQXT->isChecked();
  if (newInDev >= 0) mw->m_nDevIn=newInIndex;
  mw->m_paInDevice=inDev;
  mw->m_pendingPaInDevice=pendingInDev;
  if (newOutDev >= 0) {
    mw->m_nDevOut=newOutIndex;
    mw->m_paOutDevice=newOutDev;
  }
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

  if (restartNoticeNeeded(previousPending, m_pendingFs96000) ||
      restartNoticeNeeded(previousPendingIn, pendingInDev) || m_stopSoundIn) {
      QMessageBox::information(this,
          "Restart Required",
          QString("%1%2Please exit MAP65 and start it again.")
              .arg(m_pendingFs96000 >= 0
                   ? "Changing sample rate requires MAP65 to be restarted.\n" : "")
              .arg(!runsNow
                   ? "The selected input device can't run at the current sample rate\n"
                     "with these settings, so input stops until the restart.\n" : ""));
  }

  // Normal path
  QDialog::accept();

}

int DevSetup::selectedSampleRateCode() const
{
  return ui.rb256000->isChecked() ? 4 :
      ui.rb192000->isChecked() ? 2 :
      ui.rb128000->isChecked() ? 3 :
      ui.rb95238->isChecked()  ? 0 :
      1;
}

// PortAudio device number behind a combo box row, or -1 when there is no such
// row (nothing selected, or an empty list).
int DevSetup::inDevice(int index) const
{
  return index >= 0 && index < ui.comboBoxSndIn->count() ? m_inDevList[index] : -1;
}

int DevSetup::outDevice(int index) const
{
  return index >= 0 && index < ui.comboBoxSndOut->count() ? m_outDevList[index] : -1;
}

// Whether the input device can run at rate with channels, and if not, why.
InputFit DevSetup::inputFit(int device, int rate, int channels)
{
  // SoundIn has this device open at the running rate and channel count; an
  // open ALSA hw: device can't be asked.
  if (device >= 0 && device == mw->m_soundInDevice && mw->soundInRunning() &&
      rate == inputListSampleRate(mw->m_fs96000) && channels == mw->m_soundInChannels)
    return InputFit::Listed;
  for (auto const& d : m_inputDevices) {
    if (d.id != device) continue;
    return inputDeviceFit(rate, channels, d.maxChan, d.minSpeed, d.maxSpeed, [&] {
      auto const key = std::make_tuple(d.id, channels, rate);
      if (m_busyProbes.count(key)) return RateProbe::Busy;
      RateProbe const answer = inputSupportsRate(d.id, channels, rate);
      if (answer == RateProbe::Busy) m_busyProbes.insert(key);
      return answer;
    });
  }
  return InputFit::RateNotSupported;
}

// Fills the input list for the rate and channel count Settings has selected,
// keeping preferredDevice selected if it is still listed, otherwise nothing.
// Returns whether preferredDevice is listed, and if not, why.
InputFit DevSetup::fillInputList(int preferredDevice)
{
  int const rate = inputListSampleRate(selectedSampleRateCode());
  int const channels = ui.cbXpol->isChecked() ? 4 : 2;
  m_listRate = rate;
  m_listChannels = channels;

  QSignalBlocker block {ui.comboBoxSndIn};
  ui.comboBoxSndIn->clear();
  InputFit preferredFit = InputFit::RateNotSupported;
  int k = 0;
  for (auto const& d : m_inputDevices) {
    InputFit const fit = inputFit(d.id, rate, channels);
    if (d.id == preferredDevice) preferredFit = fit;
    if (fit != InputFit::Listed) continue;
    m_inDevList[k++] = d.id;
    ui.comboBoxSndIn->addItem(d.label);
  }
  ui.comboBoxSndIn->setCurrentIndex(rowOfDevice(m_inDevList, k, preferredDevice));
  return preferredFit;
}

// Called when the rate or Xpol choice changes: relists the devices and, in
// sound-card mode, says so if the chosen device no longer qualifies.
void DevSetup::rebuildInputList()
{
  if (!m_inputListReady) return;
  if (inputListSampleRate(selectedSampleRateCode()) == m_listRate &&
      (ui.cbXpol->isChecked() ? 4 : 2) == m_listChannels) return;

  // The combo box handler keeps any selected device equal to m_wantedInDevice.
  int const previous = inDevice(ui.comboBoxSndIn->currentIndex());
  QString previousName;
  int previousChannels = 0;
  for (auto const& d : m_inputDevices) {
    if (d.id == previous) {
      previousName = d.name;
      previousChannels = d.maxChan;
    }
  }
  InputFit const fit = fillInputList(m_wantedInDevice);
  if (previous >= 0 && ui.comboBoxSndIn->currentIndex() < 0 &&
      ui.soundCardRadioButton->isChecked()) {
    QString const kHz = QString::number(m_listRate / 1000.0, 'f', 0);
    QString reason;
    switch (fit) {
    case InputFit::TooFewChannels:
      // Only reachable by ticking Xpol: devices with fewer than 2 channels
      // are never listed.
      reason = QString("has %1 input channel%2; Xpol needs 4.\n"
                       "Choose another input device or untick Xpol.")
          .arg(previousChannels).arg(previousChannels == 1 ? "" : "s");
      break;
    case InputFit::Busy:
      reason = QString("is in use and can't be checked at %1 kHz: another program has it open,\n"
                       "or MAP65 is using another device on the same sound card.\n"
                       "Choose another input device, or close the program using it.")
          .arg(kHz);
      break;
    default:
      reason = QString("does not support %1 kHz%2.\n"
                       "Choose another input device or sample rate.")
          .arg(kHz).arg(m_listChannels == 4 ? " with 4 input channels" : "");
      break;
    }
    QMessageBox::warning(this, "Input Device",
        QString("The input device\n%1\n%2").arg(previousName, reason));
  }
}

void DevSetup::on_soundCardRadioButton_toggled(bool checked)
{
  ui.comboBoxSndIn->setEnabled(ui.soundCardRadioButton->isChecked());
  int fs96000 = selectedSampleRateCode();
  if(checked) {
    if(fs96000 == 0) fs96000 = 1;
    ui.rb95238->setEnabled(false);
  }
  else {
    ui.rb95238->setEnabled(true);
  }
  ui.rb256000->setChecked(fs96000 == 4);
  ui.rb192000->setChecked(fs96000 == 2);
  ui.rb128000->setChecked(fs96000 == 3);
  ui.rb96000->setChecked(fs96000 == 1);
  ui.rb95238->setChecked(fs96000 == 0);
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
  bool const xpol = n!=0;
  ui.rbAntenna->setEnabled(xpol);
  ui.rbAntennaX->setEnabled(xpol);
  ui.dPhiSpinBox->setEnabled(xpol);
  ui.labelDphi->setEnabled(xpol);
  rebuildInputList();
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
