//------------------------------------------------------------------ MainWindow
#include "mainwindow.h"
#include <fftw3.h>
#include <QDir>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QToolTip>
#include "revision_utils.hpp"
#include "qt_helpers.hpp"
#include "SettingsGroup.hpp"
#include "widgets/MessageBox.hpp"
#include "ui_mainwindow.h"
#include "devsetup.h"
#include "plotter.h"
#include "about.h"
#include "astro.h"
#include "widegraph.h"
#include "messages.h"
#include "bandmap.h"
#include "txtune.h"
#include "sleep.h"
#include "commons.h"
#include "soundin.h"
#include <portaudio.h>
#include <iostream>

#include <QApplication>
#include <QDebug>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QString>
#include <QByteArray>

//#include <io.h>
#include <stdio.h>

#include <cstdio>
#include <cstdlib>
#include <unistd.h>   // for pipe(), dup2()
#include <math.h>
#include <thread>
#include <fcntl.h>

#include "stdout_channel.h"
#include "fortran_mutex.hpp"

#if !defined(Q_OS_WIN)
extern "C" {
    void ptt_set_override(const char *path);
}
#endif

extern "C" {
    int ptt_(int* nport, int* itx, int* iptt);
}

#ifdef __unix__
extern "C" void ptt_close(void);
#endif

#ifdef MessageBox
#undef MessageBox
#endif

#define NFFT 32768


QByteArray g_TxTuneGeometry;

namespace {
struct Map65TxWaveStorage { short int samples[2*60*12000]; };
struct Map65RxSamplesStorage { qint16 samples[4*60*96000]; };

QString writableMap65DataDir()
{
  QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  if (dataDir.isEmpty()) {
    dataDir = QDir::home().absoluteFilePath(".map65");
  }

  if (!QDir{}.mkpath(dataDir)) {
    qWarning() << "Unable to create MAP65 data directory:" << dataDir;
  }

  QDir dir {dataDir};
  if (!dir.mkpath("save")) {
    qWarning() << "Unable to create MAP65 save directory:" << dir.absoluteFilePath("save");
  }
  return dataDir;
}

QString map65SettingsFile(QString const& appDir, QString const& dataDir)
{
  QString settingsFile = QDir {dataDir}.absoluteFilePath("map65.ini");
  QString legacySettingsFile = QDir {appDir}.absoluteFilePath("map65.ini");
  if (!QFile::exists(settingsFile) && QFile::exists(legacySettingsFile)) {
    if (QFile::copy(legacySettingsFile, settingsFile)) {
      QFile::setPermissions(settingsFile, QFile::ReadOwner | QFile::WriteOwner
                            | QFile::ReadGroup | QFile::ReadOther);
    } else {
      qWarning() << "Unable to migrate MAP65 settings from" << legacySettingsFile
                 << "to" << settingsFile;
    }
  }
  return settingsFile;
}
}  // namespace

short int (&iwave)[2*60*12000] = (new Map65TxWaveStorage{})->samples;  //Wave file for Tx audio
int nwave;                            //Length of Tx waveform
bool btxok;                           //True if OK to transmit
bool bTune;
bool bIQxt;
double outputLatency;                 //Latency in seconds
int txPower;
int iqAmp;
int iqPhase;
qint16 (&id)[4*60*96000] = (new Map65RxSamplesStorage{})->samples;
int pipefd[2];  // pipefd[0] = read end, pipefd[1] = write end

TxTune*    g_pTxTune = NULL;

extern const int RxDataFrequency = 96000;
extern const int TxDataFrequency = 11025;

std::atomic<bool> stop_m65{false};

QString guiDate;         //liveCQ
QStringList allDecodes;  //liveCQ
QStringList allDecodes2;  //liveCQ
QString m_otherUrl;
bool m_w3szUrl;
bool m_spot_to_psk_reporter;
bool m_psk_reporter_tcpip;

struct MainWindow::DecoderContext 
{ 
  StdoutChannel* stdoutChan; 
  DecoderContext(); 
  ~DecoderContext(); 
};

MainWindow::DecoderContext::DecoderContext()
{
    stdoutChan = new StdoutChannel(
        L"MAP65_STDOUT_MAPPING",
        L"MAP65_STDOUT_EVENT",
        64 * 1024
    );
}

MainWindow::DecoderContext::~DecoderContext()
{
    delete stdoutChan;
}

//-------------------------------------------------- MainWindow constructor
MainWindow::MainWindow(QWidget *parent) :
  QMainWindow(parent),
  ui(new Ui::MainWindow),
  m_appDir {QApplication::applicationDirPath ()},
  m_dataDir {writableMap65DataDir ()},
  m_settings_filename {map65SettingsFile (m_appDir, m_dataDir)},
  m_astro_window {new Astro {m_settings_filename}},
  m_band_map_window {new BandMap {m_settings_filename}},
  m_messages_window(nullptr),
  m_wide_graph_window {new WideGraph {m_settings_filename}},
  m_gui_timer {new QTimer {this}}
{
  qDebug() << "IN MainWindow Constructor NFFT IS: " << NFFT;
  // Legacy C++ and Fortran paths still use relative opens for runtime files.
  if (!QDir::setCurrent(m_dataDir)) {
    qWarning() << "Unable to set MAP65 working directory:" << m_dataDir;
  }
  constexpr int baseSeconds  = 56;
  constexpr int sampleRate   = 96000;
  constexpr int channels     = 4;   // dd(1..4, t)

  decoderCtx = new DecoderContext();
  startSharedMemoryStdoutReader(decoderCtx);
  
  // Worst-case: xpol = true ? 2 * baseSeconds * sampleRate I/Q pairs
  const int pairsWorst = 2 * baseSeconds * sampleRate;
  const int ddSize     = pairsWorst * channels;
  this->ddSize = ddSize;
  dd = new float[ddSize];

  // Tell Fortran about the maximum shape (channels × pairsWorst)
  set_dd_ptr(dd, channels, pairsWorst);

  std::cout << "dd pointer set to: " << dd
            << " size: " << ddSize
            << " (channels=" << channels
            << ", pairsWorst=" << pairsWorst << ")\n";
   
  ss = new float[4 * 322 * NFFT]; 
  savg = new float[4 * NFFT];
  set_ss_ptr(ss, 4, 322, NFFT);   
  std::cout << "ss pointer set to: " << ss << std::endl;
  set_savg_ptr(savg, 4, NFFT); 
  std::cout << "savg pointer set to: " << savg << std::endl; 

  qDebug() << "MAINWINDOW created dd ss savg ";

  ui->setupUi(this);
//  on_EraseButton_clicked();  //placing this here is a bug that will sometimes produce a crash on startup.
  ui->labUTC->setStyleSheet( \
        "QLabel { background-color : black; color : yellow; }");
  ui->labTol1->setStyleSheet( \
        "QLabel { background-color : white; color : black; }");
  ui->labTol1->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  ui->dxStationGroupBox->setStyleSheet("QFrame{border: 5px groove red}");

  QActionGroup* paletteGroup = new QActionGroup(this);
  ui->actionCuteSDR->setActionGroup(paletteGroup);
  ui->actionLinrad->setActionGroup(paletteGroup);
  ui->actionAFMHot->setActionGroup(paletteGroup);
  ui->actionBlue->setActionGroup(paletteGroup);

  QActionGroup* modeGroup = new QActionGroup(this);
  ui->actionNoJT65->setActionGroup(modeGroup);
  ui->actionJT65A->setActionGroup(modeGroup);
  ui->actionJT65B->setActionGroup(modeGroup);
  ui->actionJT65C->setActionGroup(modeGroup);

  QActionGroup* modeGroup2 = new QActionGroup(this);
  ui->actionNoQ65->setActionGroup(modeGroup2);
  ui->actionQ65A->setActionGroup(modeGroup2);
  ui->actionQ65B->setActionGroup(modeGroup2);
  ui->actionQ65C->setActionGroup(modeGroup2);
  ui->actionQ65D->setActionGroup(modeGroup2);
  ui->actionQ65E->setActionGroup(modeGroup2);

  QActionGroup* saveGroup = new QActionGroup(this);
  ui->actionSave_all->setActionGroup(saveGroup);
  ui->actionNone->setActionGroup(saveGroup);

  QActionGroup* DepthGroup = new QActionGroup(this);
  ui->actionNo_Deep_Search->setActionGroup(DepthGroup);
  ui->actionNormal_Deep_Search->setActionGroup(DepthGroup);
  ui->actionAggressive_Deep_Search->setActionGroup(DepthGroup);

  QButtonGroup* txMsgButtonGroup = new QButtonGroup;
  txMsgButtonGroup->addButton(ui->txrb1,1);
  txMsgButtonGroup->addButton(ui->txrb2,2);
  txMsgButtonGroup->addButton(ui->txrb3,3);
  txMsgButtonGroup->addButton(ui->txrb4,4);
  txMsgButtonGroup->addButton(ui->txrb5,5);
  txMsgButtonGroup->addButton(ui->txrb6,6);
  connect(txMsgButtonGroup,
        QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
        this,
        [this, txMsgButtonGroup](QAbstractButton *btn) {
            int id = txMsgButtonGroup->id(btn);
            set_ntx(id);
        });

  connect(ui->decodedTextBrowser,SIGNAL(selectCallsign(bool)),this,
          SLOT(selectCall2(bool)));

  // Callsign-overlay toggle (N6NU 2026-05-12, port of QMAP feature).
  // View menu action ↔ WideGraph state, two-way mirror via signal.
  if (m_wide_graph_window) {
    ui->actionShow_callsigns_on_Waterfall->setChecked(
        m_wide_graph_window->decodeLabelsEnabled());
    connect(ui->actionShow_callsigns_on_Waterfall, &QAction::toggled,
            m_wide_graph_window.data(), &WideGraph::setDecodeLabelsEnabled);
    connect(m_wide_graph_window.data(), &WideGraph::decodeLabelsEnabledChanged,
            ui->actionShow_callsigns_on_Waterfall, &QAction::setChecked);

    // Decoded-callsign overlay transparency — exclusive action group
    // (View → Callsign transparency). None=255 / Low=220 / Medium=200 /
    // High=175. Persisted under [WideGraph]/decode_label_alpha.
    QActionGroup* transparencyGroup = new QActionGroup(this);
    ui->actionTransparency_None  ->setActionGroup(transparencyGroup);
    ui->actionTransparency_Low   ->setActionGroup(transparencyGroup);
    ui->actionTransparency_Medium->setActionGroup(transparencyGroup);
    ui->actionTransparency_High  ->setActionGroup(transparencyGroup);
    {
      const int a = m_wide_graph_window->decodeLabelAlpha();
      if      (a == 175) ui->actionTransparency_High  ->setChecked(true);
      else if (a == 200) ui->actionTransparency_Medium->setChecked(true);
      else if (a == 220) ui->actionTransparency_Low   ->setChecked(true);
      else               ui->actionTransparency_None  ->setChecked(true);
    }
    auto* wg = m_wide_graph_window.data();
    connect(ui->actionTransparency_None,   &QAction::triggered,
            wg, [wg]{ wg->setDecodeLabelAlpha(255); });
    connect(ui->actionTransparency_Low,    &QAction::triggered,
            wg, [wg]{ wg->setDecodeLabelAlpha(220); });
    connect(ui->actionTransparency_Medium, &QAction::triggered,
            wg, [wg]{ wg->setDecodeLabelAlpha(200); });
    connect(ui->actionTransparency_High,   &QAction::triggered,
            wg, [wg]{ wg->setDecodeLabelAlpha(175); });

    // Decoded-callsign overlay font-size — exclusive action group
    // (View → Callsign font size). Small=7 / Normal=8 (default) /
    // Medium=10 / Large=12. Persisted via WideGraph::setDecodeLabelFontSize.
    QActionGroup* fontGroup = new QActionGroup(this);
    ui->actionCallsign_font_small ->setActionGroup(fontGroup);
    ui->actionCallsign_font_normal->setActionGroup(fontGroup);
    ui->actionCallsign_font_medium->setActionGroup(fontGroup);
    ui->actionCallsign_font_large ->setActionGroup(fontGroup);
    switch (wg->decodeLabelFontSize()) {
      case DecodeLabelFontSize::Small:
        ui->actionCallsign_font_small ->setChecked(true); break;
      case DecodeLabelFontSize::Medium:
        ui->actionCallsign_font_medium->setChecked(true); break;
      case DecodeLabelFontSize::Large:
        ui->actionCallsign_font_large ->setChecked(true); break;
      case DecodeLabelFontSize::Normal:
      default:
        ui->actionCallsign_font_normal->setChecked(true); break;
    }
    connect(ui->actionCallsign_font_small,  &QAction::triggered,
            wg, [wg]{ wg->setDecodeLabelFontSize(DecodeLabelFontSize::Small);  });
    connect(ui->actionCallsign_font_normal, &QAction::triggered,
            wg, [wg]{ wg->setDecodeLabelFontSize(DecodeLabelFontSize::Normal); });
    connect(ui->actionCallsign_font_medium, &QAction::triggered,
            wg, [wg]{ wg->setDecodeLabelFontSize(DecodeLabelFontSize::Medium); });
    connect(ui->actionCallsign_font_large,  &QAction::triggered,
            wg, [wg]{ wg->setDecodeLabelFontSize(DecodeLabelFontSize::Large);  });

    // Callsign-overlay anchor position. Top (legacy) or Bottom (sit
    // above the divider so fresh signals at the top of the waterfall
    // remain visible). Persisted under [WideGraph]/decode_label_position
    // via WideGraph::setDecodeLabelPosition.
    QActionGroup* positionGroup = new QActionGroup(this);
    ui->actionCallsign_position_top   ->setActionGroup(positionGroup);
    ui->actionCallsign_position_bottom->setActionGroup(positionGroup);
    if (wg->decodeLabelPosition() == DecodeLabelPosition::Bottom) {
      ui->actionCallsign_position_bottom->setChecked(true);
    } else {
      ui->actionCallsign_position_top   ->setChecked(true);
    }
    connect(ui->actionCallsign_position_top,    &QAction::triggered,
            wg, [wg]{ wg->setDecodeLabelPosition(DecodeLabelPosition::Top);    });
    connect(ui->actionCallsign_position_bottom, &QAction::triggered,
            wg, [wg]{ wg->setDecodeLabelPosition(DecodeLabelPosition::Bottom); });
  }

  setWindowTitle (program_title ());
  qDebug() << "MAINWINDOW about to start soundInThread SIGNAL/SLOT connections";

  connect(&soundInThread, SIGNAL(readyForFFT(int)),
             this, SLOT(dataSink(int)));
  connect(&soundInThread, SIGNAL(error(QString)), this,
          SLOT(showSoundInError(QString)));
  connect(&soundInThread, SIGNAL(status(QString)), this,
          SLOT(showStatusMessage(QString)));
  createStatusBar();
  qDebug() << "MAINWINDOW created soundInThread SIGNAL/SLOT connections";

  connect(&proc_editor, &QProcess::errorOccurred, this, &MainWindow::editor_error);

  connect(m_gui_timer, &QTimer::timeout, this, &MainWindow::guiUpdate);

  m_auto=false;
  m_waterfallAvg = 1;
  m_network = true;
  m_txFirst=false;
  m_txMute=false;
  btxok=false;
  m_restart=false;
  m_transmitting=false;
  m_widebandDecode=false;
  m_ntx=1;
  m_myCall="K1JT";
  m_myGrid="FN20qi";
  m_saveDir=QDir {m_dataDir}.absoluteFilePath("save");
  m_azelDir=m_dataDir;
  m_editorCommand="notepad";
  m_txFreq=125;
  m_setftx=0;
  m_loopall=false;
  m_saveAll=false;
  m_onlyEME=false;
  m_sec0=-1;
  m_hsym0=-1;
  m_palette="CuteSDR";
  m_map65RxLog=1;                     //Write Date and Time to all65.txt
  m_nutc0=9999;
  m_kb8rq=false;
  m_NB=false;
  m_mode="JT65B";
  m_mode65=2;
  m_fs96000=true;
  m_udpPort=50004;
  m_adjustIQ=0;
  m_applyIQcal=0;
  m_colors="000066ff0000ffff00969696646464";
  m_nsave=0;
  m_modeJT65=0;
  m_modeQ65=0;
  m_TRperiod=60;
  m_modeTx="JT65";
  bTune=false;
  txPower=100;
  iqAmp=0;
  iqPhase=0;

  xSignalMeter = new SignalMeter(ui->xMeterFrame);
  xSignalMeter->resize(50, 160);
  ySignalMeter = new SignalMeter(ui->yMeterFrame);
  ySignalMeter->resize(50, 160);

  fftwf_import_wisdom_from_filename (QDir {m_dataDir}.absoluteFilePath ("map65_wisdom.dat").toLocal8Bit ());

  readSettings();		             //Restore user's setup params
  PaError paerr=Pa_Initialize();                    //Initialize Portaudio
  if(paerr!=paNoError) {
    msgBox("Unable to initialize PortAudio.");
  }
  QFile quitFile(QDir {m_dataDir}.absoluteFilePath (".quit"));
  quitFile.remove();
    
  m_pbdecoding_style1="QPushButton{background-color: cyan; \
      border-style: outset; border-width: 1px; border-radius: 5px; \
      border-color: black; min-width: 5em; padding: 3px;}";
  m_pbmonitor_style="QPushButton{background-color: #00ff00; \
      border-style: outset; border-width: 1px; border-radius: 5px; \
      border-color: black; min-width: 5em; padding: 3px;}";
  m_pbAutoOn_style="QPushButton{background-color: red; \
      border-style: outset; border-width: 1px; border-radius: 5px; \
      border-color: black; min-width: 5em; padding: 3px;}";

  genStdMsgs("");

  on_actionAstro_Data_triggered();           //Create the other windows
  on_actionWide_Waterfall_triggered();
  on_actionBand_Map_triggered();
  
  m_band_map_window->setColors(m_colors);
  if (m_astro_window) m_astro_window->setFontSize (m_astroFont);

  if(m_modeQ65==0) on_actionNoQ65_triggered();
  if(m_modeQ65==1) on_actionQ65A_triggered();
  if(m_modeQ65==2) on_actionQ65B_triggered();
  if(m_modeQ65==3) on_actionQ65C_triggered();
  if(m_modeQ65==4) on_actionQ65D_triggered();
  if(m_modeQ65==5) on_actionQ65E_triggered();

  if(m_modeJT65==0) on_actionNoJT65_triggered();
  if(m_modeJT65==1) on_actionJT65A_triggered();
  if(m_modeJT65==2) on_actionJT65B_triggered();
  if(m_modeJT65==3) on_actionJT65C_triggered();
  future1 = new QFuture<void>;
  watcher1 = new QFutureWatcher<void>;
  connect(watcher1, SIGNAL(finished()),this,SLOT(diskDat()));
  bool ok = connect(watcher1, SIGNAL(finished()), this, SLOT(onDiskDecodeFinished()));
  qDebug() << "onDiskDecodeFinished watcher connected" << ok;

  future2 = new QFuture<void>;
  watcher2 = new QFutureWatcher<void>;
  connect(watcher2, SIGNAL(finished()),this,SLOT(diskWriteFinished()));

// Assign input device and start input thread
  soundInThread.setInputDevice(m_paInDevice);
  if(m_fs96000) soundInThread.setRate(96000.0);
  if(!m_fs96000) soundInThread.setRate(95238.1);
  soundInThread.setBufSize(10*7056);
  soundInThread.setNetwork(m_network);
  soundInThread.setPort(m_udpPort);
  if(!m_xpol) soundInThread.setNrx(1);
  if(m_xpol) soundInThread.setNrx(2);
  soundInThread.start(QThread::HighestPriority);

  // Assign output device and start output thread
  soundOutThread.setOutputDevice(m_paOutDevice);

  m_monitoring=true;                           // Start with Monitoring ON
  soundInThread.setMonitoring(m_monitoring);
  m_diskData=false;
  m_wide_graph_window->setFcal(m_fCal);
  if(m_fs96000) m_wide_graph_window->setFsample(96000);
  if(!m_fs96000) m_wide_graph_window->setFsample(95238);
  m_wide_graph_window->m_mult570=m_mult570;
  m_wide_graph_window->m_mult570Tx=m_mult570Tx;
  m_wide_graph_window->m_cal570=m_cal570;
  m_wide_graph_window->m_TxOffset=m_TxOffset;
  if(m_initIQplus) m_wide_graph_window->initIQplus();

// Create "m_worked", a dictionary of all calls in wsjt.log
  QFile f("wsjt.log");
  qDebug() << "MainWindow Constructor File open result:" << f.open(QFileDevice::ReadOnly);
  if(f.isOpen()) {
    QTextStream in(&f);
    QString line,t,callsign;
    for(int i=0; i<99999; i++) {
      line=in.readLine();
      if(line.length()<=0) break;
      t=line.mid(18,12);
      callsign=t.mid(0,t.indexOf(","));
      m_worked[callsign]=true;
    }
    f.close();
  }

  if(ui->actionLinrad->isChecked()) on_actionLinrad_triggered();
  if(ui->actionCuteSDR->isChecked()) on_actionCuteSDR_triggered();
  if(ui->actionAFMHot->isChecked()) on_actionAFMHot_triggered();
  if(ui->actionBlue->isChecked()) on_actionBlue_triggered();

  connect (m_wide_graph_window.get (), &WideGraph::freezeDecode2, this, &MainWindow::freezeDecode);
  connect (m_wide_graph_window.get (), &WideGraph::f11f12, this, &MainWindow::bumpDF);

  QTimer::singleShot (0, this,[this]() {
    m_messages_window = new Messages(m_settings_filename);
    m_messages_window->show();    
    on_actionMessages_triggered();
    connect (m_messages_window, &Messages::click2OnCallsign, this, &MainWindow::doubleClickOnMessages);
    if (m_messages_window) m_messages_window->setColors(m_colors);
  });
  
   QTimer::singleShot (2000, [=] {    
    setNhsym(1);
   });
   QTimer::singleShot (4000, [=] {       
    qDebug() << "MAINWINDOW reads Fortran fcenter as: " << getFcenter();
   });
  
  //default freq at startup for Doppler and Tsky  
  setFcenter(m_wide_graph_window->m_dForceCenterFreq);
  if( getFcenter() == 0) setFcenter(144.125);
  
  qDebug() << "MAINWINDOW reached end of Constructor";
  // only start the guiUpdate timer after this constructor has finished
  QTimer::singleShot (0, [=] {
           m_gui_timer->start(100); //Don't change the 100 ms!
         });
         
  // Start Run_m65       
  QTimer::singleShot(0, this, SLOT(startDecoder()));
}

  //--------------------------------------------------- MainWindow destructor
MainWindow::~MainWindow()
{
  // Stop stdout reader thread
  stdoutReaderStop.store(true);

  // Wake event so thread exits immediately
  if (decoderCtx && decoderCtx->stdoutChan) {
      void* ev = decoderCtx->stdoutChan->eventHandle;
      if (ev)
          win_set_event(ev);
  }

  if (stdoutReaderThread.joinable())
      stdoutReaderThread.join();
  writeSettings();
  if (soundInThread.isRunning()) {
    soundInThread.quit();
    soundInThread.wait(3000);
  }
  if (soundOutThread.isRunning()) {
    soundOutThread.quitExecution=true;
    soundOutThread.wait(3000);
  }
  Pa_Terminate();
  fftwf_export_wisdom_to_filename (QDir {m_dataDir}.absoluteFilePath ("map65_wisdom.dat").toLocal8Bit ());
  delete ui;
}

void MainWindow::startDecoder()
{
    qDebug() << "MAINWINDOW calling run_m65_ ";

    QByteArray runtimeDir = QDir::toNativeSeparators(m_dataDir).toLocal8Bit();
    set_wsjtx_dir_(runtimeDir.constData(), runtimeDir.size());

    QFutureWatcher<void>* watcher_m65 = new QFutureWatcher<void>(this);
    connect(watcher_m65, &QFutureWatcher<void>::finished,
            this, &MainWindow::onRunM65Finished);

    watcher_m65->setFuture(QtConcurrent::run([=]() {
        // Hook up shared stdout channel for Fortran
        StdoutSharedRegion* region =
            decoderCtx->stdoutChan->shared.getRegion();

        void*    bufPtr  = static_cast<void*>(region->buffer);
        void*    hdrPtr  = static_cast<void*>(&region->header);
        int      bufSize = static_cast<int>(decoderCtx->stdoutChan->shared.getBufferSize());
        intptr_t eventH  = reinterpret_cast<intptr_t>(decoderCtx->stdoutChan->eventHandle);

        set_stdout_channel(bufPtr, hdrPtr, bufSize, eventH);

        // Now run the decoder
        std::lock_guard<std::mutex> lock(g_fortran_decode_mutex);
        int xpol_flag = m_xpol ? 1 : 0;
        int rate_flag = m_fs96000 ? 1 : 0;
        run_m65_(&xpol_flag, &rate_flag);

    }));
}

void MainWindow::startSharedMemoryStdoutReader(DecoderContext* ctx)
{
    stdoutReaderThread = std::thread([this, ctx]() {

      StdoutSharedRegion* region =
          ctx->stdoutChan->shared.getRegion();

      char* bufferBase =
          reinterpret_cast<char*>(region->buffer);

      std::size_t bufSize =
          ctx->stdoutChan->shared.getBufferSize();

        // Linux: eventHandle is a void* pointing to posix_event_t
        void* ev = ctx->stdoutChan->eventHandle;

      // NEW: start reading from the current writeIndex
      StdoutSharedHeader h0 = region->header;
      std::uint32_t readIndex = h0.writeIndex;
      if (readIndex >= bufSize)
          readIndex = 0;

      std::string lineBuffer;

      while (!stdoutReaderStop.load()) {

            // Linux replacement for WaitForSingleObject(ev, INFINITE)
            win_wait_for_single_object(ev);

            // After wakeup, read new data
          StdoutSharedHeader h = region->header;
          std::uint32_t writeIndex = h.writeIndex;
          if (writeIndex >= bufSize)
              writeIndex = 0;

          while (readIndex != writeIndex) {
              char c = bufferBase[readIndex];
              readIndex++;
              if (readIndex >= bufSize)
                  readIndex = 0;

              lineBuffer.push_back(c);
              if (c == '\n') {
                  std::string line = lineBuffer;
                  lineBuffer.clear();

                  QString text = QString::fromStdString(line);
                  QMetaObject::invokeMethod(
                      this,
                      [this, text]() { processStdOut(text); },
                      Qt::QueuedConnection
                  );
              }
          }
      }

    });
}

void MainWindow::processStdOut(QString t)
{  
 
//  qDebug().noquote() << QDateTime::currentMSecsSinceEpoch() << "PROCESS STDOUT:" << t;

  //qDebug() << "in processStdOut STDOUT:" << t;
if (t.indexOf("<QuickDecodeDone>") >= 0) {

  // Any decoder output means "not idle" — reset idle timer in auto/disk mode
 
      m_nsum  = t.mid(17,4).toInt();
      m_nsave = t.mid(21,4).toInt();
      lab7->setText(QString{"Avg: %1"}.arg(m_nsum));
    if (m_modeQ65 > 0)
        m_wide_graph_window->setDecodeFinished();
  }

    // --- <EarlyFinished> / <DecodeFinished> ---
  if (t.indexOf("<EarlyFinished>") >= 0 || t.indexOf("<DecodeFinished>") >= 0) {

    if (m_widebandDecode) {
        if (m_messages_window)
            m_messages_window->setText(m_messagesText, m_bandmapText);
        if (m_band_map_window)
            m_band_map_window->setText(m_bandmapText);
        m_widebandDecode = false;
    }
    if (t.indexOf("<DecodeFinished>") >= 0) {
        ++m_decodeFinishedCount;

          decodeBusy(false); 
//      qDebug().noquote() << QDateTime::currentMSecsSinceEpoch() << "decodeBusy(false)";
      if (m_diskData) onDiskDecodeFinished();

        int ndecodes = t.mid(40,5).toInt();
        lab5->setText(QString::number(ndecodes));
        m_map65RxLog   = 0;        
    }

    ui->DecodeButton->setStyleSheet("");
    return;
}

    // --- same position as legacy ---
    read_log();

    // --- "!" decoded text lines ---
    if (t.startsWith("!")) {
        int n = t.length();
        int m = 2;
#ifdef WIN32
        m = 3;
#endif
        const QString decode_line = t.mid(1, n - m);

         // Suppress *second* narrowband GUI entry, but keep everything else
        if (m_RxState != 2 || m_diskData) {
          if (n >= 30 || t.indexOf("Best-fit") >= 0)
            ui->decodedTextBrowser->append(decode_line);
        }
        
        int max = ui->decodedTextBrowser->verticalScrollBar()->maximum();
        ui->decodedTextBrowser->verticalScrollBar()->setValue(max);

        // Callsign-overlay tap (N6NU 2026-05-13, DG2YCB feedback round 4).
        // Two stdout-write paths for decodes (q65b.f90:227, map65a.f90:430)
        // share enough format that we can parse them together:
        //   JT65: ("!",I3,I5,I4,I6.4,F5.1,I5,1X,A1,1X,A22,I2,I5,I5,1X,A1)
        //   Q65 : ("!",I3.3,I5,I4,I6.4,F5.1,I5," : ",A28,A3,I4,1X,A1)
        // The Q65 line is the only one that contains a literal " : " — use
        // that as the mode discriminator (right(2) does NOT work: both
        // lines end in 1X+A1 = " cp", just a single status char).
        //
        // Q65 stdout writes are guarded by an in-tolerance-of-mouse check
        // (q65b.f90:226), so most off-target Q65 decodes never reach this
        // tap. The "&" bandmap handler below covers them as a fallback.
        if (m_wide_graph_window) {
            const QString trimmed = decode_line.trimmed();
            const int sep         = decode_line.indexOf(" : ");
            const bool is_jt65    = (sep < 0);  // " : " present ⇒ Q65

            // The "!" decode line packs the audio frequency into TWO
            // columns per the Fortran writes at map65a.f90:387 and
            // q65b.f90:227 — format ("!",I3,I5,...) where nkHz is the
            // integer kHz and ndf is the signed delta-Hz within that
            // kHz, so true freq = nkHz + ndf/1000.0. The previous
            // "first token that parses as a number in [0,1e6)" approach
            // dropped ndf (off by ±500 Hz for small nkHz) or worse,
            // picked ndf itself when nkHz ≥ 100 made the right-
            // justified I3 collide with the leading "!" (the parser
            // then read ndf in Hz as kHz).
            QString rest = trimmed;
            if (rest.startsWith('!')) rest = rest.mid(1);
            const QStringList all_cols = rest.split(
                QRegularExpression("\\s+"),SkipEmptyParts);
            double freq_khz = -1.0;
            if (all_cols.size() >= 2) {
                bool ok1 = false, ok2 = false;
                const int nkHz = all_cols[0].toInt(&ok1);
                const int ndf  = all_cols[1].toInt(&ok2);
                if (ok1 && ok2) freq_khz = nkHz + ndf / 1000.0;
            }

            QString body;
            if (sep > 0) {
                body = decode_line.mid(sep + 3).trimmed();
            } else if (decode_line.size() > 31) {
                body = decode_line.mid(31).trimmed();
            }
            const QStringList msg_cols = body.split(
                QRegularExpression("\\s+"),SkipEmptyParts);
            QString sender;
            if (msg_cols.size() >= 2) {
                if (msg_cols[0] == "CQ") {
                    if (msg_cols.size() >= 3 && msg_cols[1] == "DX") sender = msg_cols[2];
                    else                                              sender = msg_cols[1];
                } else {
                    sender = msg_cols[1];   // directed: TO_call FROM_call
                }
            }
            static const QRegularExpression call_re(
                "^[A-Z0-9]{1,3}[0-9][A-Z0-9]{0,3}[A-Z](/[A-Z0-9]+)?$");
            if (freq_khz > 0 && !sender.isEmpty()
                && call_re.match(sender.toUpper()).hasMatch()) {
                m_wide_graph_window->addDecodeLabel(freq_khz, sender, is_jt65);
            }
        }

        // clear snapshots for this decode run, just like legacy
        m_messagesText.clear();
        m_bandmapText.clear();
    }

    // --- "@" message lines ---
    if (t.startsWith("@")) {
        m_messagesText += t.mid(1);
        m_widebandDecode = true;
    }

    // --- "&" bandmap lines ---
    // N6NU 2026-05-24: format widened to include the 5-char ndf from
    // line3(k)(9:13). New layout:
    //   "&" + I3 kHz + I5 ndf + " " + A6 call + A2 age
    // Old layout (pre-260524, used by stock map65):
    //   "&" + I3 kHz + " " + A6 call + A2 age
    // We auto-detect by checking column 4: if it's a digit/space-of-int,
    // it's the new format. Old format has the space-separator there.
    if (t.startsWith("&")) {
        // Detect format. New: chars 4..8 are an int (ndf). Old: char 4
        // is a space and chars 5..10 are the callsign.
        const QString ndf_field = t.mid(4, 5);
        bool ndf_ok = false;
        const int ndf_hz = ndf_field.trimmed().toInt(&ndf_ok);
        const int call_start = ndf_ok ? 10 : 5;

        QString q(t);
        QString callsign = q.mid(call_start);
        callsign = callsign.mid(0, callsign.indexOf(" "));
        if (callsign.length() > 2) {
            if (m_worked[callsign]) {
                q = q.mid(1,4) + "  " + q.mid(call_start);
            } else {
                q = q.mid(1,4) + " *" + q.mid(call_start);
            }
            m_bandmapText += q;

            // Fallback overlay tap (N6NU 2026-05-13, DG2YCB feedback r4).
            // We can't know the mode from this line, so pass is_jt65=false
            // and mode_reliable=false. With the new format we now also
            // have ndf precision, so freq_reliable=true. Old format
            // callers still pass freq_reliable=false (integer kHz only).
            if (m_wide_graph_window) {
                bool ok_khz = false;
                const int nkHz = t.mid(1, 3).trimmed().toInt(&ok_khz);
                const double freq_khz = ndf_ok
                    ? (nkHz + ndf_hz / 1000.0)
                    : double(nkHz);
                static const QRegularExpression call_re(
                    "^[A-Z0-9]{1,3}[0-9][A-Z0-9]{0,3}[A-Z](/[A-Z0-9]+)?$");
                if (ok_khz && freq_khz > 0
                    && call_re.match(callsign.toUpper()).hasMatch()) {
                    m_wide_graph_window->addDecodeLabel(
                        freq_khz, callsign, /*is_jt65=*/false,
                        /*mode_reliable=*/false,
                        /*freq_reliable=*/ndf_ok);
                }
            }
        }
    }

    // --- "=" debug lines ---
    if (t.startsWith("=")) {
        int n = t.size();
        qDebug() << t.mid(1, n - 3).trimmed();
    }
    // --- END OF processStdOut ---
}

void MainWindow::onRunM65Finished() {
    // Do something when run_m65 finishes
}

void MainWindow::onDiskDecodeFinished()
{
    if (!m_path.isEmpty())
        setWindowTitle("MAP65  -  " + QFileInfo(m_path).fileName());

    if (m_loopall)
        on_actionOpen_next_in_directory_triggered();
}

//-------------------------------------------------------- writeSettings()
void MainWindow::writeSettings()
{
  QSettings settings(m_settings_filename, QSettings::IniFormat);
  {
    SettingsGroup g {&settings, "MainWindow"};
    settings.setValue("geometry", saveGeometry());
    settings.setValue("MRUdir", m_path);
    settings.setValue("TxFirst",m_txFirst);
    settings.setValue("DXcall",ui->dxCallEntry->text());
    settings.setValue("DXgrid",ui->dxGridEntry->text());
  }

  {
  SettingsGroup g {&settings, "Common"};
  settings.setValue("MyCall",m_myCall);
  settings.setValue("MyGrid",m_myGrid);
  settings.setValue("IDint",m_idInt);
  settings.setValue("PTTpath",m_pttPath);
  settings.setValue("PTTPortNumber",m_pttPortNumber);
  settings.setValue("AstroFont",m_astroFont);
  settings.setValue("Xpol",m_xpol);
  settings.setValue("XpolX",m_xpolx);
  settings.setValue("SaveDir",m_saveDir);
  settings.setValue("AzElDir",m_azelDir);
  settings.setValue("Editor",m_editorCommand);
  settings.setValue("DXCCpfx",m_dxccPfx);
  settings.setValue("Timeout",m_timeout);
  settings.setValue("ApplyIQcal",m_applyIQcal);
  settings.setValue("dPhi",m_dPhi);
  settings.setValue("Fcal",m_fCal);
  settings.setValue("Fadd",m_fAdd);
  settings.setValue("NetworkInput", m_network);
  settings.setValue("FSam96000", m_fs96000);
  settings.setValue("SoundInIndex",m_nDevIn);
  settings.setValue("paInDevice",m_paInDevice);
  settings.setValue("SoundOutIndex",m_nDevOut);
  settings.setValue("paOutDevice",m_paOutDevice);
  settings.setValue("IQswap",m_IQswap);
  settings.setValue("Scale_dB",m_dB);
  settings.setValue("IQxt",m_bIQxt);
  settings.setValue("InitIQplus",m_initIQplus);
  settings.setValue("UDPport",m_udpPort);
  settings.setValue("PaletteCuteSDR",ui->actionCuteSDR->isChecked());
  settings.setValue("PaletteLinrad",ui->actionLinrad->isChecked());
  settings.setValue("PaletteAFMHot",ui->actionAFMHot->isChecked());
  settings.setValue("PaletteBlue",ui->actionBlue->isChecked());
  settings.setValue("Mode",m_mode);
  settings.setValue("nModeJT65",m_modeJT65);
  settings.setValue("nModeQ65",m_modeQ65);
  settings.setValue("TxMode",m_modeTx);
  settings.setValue("SaveNone",ui->actionNone->isChecked());
  settings.setValue("SaveAll",ui->actionSave_all->isChecked());
  settings.setValue("NDepth",m_ndepth);
  settings.setValue("NEME",m_onlyEME);
  settings.setValue("KB8RQ",m_kb8rq);
  settings.setValue("NB",m_NB);
  settings.setValue("NBslider",m_NBslider);
  settings.setValue("GainX",(double)m_gainx);
  settings.setValue("GainY",(double)m_gainy);
  settings.setValue("PhaseX",(double)m_phasex);
  settings.setValue("PhaseY",(double)m_phasey);
  settings.setValue("Mult570",m_mult570);
  settings.setValue("Mult570Tx",m_mult570Tx);
  settings.setValue("Cal570",m_cal570);
  settings.setValue("TxOffset",m_TxOffset);
  settings.setValue("Colors",m_colors);
  settings.setValue("MaxDrift",ui->sbMaxDrift->value());
  settings.setValue("w3szUrl",m_w3szUrl); //liveCQ
  settings.setValue("otherUrl",m_otherUrl); //liveCQ
  settings.setValue("spotPSK",m_spot_to_psk_reporter);
  settings.setValue("PSKReporterTCPIP",m_psk_reporter_tcpip);
  settings.setValue("FTol",m_tol);
	settings.endGroup();
  }
  
  {
	settings.beginGroup("TxTune");
	settings.setValue("geometry", g_TxTuneGeometry);
	settings.setValue("TxPower",txPower);
	settings.setValue("IQamp",iqAmp);
	settings.setValue("IQphase",iqPhase);
	settings.endGroup();  
  }
}

//---------------------------------------------------------- readSettings()
void MainWindow::readSettings()
{
  QSettings settings(m_settings_filename, QSettings::IniFormat);
  {
    SettingsGroup g {&settings, "MainWindow"};
    restoreGeometry(settings.value("geometry").toByteArray());
    ui->dxCallEntry->setText(settings.value("DXcall","").toString());
    ui->dxGridEntry->setText(settings.value("DXgrid","").toString());
    m_path = settings.value("MRUdir", QDir {m_dataDir}.absoluteFilePath("save")).toString();
    m_txFirst = settings.value("TxFirst",false).toBool();
    ui->txFirstCheckBox->setChecked(m_txFirst);
  }

  {
  SettingsGroup g {&settings, "Common"};
  m_myCall=settings.value("MyCall","").toString();
  m_myGrid=settings.value("MyGrid","").toString();
  m_idInt=settings.value("IDint",0).toInt();
  m_pttPath=settings.value("PTTpath",0).toString();
  m_pttPortNumber = settings.value("PTTPortNumber",0).toInt();
  #if !defined(Q_OS_WIN)
    ptt_set_override(m_pttPath.toUtf8().constData());
  #endif
  m_astroFont=settings.value("AstroFont",20).toInt();
  m_xpol=settings.value("Xpol",false).toBool();
  ui->actionFind_Delta_Phi->setEnabled(m_xpol);
  m_xpolx=settings.value("XpolX",false).toBool();
  m_saveDir=settings.value("SaveDir",QDir {m_dataDir}.absoluteFilePath("save")).toString();
  m_azelDir=settings.value("AzElDir",m_dataDir).toString();
  m_editorCommand=settings.value("Editor","notepad").toString();
  m_dxccPfx=settings.value("DXCCpfx","").toString();
  m_timeout=settings.value("Timeout",20).toInt();
  m_applyIQcal=settings.value("ApplyIQcal",0).toInt();
  ui->actionApply_IQ_Calibration->setChecked(m_applyIQcal!=0);
  m_dPhi=settings.value("dPhi",0).toInt();
  m_fCal=settings.value("Fcal",0).toInt();
  m_fAdd=settings.value("Fadd",0).toDouble();
  soundInThread.setFadd(m_fAdd);
  m_network = settings.value("NetworkInput",true).toBool();
  m_fs96000 = settings.value("FSam96000",true).toBool();
  m_nDevIn = settings.value("SoundInIndex", 0).toInt();
  m_paInDevice = settings.value("paInDevice",0).toInt();
  m_nDevOut = settings.value("SoundOutIndex", 0).toInt();
  m_paOutDevice = settings.value("paOutDevice",0).toInt();
  m_IQswap = settings.value("IQswap",false).toBool();
  m_dB = settings.value("Scale_dB",0).toInt();
  m_initIQplus = settings.value("InitIQplus",false).toBool();
  m_bIQxt = settings.value("IQxt",false).toBool();
  m_udpPort = settings.value("UDPport",50004).toInt();
  soundInThread.setSwapIQ(m_IQswap);
  soundInThread.setScale(m_dB);
  soundInThread.setPort(m_udpPort);
  ui->actionCuteSDR->setChecked(settings.value(
                                  "PaletteCuteSDR",true).toBool());
  ui->actionLinrad->setChecked(settings.value(
                                 "PaletteLinrad",false).toBool());
  m_mode=settings.value("Mode","JT65B").toString();
  m_modeJT65=settings.value("nModeJT65",2).toInt();
  if(m_modeJT65==0) ui->actionNoJT65->setChecked(true);
  if(m_modeJT65==1) ui->actionJT65A->setChecked(true);
  if(m_modeJT65==2) ui->actionJT65B->setChecked(true);
  if(m_modeJT65==3) ui->actionJT65C->setChecked(true);

  m_modeQ65=settings.value("nModeQ65",2).toInt();
  m_modeTx=settings.value("TxMode","JT65").toString();
  if(m_modeQ65==0) ui->actionNoQ65->setChecked(true);
  if(m_modeQ65==1) ui->actionQ65A->setChecked(true);
  if(m_modeQ65==2) ui->actionQ65B->setChecked(true);
  if(m_modeQ65==3) ui->actionQ65C->setChecked(true);
  if(m_modeQ65==4) ui->actionQ65D->setChecked(true);
  if(m_modeQ65==5) ui->actionQ65E->setChecked(true);
  if(m_modeTx=="JT65")  ui->pbTxMode->setText("Tx JT65   #");
  if(m_modeTx=="Q65") ui->pbTxMode->setText("Tx Q65  :");

  ui->actionNone->setChecked(settings.value("SaveNone",true).toBool());
  ui->actionSave_all->setChecked(settings.value("SaveAll",false).toBool());
  m_saveAll=ui->actionSave_all->isChecked();
  m_ndepth=settings.value("NDepth",0).toInt();
  m_onlyEME=settings.value("NEME",false).toBool();
  ui->actionOnly_EME_calls->setChecked(m_onlyEME);
  m_kb8rq=settings.value("KB8RQ",false).toBool();
  ui->actionF4_sets_Tx6->setChecked(m_kb8rq);
  m_NB=settings.value("NB",false).toBool();
  ui->NBcheckBox->setChecked(m_NB);
  ui->sbMaxDrift->setValue(settings.value("MaxDrift",0).toInt());
  m_NBslider=settings.value("NBslider",40).toInt();
  ui->NBslider->setValue(m_NBslider);
  m_gainx=settings.value("GainX",1.0).toFloat();
  m_gainy=settings.value("GainY",1.0).toFloat();
  m_phasex=settings.value("PhaseX",0.0).toFloat();
  m_phasey=settings.value("PhaseY",0.0).toFloat();
  m_mult570=settings.value("Mult570",2).toInt();
  m_mult570Tx=settings.value("Mult570Tx",1).toInt();
  m_cal570=settings.value("Cal570",0.0).toDouble();
  m_TxOffset=settings.value("TxOffset",130.9).toDouble();
  m_colors=settings.value("Colors","000066ff0000ffff00969696646464").toString();

  if(!ui->actionLinrad->isChecked() && !ui->actionCuteSDR->isChecked() &&
    !ui->actionAFMHot->isChecked() && !ui->actionBlue->isChecked()) {
    on_actionLinrad_triggered();
    ui->actionLinrad->setChecked(true);
  }
  if(m_ndepth==0) ui->actionNo_Deep_Search->setChecked(true);
  if(m_ndepth==1) ui->actionNormal_Deep_Search->setChecked(true);
  if(m_ndepth==2) ui->actionAggressive_Deep_Search->setChecked(true);
  m_w3szUrl=settings.value("w3szUrl",true).toBool();
  m_otherUrl=settings.value("otherUrl","").toString();
  m_spot_to_psk_reporter=settings.value("spotPSK",true).toBool();
  m_psk_reporter_tcpip=settings.value("PSKReporterTCPIP",false).toBool();

  m_tol=settings.value("FTol",500).toInt();
  m_wide_graph_window->setTol(m_tol);
  int i = 5;
  if(m_tol==20) i=1;
  if(m_tol==50) i=2;
  if(m_tol==100) i=3;
  if(m_tol==200) i=4;
//  if(m_tol==500) i=5;
  if(m_tol==1000) i=6;
  ui->labTol1->setText(QString::number(m_tol));
  ui->tolSpinBox->setValue(i);

  qDebug() << "In mainwindow m_spot_to_psk_reporter is: " << m_spot_to_psk_reporter;

  qDebug() << "In mainwindow m_modeTx is: " << m_modeTx;
  qDebug() << "In mainwindow m_mode is: " << m_mode;
  qDebug() << "In mainwindow n_modeJT65 is: " << m_modeJT65;
  qDebug() << "In mainwindow n_modeQ65 is: " << m_modeQ65;
	settings.endGroup();
  }
  
  {
	settings.beginGroup("TxTune");
	g_TxTuneGeometry = settings.value("geometry").toByteArray();
	txPower=settings.value("TxPower",100).toInt();
	iqAmp=settings.value("IQamp",0).toInt();
	iqPhase=settings.value("IQphase",0).toInt();
	settings.endGroup();
  }
}

//-------------------------------------------------------------- dataSink()
void MainWindow::dataSink(int k)
{
  static float s[NFFT],splot[NFFT];
  static int n=0;
  static int ihsym=0;
  static int nzap=0;
  static int ntrz=0;
  static int nkhz;
  int nfsample= m_fs96000 ? 96000 : 95238;
  setNfsample(nfsample);
  int nxpol= m_xpol ? 1 : 0;
  setNxpol(nxpol);
  static int nsec0=0;
  static int nsum=0;
  static int ndiskdat;
  static int nb;
  static int nadj=0;
  static float px=0.0,py=0.0;
  static uchar lstrong[1024];
  static float rejectx;
  static float rejecty;
  static float slimit;
  static double xsum=0.0;
  
  if(m_diskData) {
    ndiskdat=1;
      setNdiskdat(1);
  } else {
    ndiskdat=0;
      setNdiskdat(0);
  }

  setIdphi(m_dPhi);
// Get x and y power, polarized spectrum, nkhz, and ihsym
  nb=0;
  if(m_NB) nb=1;
  nfsample=96000;
  if(!m_fs96000) nfsample=95238;
  nxpol=0;
  if(m_xpol) nxpol=1;
  nadj++;
  if(m_adjustIQ==0) nadj=0;
    
  symspec_(&k, &nxpol, &ndiskdat, &nb, &m_NBslider, &m_dPhi,
           &nfsample, &m_adjustIQ, &m_applyIQcal,
           &m_gainx, &m_gainy, &m_phasex, &m_phasey, &rejectx, &rejecty,
           &px, &py, s, &nkhz, &ihsym, &nzap, &slimit, lstrong);

  int nsec=QDateTime::currentSecsSinceEpoch();
  if(nsec==nsec0) {
    xsum+=pow(10.0,0.1*px);
    nsum+=1;
  } else {
    m_xavg=0.0;
    if(nsum>0) m_xavg=xsum/nsum;
    xsum=pow(10.0,0.1*px);
    nsum=1;
  }
  nsec0=nsec;

  QString t;
  m_pctZap=nzap/178.3;
  ui->yMeterFrame->setVisible(m_xpol);
  if(m_xpol) {
    lab4->setText (
                  QString {" Rx noise: %1  %2 %3 %% "}
                     .arg (px, 5, 'f', 1)
                     .arg (py, 5, 'f', 1)
                     .arg (m_pctZap, 5, 'f', 1)
                  );
  } else {
    lab4->setText (
                  QString {" Rx noise: %1  %2 %% "}
                  .arg (px, 5, 'f', 1)
                  .arg (m_pctZap, 5, 'f', 1)
                  );
  }
  xSignalMeter->setValue(px);                   // Update the signal meters
  ySignalMeter->setValue(py);
  if(m_monitoring || m_diskData) {
    m_wide_graph_window->dataSink2(s,nkhz,ihsym,m_diskData,lstrong);
  }

  if(nadj == 10) {
    if(m_xpol) {
      ui->decodedTextBrowser->append (
                                      QString {"Amp: %1 %2   Phase: %3 %4"}
                                         .arg (m_gainx, 6, 'f', 4).arg (m_gainy, 6, 'f', 4)
                                         .arg (m_phasex, 6, 'f', 4)
                                         .arg (m_phasey, 6, 'f', 4)
                                      );
    } else {
      ui->decodedTextBrowser->append(
                                     QString {"Amp: %1   Phase: %2"}
                                        .arg (m_gainx, 6, 'f', 4)
                                        .arg (m_phasex, 6, 'f', 4)
                                     );
    }
    ui->decodedTextBrowser->append(t);
    m_adjustIQ=0;
  }

  //Average over specified number of spectra
  if (n==0) {
    for (int i=0; i<NFFT; i++)
      splot[i]=s[i];
  } else {
    for (int i=0; i<NFFT; i++)
      splot[i] += s[i];
  }
  n++;

  if (n>=m_waterfallAvg) {
    for (int i=0; i<NFFT; i++) {
        splot[i] /= n;                           //Normalize the average
    }

// Time according to this computer
    qint64 ms = QDateTime::currentMSecsSinceEpoch() % 86400000;
    int ntr1 = (ms/1000) % m_TRperiod;
    if((m_diskData && ihsym <= m_waterfallAvg) || (!m_diskData && ntr1<ntrz)) {
      for (int i=0; i<NFFT; i++) {
        splot[i] = 1.e30;
      }
    }
    ntrz=ntr1;
    n=0;
  }

  if(ihsym<280) m_RxState=0;

  if(m_RxState==0 and ihsym>=280 and !m_diskData) {   //Early decode, t=52 s
    m_RxState=1;
    setDecoderReady(0);
    setNewdat(1);
    setNagain(0);
    setNhsym(ihsym);
    QDateTime t = QDateTime::currentDateTimeUtc();
    m_dateTime=t.toString("yyyy-MMM-dd hh:mm");
    decode();                                           //Start the decoder
  //  qDebug() << "decoding at t=52s in dataSink in mainwindow.cpp";
  }

  if(m_RxState<=1 and ihsym>=302) {   //Decode at t=56 s (for Q65 and data from disk)
    m_RxState=2;
    setDecoderReady(0);
    setNewdat(1);
    setNagain(0);
    setNhsym(ihsym);
    QDateTime t = QDateTime::currentDateTimeUtc();
    m_dateTime=t.toString("yyyy-MMM-dd hh:mm");
    decode();                                           //Start the decoder
    if(m_saveAll and !m_diskData) {
      QString fname=m_saveDir + "/" + t.date().toString("yyMMdd") + "_" +
          t.time().toString("hhmm");
      if(m_xpol) fname += ".tf2";
      if(!m_xpol) fname += ".iq";
      *future2 = QtConcurrent::run([this](QString fname, bool xpol) {
        this->savetf2(fname, xpol);
        }, fname, m_xpol);        

      qDebug() << "saving to file " << fname << " in dataSink in mainwindow.cpp";
      watcher2->setFuture(*future2);
    }
  }
  soundInThread.m_dataSinkBusy=false;
}

/* Generate gaussian random float with mean=0 and std_dev=1 */
float gran()
{
  float fac,rsq,v1,v2;
  static float gset;
  static int iset;

  if(iset){
    /* Already got one */
    iset = 0;
    return gset;
  }
  /* Generate two evenly distributed numbers between -1 and +1
   * that are inside the unit circle
   */
  do {
    v1 = 2.0 * (float)rand() / RAND_MAX - 1;
    v2 = 2.0 * (float)rand() / RAND_MAX - 1;
    rsq = v1*v1 + v2*v2;
  } while(rsq >= 1.0 || rsq == 0.0);
  fac = sqrt(-2.0*log(rsq)/rsq);
  gset = v1*fac;
  iset++;
  return v2*fac;
}

float* MainWindow::getDd() const
{
    return dd;
}

void MainWindow::savetf2(QString fname, bool xpol)
{
  int npts=2*56*96000;
  if(xpol) npts=2*npts;
  FILE* fp = fopen(fname.toUtf8().constData(), "wb");

  if (!fp) {
    qDebug() << "FAILED TO OPEN FILE:" << fname;
    perror("fopen");
    return;
  }

  qint16* buf = static_cast<qint16*>(malloc(npts * sizeof(*buf)));

  if(fp != NULL) {
    double fcenter = getFcenter();
      //fcenter = 1296.125;
    fwrite(&fcenter, sizeof(fcenter), 1, fp);  // Write fcenter to file
    uint64_t zero = 0;
    fwrite(&zero, sizeof(zero), 1, fp);         // another 8 bytes
    qDebug() << "in savetf2 fcenter is: " << fcenter;

    int j = 0;
    for (int i = 0; i < npts; i += 2) {
      buf[i]     = static_cast<qint16>(dd[j++]);
      buf[i + 1] = static_cast<qint16>(dd[j++]);
      if (!xpol) j += 2;  // Skip over dd(3,x) and dd(4,x)
    }

    fwrite(buf, sizeof(buf[0]), npts, fp);
    qDebug() << "saving file " << fname << " to disk in savetf2 in mainwindow.cpp";
    fclose(fp);
  }
  free(buf);
}

void MainWindow::getfile(QString fname, bool xpol, int dbDgrd)
{
    setDecoderReady(0);
    int npts = 2 * 56 * 96000;
    if (xpol) npts = 2 * npts;

    // j indexes dd[], which has 4 channels per pair
    int j = 0;

    // Clear id[] properly
    memset(id, 0, npts * sizeof(id[0]));

    // Open file
    FILE* fp = fopen(fname.toUtf8().constData(), "rb");
    if (!fp) {
        qWarning() << "Failed to open file:" << fname;
        return;
    }

    // Read fcenter
    double fcenter = 0.0;
    if (fread(&fcenter, sizeof(fcenter), 1, fp) != 1) {
		fclose(fp);
		return;
	}

    setFcenter(fcenter);

    // Skip the 8-byte zero padding
    uint64_t pad = 0;
    if (fread(&pad, sizeof(pad), 1, fp) != 1) {
		fclose(fp);
		return;
	}


    // Read raw samples
    size_t nRead = fread(id, sizeof(id[0]), npts, fp);
    qDebug() << "fread read" << nRead << "samples, expected" << npts;

    fclose(fp);
    
    // Compute degradation factors
    float dgrd = 0.0;
    if (dbDgrd < 0)
        dgrd = 23.0 * sqrt(pow(10.0, -0.1 * (double)dbDgrd) - 1.0);

    float fac = 23.0 / sqrt(dgrd * dgrd + 23.0 * 23.0);

    // Fill dd[]
    j = 0;
    for (int i = 0; i < npts; i += 2) {

        if (dbDgrd < 0) {
            dd[j++] = fac * ((float)id[i]     + dgrd * gran());
            dd[j++] = fac * ((float)id[i + 1] + dgrd * gran());
        } else {
            dd[j++] = id[i];
            dd[j++] = id[i + 1];
        }

        if (!xpol) {
            // Skip channels 3 and 4
            j += 2;

        }
    }
     
  //  qDebug() << "GETFILE: starting decode for:" << fname;
    setNdiskdat(1);
    int nfreq = getFcenter();
    if (nfreq > 9998) setFcenter(9990.100);

    int i0 = fname.indexOf(".tf2");
    if (i0 < 0) i0 = fname.indexOf(".iq");

    setNutc(0);
    if (i0 > 0) {
        setNutc(100 * fname.mid(i0 - 4, 2).toInt() +
                fname.mid(i0 - 2, 2).toInt());
    }
}

void MainWindow::showSoundInError(const QString& errorMsg)
 {QMessageBox::critical(this, tr("Error in SoundIn"), errorMsg);}

void MainWindow::showStatusMessage(const QString& statusMsg)
 {statusBar()->showMessage(statusMsg);}

void MainWindow::on_actionDeviceSetup_triggered()
{
  DevSetup dlg(this);
  dlg.initDlg();

    if (dlg.exec() == QDialog::Accepted)
    {
        //
        // Apply runtime effects for SoundIn
        //
        if (dlg.m_restartSoundIn)
        {
      soundInThread.quit();
      soundInThread.wait(1000);

            soundInThread.setInputDevice(m_paInDevice);
      soundInThread.setNetwork(m_network);
      soundInThread.setFadd(m_fAdd);
            soundInThread.setRate(m_fs96000 ? 96000.0 : 95238.1);
            soundInThread.setSwapIQ(m_IQswap);
            soundInThread.setScale(m_dB);
            soundInThread.setPort(m_udpPort);
            soundInThread.setNrx(m_xpol ? 2 : 1);

      soundInThread.start(QThread::HighestPriority);
    }

        //
        // Apply runtime effects for SoundOut
        //
        if (dlg.m_restartSoundOut)
        {
      soundOutThread.quitExecution=true;
      soundOutThread.wait(1000);

      soundOutThread.setOutputDevice(m_paOutDevice);
            soundOutThread.start();
    }

        //
        // GUI updates
        //
        if (m_astro_window && m_astro_window->isVisible())
            m_astro_window->setFontSize(m_astroFont);

        ui->actionFind_Delta_Phi->setEnabled(m_xpol);

        m_messages_window->setColors(m_colors);
        m_band_map_window->setColors(m_colors);

        //
        // WideGraph updates
        //
        m_wide_graph_window->m_mult570   = m_mult570;
        m_wide_graph_window->m_mult570Tx = m_mult570Tx;
        m_wide_graph_window->m_cal570    = m_cal570;
        m_wide_graph_window->setFcal(m_fCal);

        //
        // Save to disk
        //
        writeSettings();
  }
}


void MainWindow::on_monitorButton_clicked()                  //Monitor
{
  m_monitoring=true;
  soundInThread.setMonitoring(true);
  m_diskData=false;
}
void MainWindow::on_actionLinrad_triggered()                 //Linrad palette
{
  if(m_wide_graph_window) m_wide_graph_window->setPalette("Linrad");
}

void MainWindow::on_actionCuteSDR_triggered()                //CuteSDR palette
{
  if(m_wide_graph_window) m_wide_graph_window->setPalette("CuteSDR");
}

void MainWindow::on_actionAFMHot_triggered()
{
  if(m_wide_graph_window) m_wide_graph_window->setPalette("AFMHot");
}

void MainWindow::on_actionBlue_triggered()
{
  if(m_wide_graph_window) m_wide_graph_window->setPalette("Blue");
}

void MainWindow::on_actionAbout_triggered()                  //Display "About"
{
  CAboutDlg dlg(this);
  dlg.exec();
}

void MainWindow::on_autoButton_clicked()                     //Auto
{
  m_auto = !m_auto;
  if(m_auto) {
    ui->autoButton->setStyleSheet(m_pbAutoOn_style);
    ui->autoButton->setText("Auto is ON");
  } else {
    btxok=false;
    ui->autoButton->setStyleSheet("");
    ui->autoButton->setText("Auto is OFF");
    on_monitorButton_clicked();
  }
}

void MainWindow::on_stopTxButton_clicked()                    //Stop Tx
{
  if(m_auto) on_autoButton_clicked();
  btxok=false;
}

void MainWindow::keyPressEvent( QKeyEvent *e )                //keyPressEvent
{
  switch(e->key())
  {
  case Qt::Key_F3:
    m_txMute=!m_txMute;
    break;
  case Qt::Key_F4:
    ui->dxCallEntry->setText("");
    ui->dxGridEntry->setText("");
    if(m_kb8rq) {
      m_ntx=6;
      ui->txrb6->setChecked(true);
    }
    break;
  case Qt::Key_F6:
    if(e->modifiers() & Qt::ShiftModifier) {
      on_actionDecode_remaining_files_in_directory_triggered();
    }
    break;
  case Qt::Key_F11:
    if(e->modifiers() & Qt::ShiftModifier) {
    } else {
      int n0=m_wide_graph_window->DF();
      int n=(n0 + 10000) % 5;
      if(n==0) n=5;
      m_wide_graph_window->setDF(n0-n);
    }
    break;
  case Qt::Key_F12:
    if(e->modifiers() & Qt::ShiftModifier) {
    } else {
      int n0=m_wide_graph_window->DF();
      int n=(n0 + 10000) % 5;
      if(n==0) n=5;
      m_wide_graph_window->setDF(n0+n);
    }
    break;
  case Qt::Key_G:
    if(e->modifiers() & Qt::AltModifier) {
      genStdMsgs("");
    }
    break;
  case Qt::Key_L:
    if(e->modifiers() & Qt::ControlModifier) {
      lookup();
      genStdMsgs("");
      break;
    }
  }
}

void MainWindow::bumpDF(int n)                                  //bumpDF()
{
  if(n==11) {
    int n0=m_wide_graph_window->DF();
    int n=(n0 + 10000) % 5;
    if(n==0) n=5;
    m_wide_graph_window->setDF(n0-n);
  }
  if(n==12) {
    int n0=m_wide_graph_window->DF();
    int n=(n0 + 10000) % 5;
    if(n==0) n=5;
    m_wide_graph_window->setDF(n0+n);
  }
}

bool MainWindow::eventFilter(QObject *object, QEvent *event)  //eventFilter()
{
  if (event->type() == QEvent::KeyPress) {
    //Use the event in parent using its keyPressEvent()
    QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
    MainWindow::keyPressEvent(keyEvent);
    return QObject::eventFilter(object, event);
  }
  return QObject::eventFilter(object, event);
}

void MainWindow::createStatusBar()                           //createStatusBar
{
  lab1 = new QLabel("Receiving");
  lab1->setAlignment(Qt::AlignHCenter);
  lab1->setMinimumSize(QSize(80,10));
  lab1->setStyleSheet("QLabel{background-color: #00ff00}");
  lab1->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  statusBar()->addWidget(lab1);

  lab2 = new QLabel("QSO freq:  125");
  lab2->setAlignment(Qt::AlignHCenter);
  lab2->setMinimumSize(QSize(90,10));
  lab2->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  statusBar()->addWidget(lab2);

  lab3 = new QLabel("QSO DF:   0");
  lab3->setAlignment(Qt::AlignHCenter);
  lab3->setMinimumSize(QSize(80,10));
  lab3->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  statusBar()->addWidget(lab3);

  lab4 = new QLabel("");
  lab4->setAlignment(Qt::AlignHCenter);
  lab4->setMinimumSize(QSize(80,10));
  lab4->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  statusBar()->addWidget(lab4);

  lab5 = new QLabel("");
  lab5->setAlignment(Qt::AlignHCenter);
  lab5->setMinimumSize(QSize(50,10));
  lab5->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  statusBar()->addWidget(lab5);

  lab6 = new QLabel("");
  lab6->setAlignment(Qt::AlignHCenter);
  lab6->setMinimumSize(QSize(50,10));
  lab6->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  statusBar()->addWidget(lab6);

  lab7 = new QLabel("Avg: 0");
  lab7->setAlignment(Qt::AlignHCenter);
  lab7->setMinimumSize(QSize(50,10));
  lab7->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  statusBar()->addWidget(lab7);
}

void MainWindow::on_tolSpinBox_valueChanged(int i)             //tolSpinBox
{
  static int ntol[] = {10,20,50,100,200,500,1000};
  m_tol=ntol[i];
  m_wide_graph_window->setTol(m_tol);
  ui->labTol1->setText(QString::number(ntol[i]));
}

void MainWindow::on_actionExit_triggered()                     //Exit()
{
  close ();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    set_stop_m65(1);
    if (m_gui_timer) m_gui_timer->stop();
    m_wide_graph_window->saveSettings();

    QFile quitFile(QDir {m_dataDir}.absoluteFilePath (".quit"));
    (void)quitFile.open(QFileDevice::ReadWrite);
    setQuitID(quitFile.handle());

    if (m_astro_window) m_astro_window->close();
    if (m_band_map_window) m_band_map_window->close();
    if (m_messages_window) {
        m_messages_window->setClosingForShutdown(true);
        m_messages_window->close();
    }
    if (m_wide_graph_window) m_wide_graph_window->close();

#ifdef __unix__
    ptt_close();   // close persistent Linux serial port
#endif

    if (g_pTxTune) {
        g_pTxTune->close();
        delete g_pTxTune;
        g_pTxTune = nullptr;
    }

    quitFile.remove();
    QMainWindow::closeEvent(e);
}


void MainWindow::on_stopButton_clicked()                       //stopButton
{
  m_monitoring=false;
  soundInThread.setMonitoring(m_monitoring);
  m_loopall=false;  
}

void MainWindow::msgBox(QString t)                             //msgBox
{
  msgBox0.setText(t);
  msgBox0.exec();
}

void MainWindow::stub()                                        //stub()
{
  msgBox("Not yet implemented.");
}

void MainWindow::on_actionRelease_Notes_triggered()
{
  QDesktopServices::openUrl(QUrl(
  "https://wsjt.sourceforge.io/Release_Notes.txt",
                              QUrl::TolerantMode));
}

void MainWindow::on_actionOnline_Users_Guide_triggered()      //Display manual
{
  QDesktopServices::openUrl(QUrl(
  "https://wsjt.sourceforge.io/MAP65_Users_Guide.pdf",
                              QUrl::TolerantMode));
}

void MainWindow::on_actionQSG_Q65_triggered()
{
  QDesktopServices::openUrl (QUrl {"https://wsjt.sourceforge.io/Q65_Quick_Start.pdf"});
}

void MainWindow::on_actionQSG_MAP65_v3_triggered()
{
  QDesktopServices::openUrl (QUrl {"https://wsjt.sourceforge.io/WSJTX_2.5.0_MAP65_3.0_Quick_Start.pdf"});
}

void MainWindow::on_actionQ65_Sensitivity_in_MAP65_3_0_triggered()
{
  QDesktopServices::openUrl (QUrl {"https://wsjt.sourceforge.io/Q65_Sensitivity_in_MAP65.pdf"});
}

void MainWindow::on_actionAstro_Data_triggered()             //Display Astro
{
  if (m_astro_window ) m_astro_window->show();
}

void MainWindow::on_actionWide_Waterfall_triggered()      //Display Waterfalls
{
  m_wide_graph_window->show();
}

void MainWindow::on_actionBand_Map_triggered()              //Display BandMap
{
  m_band_map_window->show ();
}

void MainWindow::on_actionMessages_triggered()              //Display Messages
{
    m_messages_window->show();
}   

void MainWindow::on_actionOpen_triggered()                     //Open File
{
  m_monitoring=false;
  soundInThread.setMonitoring(m_monitoring);
  // Wipe waterfall callsign overlay so stale labels from the previous
  // file (or live capture) don't linger over the new decode.
  if (m_wide_graph_window) m_wide_graph_window->clearDecodeLabels();
  QString fname;
  if(m_xpol) {
    fname=QFileDialog::getOpenFileName(this, "Open File", m_path,
                                       "MAP65 Files (*.tf2)");
  } else {
    fname=QFileDialog::getOpenFileName(this, "Open File", m_path,
                                       "MAP65 Files (*.iq)");
  }
  if(fname != "") {
    m_path=fname;
    int i;
    i=fname.indexOf(".iq") - 11;
    if(m_xpol) i=fname.indexOf(".tf2") - 11;
    if(i>=0) {
      lab1->setStyleSheet("QLabel{background-color: #66ff66}");
      lab1->setText(" " + fname.mid(i,15) + " ");
    }
    on_stopButton_clicked();
    m_diskData=true;
    m_decoderBusy=true;  //added 1.6.25 w3sz
    int dbDgrd=0;
    if(m_myCall=="K1JT" and m_idInt<0) dbDgrd=m_idInt;
    *future1 = QtConcurrent::run([this](QString fname, bool xpol, int dbDgrd) {
      this->getfile(fname, xpol, dbDgrd);
      }, fname, m_xpol, dbDgrd);      
  //  qDebug() << QDateTime::currentMSecsSinceEpoch()  << "MainWindow::on_actionOpen_triggered Reading wav file: " << m_path;  
    watcher1->setFuture(*future1);
  }
}

void MainWindow::on_actionOpen_next_in_directory_triggered()   //Open Next
{
  if (m_decoderBusy) { 
    qDebug() << "Decode is busy; not starting next file yet."; 
    return; 
  } 
  m_decoderBusy = true;
  if (m_wide_graph_window) m_wide_graph_window->clearDecodeLabels();

  int i,len;
  QFileInfo fi(m_path);
  QStringList list;
  if(m_xpol) {
      list= fi.dir().entryList().filter(".tf2");
  } else {
      list= fi.dir().entryList().filter(".iq");
  }
  for (i = 0; i < list.size()-1; ++i) {
    if(i==list.size()-2) m_loopall=false;
    const QString &entry = list.at(i);
    len = entry.length();
    if (entry == m_path.right(len)) {
      int n=m_path.length();
      QString fname = m_path;
      fname.replace(n - len, len, list.at(i+1));
      m_path=fname;
      int idx = fname.indexOf(m_xpol ? ".tf2" : ".iq") - 11;
      if (idx >= 0) {
        lab1->setStyleSheet("QLabel{background-color: #66ff66}");
          lab1->setText(" " + fname.mid(idx, len) + " ");
      }
      m_diskData=true;
      int dbDgrd=0;
      if(m_myCall=="K1JT" and m_idInt<0) dbDgrd=m_idInt;
      *future1 = QtConcurrent::run([this](QString fname, bool xpol, int dbDgrd) {
        this->getfile(fname, xpol, dbDgrd);
        }, fname, m_xpol, dbDgrd);
    //  qDebug() << "MainWindow::on_actionOpen_next_in_directory_triggered Reading wav file: " << m_path;        
      watcher1->setFuture(*future1);
      return;
    }
  }
}
                                                   //Open all remaining files
void MainWindow::on_actionDecode_remaining_files_in_directory_triggered()
{
  m_loopall=true;
  on_actionOpen_next_in_directory_triggered();
}

void MainWindow::diskDat()                                   //diskDat()
{
  double hsym;
  //These may be redundant??
  m_diskData=true;
  setNewdat(1);
  if(m_wide_graph_window->m_bForceCenterFreq) {
    setFcenter(m_wide_graph_window->m_dForceCenterFreq);
  }

  if(m_fs96000) hsym=2048.0*96000.0/11025.0;   //Samples per JT65 half-symbol
  if(!m_fs96000) hsym=2048.0*95238.1/11025.0;
  for(int i=0; i<304; i++) {           // Do the half-symbol FFTs
    int k = i*hsym + 2048.5;
    dataSink(k);
    qApp->processEvents();             // Allow the waterfall to update
  }
}

void MainWindow::diskWriteFinished()                      //diskWriteFinished
{
//  qDebug() << "diskWriteFinished";
//  decode();
}
                                                        //Delete ../save/*.tf2
void MainWindow::on_actionDelete_all_tf2_files_in_SaveDir_triggered()
{
  int i;
  QString fname;
  int ret = QMessageBox::warning(this, "Confirm Delete",
      "Are you sure you want to delete all *.tf2 and *.iq files in\n" +
       QDir::toNativeSeparators(m_saveDir) + " ?",
       QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
  if(ret==QMessageBox::Yes) {
    QDir dir(m_saveDir);
    QStringList files=dir.entryList(QDir::Files);
    QList<QString>::iterator f;
    for(f=files.begin(); f!=files.end(); ++f) {
      fname=*f;
      i=(fname.indexOf(".tf2"));
      if(i==11) dir.remove(fname);
      i=(fname.indexOf(".iq"));
      if(i==11) dir.remove(fname);
    }
  }
}
                                          //Clear BandMap and Messages windows
void MainWindow::on_actionErase_Band_Map_and_Messages_triggered()
{
  m_band_map_window->setText("");
  m_messages_window->setText("","");
  m_map65RxLog |= 4;
}

void MainWindow::on_actionFind_Delta_Phi_triggered()              //Find dPhi
{
  m_map65RxLog |= 8;
  on_DecodeButton_clicked();
}

void MainWindow::on_actionF4_sets_Tx6_triggered()                //F4 sets Tx6
{
  m_kb8rq = !m_kb8rq;
}

void MainWindow::on_actionOnly_EME_calls_triggered()          //EME calls only
{
  m_onlyEME = ui->actionOnly_EME_calls->isChecked();
}

void MainWindow::on_actionNo_shorthands_if_Tx1_triggered()
{
  stub();
}

void MainWindow::on_actionNo_Deep_Search_triggered()          //No Deep Search
{
  m_ndepth=0;
}

void MainWindow::on_actionNormal_Deep_Search_triggered()      //Normal DS
{
  m_ndepth=1;
}

void MainWindow::on_actionAggressive_Deep_Search_triggered()  //Aggressive DS
{
  m_ndepth=2;
}

void MainWindow::on_actionNone_triggered()                    //Save None
{
  m_saveAll=false;
}

// ### Implement "Save Last" here? ###

void MainWindow::on_actionSave_all_triggered()                //Save All
{
  m_saveAll=true;
}
                                          //Display list of keyboard shortcuts
void MainWindow::on_actionKeyboard_shortcuts_triggered()
{
  stub();
}
                                              //Display list of mouse commands
void MainWindow::on_actionSpecial_mouse_commands_triggered()
{
  stub();
}
                                              //Diaplay list of Add-On pfx/sfx
void MainWindow::on_actionAvailable_suffixes_and_add_on_prefixes_triggered()
{
  stub();
}

void MainWindow::on_DecodeButton_clicked()                    //Decode request
{
  int n=m_sec0%m_TRperiod;
  if(m_monitoring and n>47 and (n<52 or m_decoderBusy)) return;
  if(!m_decoderBusy) {
    setNewdat(0);
    setNagain(1);
    decode();
  }
}

void MainWindow::freezeDecode(int n)                          //freezeDecode()
{
  if(n==2) {
    ui->tolSpinBox->setValue(5);
    setNtol(m_tol);
    setMousedf(0);
  } else {
    ui->tolSpinBox->setValue(qMin(3,ui->tolSpinBox->value()));
    setNtol(m_tol);
  }
  if(!m_decoderBusy) {
    setNagain(1);
    setNewdat(0);
    decode();
  }
}

void MainWindow::decode()                                       //decode()
{
  decodeBusy(true);
  // Reset decode-finished count for this file
  m_decodeFinishedCount = 0;

  ui->DecodeButton->setStyleSheet(m_pbdecoding_style1);

//  QFile f("mockRTfiles.txt");
//  if(datcom_.nagain==0 && (!m_diskData) && !f.exists()) {
  if(getNagain()==0 && (!m_diskData)) {
    qint64 ms = QDateTime::currentMSecsSinceEpoch() % 86400000;
    int imin=ms/60000;
    int ihr=imin/60;
    imin=imin % 60;
    setNutc(100*ihr + imin);
  }

  setIdphi(m_dPhi);
  setMousedf(m_wide_graph_window->DF());
  setMousefqso(m_wide_graph_window->QSOfreq());
  setNdepth(m_ndepth);
  setNdiskdat(0);
  if(m_diskData) {
    if(m_myGrid.trimmed().length()>=6) {
      setNdiskdat(1);
      setNagain(0);
      int i0=m_path.indexOf(".tf2");
      if(i0<0) i0=m_path.indexOf(".iq");
      if(i0>0) {
        // Compute self Doppler using the filename for Date and Time
        int nyear=m_path.mid(i0-11,2).toInt()+2000;
        int month=m_path.mid(i0-9,2).toInt();
        int nday=m_path.mid(i0-7,2).toInt();
        int nhr=m_path.mid(i0-4,2).toInt();
        int nmin=m_path.mid(i0-2,2).toInt();
        double uth=nhr + nmin/60.0;
        int nfreq = getFcenter();
        int ndop00;
        QByteArray myGridData = m_myGrid.toLatin1();
        astrosub00_(&nyear, &month, &nday, &uth, &nfreq, myGridData.constData(),&ndop00, myGridData.size());
        setNdop00(ndop00);               //Send self Doppler to decoder, via datcom
      }
    }
    else { 
      QMessageBox::information(this,"MAP65","No 6-digit MyGrid recognized by decode()");
      return;
    }
  }
  setNeme(0);
  if(ui->actionOnly_EME_calls->isChecked()) setNeme(1);

  int ispan = int(m_wide_graph_window->fSpan());
  if (ispan % 2 == 1) ispan++;

  double fc = getFcenter();  //MHz
  // ifc is fractional kHz of RF center, used to align baseband window with RF dial.
  // Must stay in [0, 999]. Changing fcenter semantics will break wideband.
  int ifc = int(1000.0*(fc - int(fc)) + 0.5);  // fractional kHz of RF center

  int nfa=m_wide_graph_window->nStartFreq();
  int nfb=nfa+ispan;
  int nfshift=nfa + ispan/2 - ifc;

  setNfa(nfa);
  setNfb(nfb);
  setNfshift(nfshift);

  setNfcal(m_fCal);
  setMcall3(0);
  if(m_call3Modified) setMcall3(1);
  setNtimeout(m_timeout);
  setNtol(m_tol);
  setNxant(0);
  if(m_xpolx) setNxant(1);
  if(getNutc() < m_nutc0) m_map65RxLog |= 1;  //Date and Time to map65_rx.log
  m_nutc0=getNutc();
  setMap65RxLog(m_map65RxLog);
  setNfsample(96000);
  if(!m_fs96000) setNfsample(95238);
  setNxpol(0);
  if(m_xpol) setNxpol(1);
  setNmode(10*m_modeQ65 + m_modeJT65);
//  datcom_.nfast=1;                               //No longer used
  setNsave(m_nsave);
  setMaxDrift(ui->sbMaxDrift->value());

QString mcall = (m_myCall + "            ").mid(0, 12);
QString mgrid = (m_myGrid + "            ").mid(0, 6);
QString hcall = (ui->dxCallEntry->text() + "            ").mid(0, 12);
QString hgrid = (ui->dxGridEntry->text() + "      ").mid(0, 6);

  setMyCall(mcall);
  setMyGrid(mgrid);
  setHisCall(hcall);
  setHisGrid(hgrid);
  setDatetime(m_dateTime);
  setNewdat(1);
  setJunk1(1234);
  setJunk2(5678);

  setNagain(0); //added 12-30-25 to agree with legacy
  if (!m_diskData) setNdiskdat(0);  //added 12-30-25 to agree with legacy
  setDecoderReady(1);
  m_map65RxLog=0;
  m_call3Modified=false;
//  qDebug() << QDateTime::currentMSecsSinceEpoch()  << "finished MainWindow::decode()";
}

bool MainWindow::subProcessFailed (QProcess * process, int exit_code, QProcess::ExitStatus status)
{
  if (exit_code || QProcess::NormalExit != status)
    {
      QStringList arguments;
      for (auto argument: process->arguments ())
        {
          if (argument.contains (' ')) argument = '"' + argument + '"';
          arguments << argument;
        }
      writeCrashData();  
      MessageBox::critical_message (this, tr ("Subprocess Error")
                                    , tr ("Subprocess failed with exit code %1")
                                    .arg (exit_code)
                                    , tr ("Running: %1\n%2")
                                    .arg (process->program () + ' ' + arguments.join (' '))
                                    .arg (QString {process->readAllStandardError()}));
      return true;
    }
  return false;
}

void MainWindow::writeCrashData() {
  QFile file("crash_data.txt");

  if (file.open(QIODevice::Append | QIODevice::Text)) {
      QTextStream out(&file);

      out << "---- Crash Data Dump ----\n";

      out << "fcenter: " << getFcenter() << "\n";

      out << "nutc: " << getNutc() << ", idphi: " << getIdphi()
          << ", mousedf: " << getMousedf() << ", mousefqso: " << getMousefqso()
          << ", nagain: " << getNagain() << ", ndepth: " << getNdepth() << "\n";

      out << "ndiskdat: " << getNdiskdat() << ", neme: " << getNeme()
          << ", newdat: " << getNewdat() << ", nfa: " << getNfa()
          << ", nfb: " << getNfb() << ", nfcal: " << getNfcal()
          << ", nfshift: " << getNfshift() << "\n";

      out << "mcall3: " << getMcall3() << ", ntimeout: " << getNtimeout()
          << ", ntol: " << getNtol() << ", nxant: " << getNxant()
          << ", map65RxLog: " << getMap65RxLog() << ", nfsample: " << getNfsample() << "\n";

      out << "nxpol: " << getNxpol() << ", nmode: " << getNmode()
          << ", nsave: " << getNsave()
          << ", max_drift: " << getMaxDrift() << ", nhsym: " << getNhsym() << "\n";

      out << "junk1: " << getJunk1() << ", junk2: " << getJunk2() << "\n";

      out << "mycall: " << getMyCall() << "\n";
      out << "hiscall: " << getHisCall() << "\n";
      out << "mygrid: " << getMyGrid() << "\n";
      out << "hisgrid: " << getHisGrid() << "\n";
      out << "datetime: " << getDatetime() << "\n";

      out << "--------------------------\n";

      file.close();
  } else {
      qWarning("Could not open crash_data.txt for writing.");
  }
}

void MainWindow::editor_error()                                 //editor_error
{
  msgBox("Error starting or running\n" + m_appDir + "/" + m_editorCommand);
}

void MainWindow::on_EraseButton_clicked()
{
  qint64 ms=QDateTime::currentMSecsSinceEpoch();
  ui->decodedTextBrowser->clear();
  if((ms-m_msErase)<500) {
    on_actionErase_Band_Map_and_Messages_triggered();
  }
  m_msErase=ms;
}


void MainWindow::decodeBusy(bool b)                             //decodeBusy()
{
//  qDebug()  << QDateTime::currentMSecsSinceEpoch() << "decodeBusy(" << b << ")";
  m_decoderBusy=b;
  ui->DecodeButton->setEnabled(!b);
  ui->actionOpen->setEnabled(!b);
  ui->actionOpen_next_in_directory->setEnabled(!b);
  ui->actionDecode_remaining_files_in_directory->setEnabled(!b);
}

//------------------------------------------------------------- //guiUpdate()
void MainWindow::guiUpdate()
{
  static int iptt0=0;
  static int iptt=0;
  static bool btxok0=false;
  static bool bTune0=false;
  static bool bMonitoring0=false;
  static int nc0=1;
  static int nc1=1;
  static char msgsent[23];
  static int nsendingsh=0;
  int khsym=0;

  double tx1=0.0;
  double tx2=126.0*4096.0/11025.0 + 1.8;
  if(m_modeTx=="Q65") tx2=85.0*7200.0/12000.0 + 1.8;

  if(!m_txFirst) {
    tx1 += m_TRperiod;
    tx2 += m_TRperiod;
  }
  qint64 ms = QDateTime::currentMSecsSinceEpoch() % 86400000;
  int nsec=ms/1000;
  double tsec=0.001*ms;
  double t2p=fmod(tsec,120.0);
  bool bTxTime = (t2p >= tx1) and (t2p < tx2);

  if(bTune0 and !bTune) {
    btxok=false;
    m_monitoring=bMonitoring0;
    soundInThread.setMonitoring(m_monitoring);
  }
  if(bTune and !bTune0) bMonitoring0=m_monitoring;
  bTune0=bTune;

  if(m_auto or bTune) {
    if ((bTxTime or bTune) && iptt == 0 && !m_txMute) {

  if (m_pttPath != "NONE") {
      int itx = 1;
      int nport = m_pttPortNumber;   // the real COM port number
      int ierr = ptt_(&nport, &itx, &iptt);

      if (ierr != 0) {
          if (!m_pttErrorShown) {
              char s[256];
              snprintf(s, sizeof(s), "Cannot open Port: %s",
                      m_pttPath.toUtf8().constData());
              msgBox(s);
              m_pttErrorShown = true;
          }
          on_stopTxButton_clicked();
      }
  }

        if (m_bIQxt)
            m_wide_graph_window->tx570();

        if (!soundOutThread.isRunning())
        soundOutThread.start(QThread::HighPriority);
      }

    if ((!bTxTime && !bTune) || m_txMute)
      btxok=false;
    }

// Calculate Tx waveform when needed
  if((iptt==1 && iptt0==0) || m_restart) {
    char message[23];
    QByteArray ba;
    if(m_ntx == 1) ba=ui->tx1->text().toLocal8Bit();
    if(m_ntx == 2) ba=ui->tx2->text().toLocal8Bit();
    if(m_ntx == 3) ba=ui->tx3->text().toLocal8Bit();
    if(m_ntx == 4) ba=ui->tx4->text().toLocal8Bit();
    if(m_ntx == 5) ba=ui->tx5->text().toLocal8Bit();
    if(m_ntx == 6) ba=ui->tx6->text().toLocal8Bit();

    ba2msg(ba,message);
    int len1=22;
    int mode65=m_mode65;
    int ntxFreq=1000;
    double samfac=1.0;
    qDebug() << mode65 << samfac;
    if(m_modeTx=="JT65") {
      gen65_(message,&mode65,&samfac,&nsendingsh,msgsent,iwave,
             &nwave,len1,len1);
    } else {
      if(m_modeQ65==5) ntxFreq=700;
      gen_q65_wave_(message,&ntxFreq,&m_modeQ65,msgsent,iwave,
                 &nwave,len1,len1);
    }
    msgsent[22]=0;

    if(m_restart) {
      QString t="  Tx " + m_modeTx + "   ";
      t=t.left(11);
      QFile f("map65_tx.log");
      qDebug() << "MainWindow::guiUpdate 1 File open result:" << f.open(QFileDevice::WriteOnly | QFileDevice::Text | QFileDevice::Append);
      QTextStream out(&f);
      out << QDateTime::currentDateTimeUtc().toString("yyyy-MMM-dd hh:mm")
          << t << QString::fromLatin1(msgsent)
#if QT_VERSION >= QT_VERSION_CHECK (5, 15, 0)
          << Qt::endl
#else
          << endl
#endif
        ;
      f.close();
    }

    m_restart=false;
  }

// If PTT was just raised, start a countdown for raising TxOK:
  if(iptt==1 && iptt0==0) nc1=-9;    // TxDelay = 0.8 s
  if(nc1 <= 0) nc1++;
  if(nc1 == 0) {
    xSignalMeter->setValue(0);
    ySignalMeter->setValue(0);
    m_monitoring=false;
    soundInThread.setMonitoring(false);
    btxok=true;
    m_transmitting=true;
    m_wide_graph_window->enableSetRxHardware(false);

    QString t="  Tx " + m_modeTx + "   ";
    t=t.left(11);
    QFile f("map65_tx.log");
    qDebug() << "MainWindow::guiUpdate 2 File open result:" << f.open(QFileDevice::WriteOnly | QFileDevice::Text | QFileDevice::Append);
    QTextStream out(&f);
    out << QDateTime::currentDateTimeUtc().toString("yyyy-MMM-dd hh:mm")
        << t << QString::fromLatin1(msgsent)
#if QT_VERSION >= QT_VERSION_CHECK (5, 15, 0)
        << Qt::endl
#else
        << endl
#endif
      ;
    f.close();
  }

// If btxok was just lowered, start a countdown for lowering PTT
  if(!btxok && btxok0 && iptt==1) nc0=-11;  //RxDelay = 1.0 s
  btxok0=btxok;
  if(nc0 <= 0) nc0++;
  if(nc0 == 0) {
    if(m_bIQxt) m_wide_graph_window->rx570();     // Set Si570 back to Rx Freq
  int itx = 0;
  int nport = m_pttPortNumber;   // the real COM port number
  ptt_(&nport, &itx, &iptt);     // Lower PTT
  m_pttErrorShown = false;

    if(!m_txMute) {
      soundOutThread.quitExecution=true;\
    }
    m_transmitting=false;
    m_wide_graph_window->enableSetRxHardware(true);
    if(m_auto) {
      m_monitoring=true;
      soundInThread.setMonitoring(m_monitoring);
    }
  }

  if(iptt == 0 && !btxok) {
    // sending=""
    // nsendingsh=0
  }

  if(m_monitoring) {
    ui->monitorButton->setStyleSheet(m_pbmonitor_style);
  } else {
    ui->monitorButton->setStyleSheet("");
  }

  lab2->setText("QSO Freq:  " + QString::number(m_wide_graph_window->QSOfreq()));
  lab3->setText("QSO DF:  " + QString::number(m_wide_graph_window->DF()));

  m_wide_graph_window->updateFreqLabel();

  if(m_modeQ65==0 and m_modeTx=="Q65") on_pbTxMode_clicked();
  if(m_modeJT65==0  and m_modeTx=="JT65")  on_pbTxMode_clicked();

  if(nsec != m_sec0) {                                     //Once per second
//    qDebug() << "A" << nsec%60 << m_mode65 << m_modeQ65 << m_modeTx;
    soundInThread.setForceCenterFreqMHz(m_wide_graph_window->m_dForceCenterFreq);
    soundInThread.setForceCenterFreqBool(m_wide_graph_window->m_bForceCenterFreq);

    if(m_pctZap>30.0 and !m_transmitting) {
      lab4->setStyleSheet("QLabel{background-color: #ff0000}");
    } else {
      lab4->setStyleSheet("");
    }

    if(m_transmitting) {
      if(nsendingsh==1) {
        lab1->setStyleSheet("QLabel{background-color: #66ffff}");
      } else if(nsendingsh==-1) {
        lab1->setStyleSheet("QLabel{background-color: #ffccff}");
      } else {
        lab1->setStyleSheet("QLabel{background-color: #ffff33}");
      }
      char s[37];
      snprintf(s, sizeof(s), "Tx: %.32s", msgsent);
      lab1->setText(s);
    } else if(m_monitoring) {
      lab1->setStyleSheet("QLabel{background-color: #00ff00}");
      m_nrx=soundInThread.nrx();
      khsym=soundInThread.mhsym();
      QString t;
      if(m_network) {
        if(m_nrx==-1) t="F1";
        if(m_nrx==1) t="I1";
        if(m_nrx==-2) t="F2";
        if(m_nrx==+2) t="I2";
      } else {
        if(m_nrx==1) t="S1";
        if(m_nrx==2) t="S2";
      }
      if((abs(m_nrx)==1 and m_xpol) or (abs(m_nrx)==2 and !m_xpol))
        lab1->setStyleSheet("QLabel{background-color: #ff1493}");
      if(khsym==m_hsym0) {
        t="Nil";
        lab1->setStyleSheet("QLabel{background-color: #ffc0cb}");
      }
      lab1->setText("Receiving " + t);
    } else if (!m_diskData) {
      lab1->setStyleSheet("");
      lab1->setText("");
    }

    QDateTime t = QDateTime::currentDateTimeUtc();
    int fQSO=m_wide_graph_window->QSOfreq();
    if (m_wide_graph_window->m_bLockTxRx) m_txFreq=fQSO;
    m_astro_window->astroUpdate(t, m_myGrid, m_hisGrid, fQSO, m_setftx,
                          m_txFreq, m_azelDir, m_xavg);
    m_setftx=0;
    QString utc = t.date().toString(" yyyy MMM dd \n") + t.time().toString();
    ui->labUTC->setText(utc);
    guiDate = ui->labUTC->text().trimmed().mid(0,12); //liveCQ
    if((!m_monitoring and !m_diskData) or (khsym==m_hsym0)) {
      xSignalMeter->setValue(0);
      ySignalMeter->setValue(0);
      lab4->setText(" Rx noise:    0.0     0.0  0.0% ");
    }
    m_hsym0=khsym;
    m_sec0=nsec;
  }
  iptt0=iptt;
  bIQxt=m_bIQxt;
}

void MainWindow::ba2msg(QByteArray ba, char message[])             //ba2msg()
{
  bool eom;
  eom=false;
  for(int i=0;i<22; i++) {
    if (i >= ba.size () || !ba[i]) eom=true;
    if(eom) {
      message[i] = ' ';
    } else {
      message[i]=ba[i];
    }
  }
  message[22] = '\0';
}

void MainWindow::on_txFirstCheckBox_stateChanged(int nstate)        //TxFirst
{
  m_txFirst = (nstate==2);
}

void MainWindow::set_ntx(int n)                                   //set_ntx()
{
  m_ntx=n;
}

void MainWindow::on_txb1_clicked()                                //txb1
{
  m_ntx=1;
  ui->txrb1->setChecked(true);
  m_restart=true;
}

void MainWindow::on_txb2_clicked()                                //txb2
{
  m_ntx=2;
  ui->txrb2->setChecked(true);
  m_restart=true;
}

void MainWindow::on_txb3_clicked()                                //txb3
{
  m_ntx=3;
  ui->txrb3->setChecked(true);
  m_restart=true;
}

void MainWindow::on_txb4_clicked()                                //txb4
{
  m_ntx=4;
  ui->txrb4->setChecked(true);
  m_restart=true;
}

void MainWindow::on_txb5_clicked()                                //txb5
{
  m_ntx=5;
  ui->txrb5->setChecked(true);
  m_restart=true;
}

void MainWindow::on_txb6_clicked()                                //txb6
{
  m_ntx=6;
  ui->txrb6->setChecked(true);
  m_restart=true;
}

void MainWindow::selectCall2(bool ctrl)                         //selectCall2
{
  QString t = ui->decodedTextBrowser->toPlainText();   //Full contents
  int i=ui->decodedTextBrowser->textCursor().position();
  int i0=t.lastIndexOf(" ",i);
  int i1=t.indexOf(" ",i);
  QString hiscall=t.mid(i0+1,i1-i0-1);
  if(hiscall!="") {
    int n=hiscall.length();
    if( n>2 and n<13 and hiscall.toDouble()==0.0 and \
        hiscall.mid(2,-1).toInt()==0) doubleClickOnCall(hiscall, ctrl);
  }
}
                                                          //doubleClickOnCall
void MainWindow::doubleClickOnCall(QString hiscall, bool ctrl)
{
  if(m_worked[hiscall]) {
    msgBox("Possible dupe: " + hiscall + " already in log.");
  }
  ui->dxCallEntry->setText(hiscall);
  QString t = ui->decodedTextBrowser->toPlainText();   //Full contents
  int i2=ui->decodedTextBrowser->textCursor().position();
  QString t1 = t.mid(0,i2);              //contents up to text cursor
  int i1=t1.lastIndexOf("\n") + 1;
  QString t2 = t1.mid(i1,i2-i1);         //selected line
  int n = 60*t2.mid(14,2).toInt() + t2.mid(16,2).toInt();
  m_txFirst = ((n%2) == 1);
  ui->txFirstCheckBox->setChecked(m_txFirst);
  if((t2.indexOf("#")>0) and m_modeTx!="JT65") on_pbTxMode_clicked();
  if((t2.indexOf(":")>0) and m_modeTx!="Q65") on_pbTxMode_clicked();

  QString t3=t.mid(i1);
  int i3=t3.indexOf("\n");
  if(i3<0) i3=t3.length();
  t3=t3.left(i3);
  auto const& words = t3.mid(30).split(' ', SkipEmptyParts);
  QString grid=words[2];
  if(isGrid4(grid) and hiscall==words[1]) {
    ui->dxGridEntry->setText(grid);
  } else {
    lookup();
  }

  QString rpt="";
  if(ctrl or m_modeTx=="Q65") rpt=t2.mid(25,3);
  genStdMsgs(rpt);
  if(t2.indexOf(m_myCall)>0) {
    m_ntx=2;
    ui->txrb2->setChecked(true);
  } else {
    m_ntx=1;
    ui->txrb1->setChecked(true);
  }
}
                                                      //doubleClickOnMessages
void MainWindow::doubleClickOnMessages(QString hiscall, QString t2, bool ctrl)
{
  if(hiscall.length()<3) return;
  if(m_worked[hiscall]) {
    msgBox("Possible dupe: " + hiscall + " already in log.");
  }
  ui->dxCallEntry->setText(hiscall);
  int n = 60*t2.mid(13,2).toInt() + t2.mid(15,2).toInt();
  m_txFirst = ((n%2) == 1);
  ui->txFirstCheckBox->setChecked(m_txFirst);

  if((t2.indexOf(":")<0) and m_modeTx!="JT65") on_pbTxMode_clicked();
  if((t2.indexOf(":")>0) and m_modeTx!="Q65") on_pbTxMode_clicked();

  auto const& words = t2.mid(25).split(' ', SkipEmptyParts);
  QString grid=words[2];
  if(isGrid4(grid) and hiscall==words[1]) {
    ui->dxGridEntry->setText(grid);
  } else {
    lookup();
  }

  QString rpt="";
  if(ctrl or m_modeTx=="Q65") rpt=t2.mid(20,3);
  genStdMsgs(rpt);

  if(t2.indexOf(m_myCall)>0) {
    m_ntx=2;
    ui->txrb2->setChecked(true);
  } else {
    m_ntx=1;
    ui->txrb1->setChecked(true);
  }
}

void MainWindow::genStdMsgs(QString rpt)                       //genStdMsgs()
{
  if(rpt.left(2)==" -") rpt="-0"+rpt.mid(2,1);
  if(rpt.left(2)==" +") rpt="+0"+rpt.mid(2,1);
  QString hiscall=ui->dxCallEntry->text().toUpper().trimmed();
  ui->dxCallEntry->setText(hiscall);
  QString t0=hiscall + " " + m_myCall + " ";
  QString t=t0;
  if(t0.indexOf("/")<0) t=t0 + m_myGrid.mid(0,4);
  msgtype(t, ui->tx1);
  if(rpt == "" and m_modeTx=="Q65") rpt="-24";
  if(rpt == "" and m_modeTx=="JT65") {
    t=t+" OOO";
    msgtype(t, ui->tx2);
    msgtype("RO", ui->tx3);
    msgtype("RRR", ui->tx4);
    msgtype("73", ui->tx5);
  } else {
    t=t0 + rpt;
    msgtype(t, ui->tx2);
    t=t0 + "R" + rpt;
    msgtype(t, ui->tx3);
    t=t0 + "RRR";
    msgtype(t, ui->tx4);
    t=t0 + "73";
    msgtype(t, ui->tx5);
  }
  t="CQ " + m_myCall + " " + m_myGrid.mid(0,4);
  msgtype(t, ui->tx6);
  m_ntx=1;
  ui->txrb1->setChecked(true);
}

QString MainWindow::call3Path() const
{
  QString writablePath = QDir {m_dataDir}.absoluteFilePath("CALL3.TXT");
  if (!QFile::exists(writablePath)) {
    QString bundledPath = QDir {m_appDir}.absoluteFilePath("CALL3.TXT");
    if (QFile::exists(bundledPath)) {
      QFile::copy(bundledPath, writablePath);
    }
  }
  return writablePath;
}

void MainWindow::lookup()                                       //lookup()
{
  QString hiscall=ui->dxCallEntry->text().toUpper().trimmed();
  ui->dxCallEntry->setText(hiscall);
  QString call3File = call3Path();
  QFile f(call3File);
  if(!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    msgBox("Cannot open " + call3File);
    return;
  }
  char c[132];
  qint64 n=0;
  for(int i=0; i<999999; i++) {
    n=f.readLine(c,sizeof(c));
    if(n <= 0) {
      ui->dxGridEntry->setText("");
      break;
     }
    QString t=QString(c);
    if(t.indexOf(hiscall)==0) {
      int i1=t.indexOf(",");
      QString hisgrid=t.mid(i1+1,6);
      i1=hisgrid.indexOf(",");
      if(i1>0) {
        hisgrid=hisgrid.mid(0,4);
      } else {
        hisgrid=hisgrid.mid(0,4) + hisgrid.mid(4,2).toLower();
      }
      ui->dxGridEntry->setText(hisgrid);
      break;
    }
  }
  f.close();
}

void MainWindow::on_lookupButton_clicked()                    //Lookup button
{
  lookup();
}

void MainWindow::on_addButton_clicked()                       //Add button
{
  if(ui->dxGridEntry->text()=="") {
    msgBox("Please enter a valid grid locator.");
    return;
  }
  m_call3Modified=false;
  QString hiscall=ui->dxCallEntry->text().toUpper().trimmed();
  QString hisgrid=ui->dxGridEntry->text().trimmed();
  QString newEntry=hiscall + "," + hisgrid;

  int ret = QMessageBox::warning(this, "Add",
       newEntry + "\n" + "Is this station known to be active on EME?",
       QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
  if(ret==QMessageBox::Yes) {
    newEntry += ",EME,,";
  } else {
    newEntry += ",,,";
  }
  QString call3File = call3Path();
  QFile f1(call3File);
  if(!f1.open(QIODevice::ReadWrite | QIODevice::Text)) {
    msgBox("Cannot open " + call3File);
    return;
  }

  if(f1.size()==0) {
    QTextStream out(&f1);
    out << "ZZZZZZ"
#if QT_VERSION >= QT_VERSION_CHECK (5, 15, 0)
        << Qt::endl
#else
        << endl
#endif
      ;
    f1.seek (0);
  }

  QString tmpFile = QDir {m_dataDir}.absoluteFilePath("CALL3.TMP");
  QFile f2(tmpFile);
  if(!f2.open(QIODevice::ReadWrite | QIODevice::Truncate | QIODevice::Text)) {
    msgBox("Cannot open " + tmpFile);
    return;
  }
  {
    QTextStream in(&f1);
    QTextStream out(&f2);
    QString hc=hiscall;
    QString hc1="";
    QString hc2="000000";
    QString s;
    do {
      s=in.readLine();
      hc1=hc2;
      if(s.mid(0,2)=="//") {
        out << s + "\n";
      } else {
        int i1=s.indexOf(",");
        hc2=s.mid(0,i1);
        if(hc>hc1 && hc<hc2) {
          out << newEntry + "\n";
          out << s + "\n";
          m_call3Modified=true;
        } else if(hc==hc2) {
          QString t=s + "\n\n is already in CALL3.TXT\n" +
            "Do you wish to replace it?";
          int ret = QMessageBox::warning(this, "Add",t,
                                         QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
          if(ret==QMessageBox::Yes) {
            out << newEntry + "\n";
            m_call3Modified=true;
          }
        } else {
          if(s!="") out << s + "\n";
        }
      }
    } while(!s.isNull());
    if(hc>hc1 && !m_call3Modified) out << newEntry + "\n";
  }
  
  if(m_call3Modified) {
    auto const& old_path = QDir {m_dataDir}.absoluteFilePath("CALL3.OLD");
    QFile f0 {old_path};
    if (f0.exists ()) f0.remove ();
    f1.copy (old_path);         // copying as we want to preserve
                                // symlinks
    qDebug() << "MainWindow::on_addButton_clicked File open result:" << f1.open (QFileDevice::WriteOnly | QFileDevice::Text); // truncates
    f2.seek (0);
    f1.write (f2.readAll ());   // copy contents
    f2.remove ();
  }
}

void MainWindow::msgtype(QString t, QLineEdit* tx)                //msgtype()
{
//  if(t.length()<1) return 0;
  char message[23];
  char msgsent[23];
  int len1=22;
  int mode65=0;            //mode65 ==> check message but don't make wave()
  double samfac=1.0;
  int nsendingsh=0;
  int mwave;
  t=t.toUpper();
  int i1=t.indexOf(" OOO");
  QByteArray s=t.toUpper().toLocal8Bit();
  ba2msg(s,message);
  gen65_(message,&mode65,&samfac,&nsendingsh,msgsent,iwave,
         &mwave,len1,len1);

  QPalette p(tx->palette());
  if(nsendingsh==1) {
    p.setColor(QPalette::Base,"#66ffff");
  } else if(nsendingsh==-1) {
    p.setColor(QPalette::Base,"#ffccff");
  } else {
    p.setColor(QPalette::Base,Qt::white);
  }
  tx->setPalette(p);
  int len=t.length();
  if(nsendingsh==-1) {
    len=qMin(len,13);
    if(i1>10) {
      tx->setText(t.mid(0,len).toUpper() + " OOO");
    } else {
      tx->setText(t.mid(0,len).toUpper());
    }
  } else {
    tx->setText(t);
  }
}

void MainWindow::on_tx1_editingFinished()                       //tx1 edited
{
  QString t=ui->tx1->text();
  msgtype(t, ui->tx1);
}

void MainWindow::on_tx2_editingFinished()                       //tx2 edited
{
  QString t=ui->tx2->text();
  msgtype(t, ui->tx2);
}

void MainWindow::on_tx3_editingFinished()                       //tx3 edited
{
  QString t=ui->tx3->text();
  msgtype(t, ui->tx3);
}

void MainWindow::on_tx4_editingFinished()                       //tx4 edited
{
  QString t=ui->tx4->text();
  msgtype(t, ui->tx4);
}

void MainWindow::on_tx5_editingFinished()                       //tx5 edited
{
  QString t=ui->tx5->text();
  msgtype(t, ui->tx5);
}

void MainWindow::on_tx6_editingFinished()                       //tx6 edited
{
  QString t=ui->tx6->text();
  msgtype(t, ui->tx6);
}

void MainWindow::on_setTxFreqButton_clicked()                  //Set Tx Freq
{
  m_setftx=1;
  m_txFreq=m_wide_graph_window->QSOfreq();
}

void MainWindow::on_dxCallEntry_textChanged(const QString &t) //dxCall changed
{
  m_hisCall=t.toUpper().trimmed();
  ui->dxCallEntry->setText(m_hisCall);
}

void MainWindow::on_dxGridEntry_textChanged(const QString &t) //dxGrid changed
{
  int n=t.length();
  if(n!=4 and n!=6) return;
  if(!t[0].isLetter() or !t[1].isLetter()) return;
  if(!t[2].isDigit() or !t[3].isDigit()) return;
  if(n==4) m_hisGrid=t.mid(0,2).toUpper() + t.mid(2,2);
  if(n==6) m_hisGrid=t.mid(0,2).toUpper() + t.mid(2,2) +
      t.mid(4,2).toLower();
  ui->dxGridEntry->setText(m_hisGrid);
}

void MainWindow::on_genStdMsgsPushButton_clicked()         //genStdMsgs button
{
  genStdMsgs("");
}

void MainWindow::on_logQSOButton_clicked()                 //Log QSO button
{
  int nMHz=getFcenter();
  QDateTime t = QDateTime::currentDateTimeUtc();
  QString qsoMode=lab5->text();
  if(m_modeTx.startsWith("Q65")) qsoMode=lab6->text();
  QString logEntry=t.date().toString("yyyy-MMM-dd,") +
      t.time().toString("hh:mm,") + m_hisCall + "," + m_hisGrid + "," +
          QString::number(nMHz) + "," + qsoMode + "\r\n";

  int ret = QMessageBox::warning(this, "Log Entry",
       "Please confirm log entry:\n\n" + logEntry + "\n",
       QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
  if(ret==QMessageBox::No) return;
  QFile f("wsjt.log");
  if(!f.open(QFile::Append)) {
    msgBox("Cannot open file \"wsjt.log\".");
    return;
  }
  QTextStream out(&f);
  out << logEntry;
  f.close();
  m_worked[m_hisCall]=true;
}

void MainWindow::on_actionErase_map65_rx_log_triggered()     //Erase Rx log
{
  int ret = QMessageBox::warning(this, "Confirm Erase",
      "Are you sure you want to erase file map65_rx.log ?",
       QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
  if(ret==QMessageBox::Yes) {
    m_map65RxLog |= 2;                      // Rewind map65_rx.log
  }
}

void MainWindow::on_actionErase_map65_tx_log_triggered()     //Erase Tx log
{
  int ret = QMessageBox::warning(this, "Confirm Erase",
      "Are you sure you want to erase file map65_tx.log ?",
       QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
  if(ret==QMessageBox::Yes) {
    QFile f("map65_tx.log");
    f.remove();
  }
}

void MainWindow::on_actionNoJT65_triggered()
{
  m_mode65=0;
  m_modeJT65=0;
  m_TRperiod=60;
  soundInThread.setPeriod(m_TRperiod);
  soundOutThread.setPeriod(m_TRperiod);
  m_wide_graph_window->setMode65(m_mode65);
  m_wide_graph_window->setPeriod(m_TRperiod);
  lab5->setStyleSheet("");
  lab5->setText("");
}
void MainWindow::on_actionJT65A_triggered()
{
  m_mode="JT65A";
  m_modeJT65=1;
  m_mode65=1;
  m_TRperiod=60;
  soundInThread.setPeriod(m_TRperiod);
  soundOutThread.setPeriod(m_TRperiod);
  m_wide_graph_window->setMode65(m_mode65);
  m_wide_graph_window->setPeriod(m_TRperiod);
  lab5->setStyleSheet("QLabel{background-color: #ff6666}");
  lab5->setText("JT65A");
  ui->actionJT65A->setChecked(true);
}

void MainWindow::on_actionJT65B_triggered()
{
  m_mode="JT65B";
  m_modeJT65=2;
  m_mode65=2;
  m_TRperiod=60;
  soundInThread.setPeriod(m_TRperiod);
  soundOutThread.setPeriod(m_TRperiod);
  m_wide_graph_window->setMode65(m_mode65);
  m_wide_graph_window->setPeriod(m_TRperiod);
  lab5->setStyleSheet("QLabel{background-color: #ffff66}");
  lab5->setText("JT65B");
  ui->actionJT65B->setChecked(true);
}

void MainWindow::on_actionJT65C_triggered()
{
  m_mode="JT65C";
  m_modeJT65=3;
  m_mode65=4;
  m_TRperiod=60;
  soundInThread.setPeriod(m_TRperiod);
  soundOutThread.setPeriod(m_TRperiod);
  m_wide_graph_window->setMode65(m_mode65);
  m_wide_graph_window->setPeriod(m_TRperiod);
  lab5->setStyleSheet("QLabel{background-color: #66ffb2}");
  lab5->setText("JT65C");
  ui->actionJT65C->setChecked(true);
}

void MainWindow::on_actionNoQ65_triggered()
{
  m_modeQ65=0;
  lab6->setStyleSheet("");
  lab6->setText("");
}

void MainWindow::on_actionQ65A_triggered()
{
  m_modeQ65=1;
  lab6->setStyleSheet("QLabel{background-color: #ffb266}");
  lab6->setText("Q65A");
}

void MainWindow::on_actionQ65B_triggered()
{
  m_modeQ65=2;
  lab6->setStyleSheet("QLabel{background-color: #b2ff66}");
  lab6->setText("Q65B");
}


void MainWindow::on_actionQ65C_triggered()
{
  m_modeQ65=3;
  lab6->setStyleSheet("QLabel{background-color: #66ffff}");
  lab6->setText("Q65C");
}

void MainWindow::on_actionQ65D_triggered()
{
  m_modeQ65=4;
  lab6->setStyleSheet("QLabel{background-color: #b266ff}");
  lab6->setText("Q65D");
}

void MainWindow::on_actionQ65E_triggered()
{
  m_modeQ65=5;
  lab6->setStyleSheet("QLabel{background-color: #ff66ff}");
  lab6->setText("Q65E");
}


void MainWindow::on_NBcheckBox_toggled(bool checked)
{
  m_NB=checked;
  ui->NBslider->setEnabled(m_NB);
}

void MainWindow::on_NBslider_valueChanged(int n)
{
  m_NBslider=n;
}

void MainWindow::on_actionAdjust_IQ_Calibration_triggered()
{
  m_adjustIQ=1;
}

void MainWindow::on_actionApply_IQ_Calibration_triggered()
{
  m_applyIQcal= 1-m_applyIQcal;
}

void MainWindow::on_actionFUNcube_Dongle_triggered()
{
  proc_qthid.start (QDir::toNativeSeparators(m_appDir + "/qthid"), QStringList {});
}

void MainWindow::on_actionEdit_wsjt_log_triggered()
{
  proc_editor.start (QDir::toNativeSeparators (m_editorCommand),
                     {QDir::toNativeSeparators (QDir {m_dataDir}.absoluteFilePath ("wsjt.log")), });
}

void MainWindow::on_actionTx_Tune_triggered()
{
  if (!g_pTxTune) {
    g_pTxTune = new TxTune(0);

    if (!g_TxTuneGeometry.isEmpty())
      g_pTxTune->restoreGeometry(g_TxTuneGeometry);
  }
  g_pTxTune->set_iqAmp(iqAmp);
  g_pTxTune->set_iqPhase(iqPhase);
  g_pTxTune->set_txPower(txPower);
  g_pTxTune->show();
}

void MainWindow::on_pbTxMode_clicked()
{
  if(m_modeTx=="Q65") {
    m_modeTx="JT65";
    ui->pbTxMode->setText("Tx JT65   #");
  } else {
    m_modeTx="Q65";
    ui->pbTxMode->setText("Tx Q65  :");
  }
//  m_wideGraph->setModeTx(m_modeTx);
//  statusChanged();
}

bool MainWindow::isGrid4(QString g)
{
  if(g.length()!=4) return false;
  if(g.mid(0,1)<'A' or g.mid(0,1)>'R') return false;
  if(g.mid(1,1)<'A' or g.mid(1,1)>'R') return false;
  if(g.mid(2,1)<'0' or g.mid(2,1)>'9') return false;
  if(g.mid(3,1)<'0' or g.mid(3,1)>'9') return false;
  return true;
}

void MainWindow::read_log()
{
  // Update "m_worked" by reading wsjtx.log
  m_worked.clear();                     //Start from scratch
  QFile f("wsjtx.log");
//  qDebug() << "MainWindow::read_log File open result:" << f.open(QFileDevice::ReadOnly);
  if(f.isOpen()) {
    QTextStream in(&f);
    QString line,callsign;
    for(int i=0; i<99999; i++) {
      line=in.readLine();
      if(line.length()<=0) break;
      callsign=line.mid(40,6);
      int n=callsign.indexOf(",");
      if(n>0) callsign=callsign.left(n);
      m_worked[callsign]=true;
    }
    f.close();
  }
}

void pa_deinit()
{
    Pa_Terminate();
}
