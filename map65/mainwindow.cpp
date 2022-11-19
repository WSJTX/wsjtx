//------------------------------------------------------------------ MainWindow
#include "mainwindow.h"
#include <fftw3.h>
#include <QDir>
#include <QSettings>
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
#include <portaudio.h>

#define NFFT 32768

short int iwave[2*60*12000];          //Wave file for Tx audio
int nwave;                            //Length of Tx waveform
bool btxok;                           //True if OK to transmit
bool bTune;
bool bIQxt;
double outputLatency;                 //Latency in seconds
int txPower;
int iqAmp;
int iqPhase;
qint16 id[4*60*96000];

TxTune*    g_pTxTune = NULL;
QSharedMemory mem_m65("mem_m65");

extern const int RxDataFrequency = 96000;
extern const int TxDataFrequency = 11025;

//-------------------------------------------------- MainWindow constructor
MainWindow::MainWindow(QWidget *parent) :
  QMainWindow(parent),
  ui(new Ui::MainWindow),
  m_appDir {QApplication::applicationDirPath ()},
  m_settings_filename {m_appDir + "/map65.ini"},
  m_astro_window {new Astro {m_settings_filename}},
  m_band_map_window {new BandMap {m_settings_filename}},
  m_messages_window {new Messages {m_settings_filename}},
  m_wide_graph_window {new WideGraph {m_settings_filename}},
  m_gui_timer {new QTimer {this}}
{
  ui->setupUi(this);
  on_EraseButton_clicked();
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
  connect(txMsgButtonGroup,SIGNAL(buttonClicked(int)),SLOT(set_ntx(int)));
  connect(ui->decodedTextBrowser,SIGNAL(selectCallsign(bool)),this,
          SLOT(selectCall2(bool)));

  setWindowTitle (program_title ());

  connect(&soundInThread, SIGNAL(readyForFFT(int)),
             this, SLOT(dataSink(int)));
  connect(&soundInThread, SIGNAL(error(QString)), this,
          SLOT(showSoundInError(QString)));
  connect(&soundInThread, SIGNAL(status(QString)), this,
          SLOT(showStatusMessage(QString)));
  createStatusBar();

  connect(&proc_m65, SIGNAL(readyReadStandardOutput()), this, SLOT(readFromStdout()));
  connect(&proc_m65, &QProcess::errorOccurred, this, &MainWindow::m65_error);
  connect(&proc_m65, static_cast<void (QProcess::*) (int, QProcess::ExitStatus)> (&QProcess::finished),
          [this] (int exitCode, QProcess::ExitStatus status) {
            if (subProcessFailed (&proc_m65, exitCode, status))
              {
                QTimer::singleShot (0, this, SLOT (close ()));
              }
          });

  connect(&proc_editor, SIGNAL(error(QProcess::ProcessError)),
          this, SLOT(editor_error()));

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
  m_saveDir="/users/joe/map65/install/save";
  m_azelDir="/users/joe/map65/install/";
  m_editorCommand="notepad";
  m_txFreq=125;
  m_setftx=0;
  m_loopall=false;
  m_startAnother=false;
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

#ifdef WIN32
  while(true) {
      int iret=killbyname("m65.exe");
      if(iret == 603) break;
      if(iret != 0) msgBox("KillByName return code: " +
                           QString::number(iret));
  }
#endif

  if(!mem_m65.attach()) {
    if (!mem_m65.create(sizeof(datcom_))) {
      msgBox("Unable to create shared memory segment.");
    }
  }
  char *to = (char*)mem_m65.data();
  int size=sizeof(datcom_);
  if(datcom_.newdat==0) {
    int noffset = 4*4*5760000 + 4*4*322*32768 + 4*4*32768;
    to += noffset;
    size -= noffset;
  }
  memset(to,0,size);         //Zero all decoding params in shared memory

  fftwf_import_wisdom_from_filename (QDir {m_appDir}.absoluteFilePath ("map65_wisdom.dat").toLocal8Bit ());

  PaError paerr=Pa_Initialize();                    //Initialize Portaudio
  if(paerr!=paNoError) {
    msgBox("Unable to initialize PortAudio.");
  }
  readSettings();		             //Restore user's setup params
  QFile lockFile(m_appDir + "/.lock"); //Create .lock so m65 will wait
  lockFile.open(QIODevice::ReadWrite);
  QFile quitFile(m_appDir + "/.quit");
  quitFile.remove();
  proc_m65.start(QDir::toNativeSeparators(m_appDir + "/m65"), {"-s", });

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
  on_actionMessages_triggered();
  on_actionBand_Map_triggered();
  if (m_messages_window) m_messages_window->setColors(m_colors);
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
//  soundOutThread.start(QThread::HighPriority);

  m_monitoring=true;                           // Start with Monitoring ON
  soundInThread.setMonitoring(m_monitoring);
  m_diskData=false;
  m_tol=500;
  m_wide_graph_window->setTol(m_tol);
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
  f.open(QIODevice::ReadOnly);
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

  connect (m_messages_window.get (), &Messages::click2OnCallsign, this, &MainWindow::doubleClickOnMessages);
  connect (m_wide_graph_window.get (), &WideGraph::freezeDecode2, this, &MainWindow::freezeDecode);
  connect (m_wide_graph_window.get (), &WideGraph::f11f12, this, &MainWindow::bumpDF);

  // only start the guiUpdate timer after this constructor has finished
  QTimer::singleShot (0, [=] {
                           m_gui_timer->start(100); //Don't change the 100 ms!
                         });
}

  //--------------------------------------------------- MainWindow destructor
MainWindow::~MainWindow()
{
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
  fftwf_export_wisdom_to_filename (QDir {m_appDir}.absoluteFilePath ("map65_wisdom.dat").toLocal8Bit ());
  if(!m_decoderBusy) {
    QFile lockFile(m_appDir + "/.lock");
    lockFile.remove();
  }
  delete ui;
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

  SettingsGroup g {&settings, "Common"};
  settings.setValue("MyCall",m_myCall);
  settings.setValue("MyGrid",m_myGrid);
  settings.setValue("IDint",m_idInt);
  settings.setValue("PTTport",m_pttPort);
  settings.setValue("AstroFont",m_astroFont);
  settings.setValue("Xpol",m_xpol);
  settings.setValue("XpolX",m_xpolx);
  settings.setValue("SaveDir",m_saveDir);
  settings.setValue("AzElDir",m_azelDir);
  settings.setValue("Editor",m_editorCommand);
  settings.setValue("DXCCpfx",m_dxccPfx);
  settings.setValue("Timeout",m_timeout);
  settings.setValue("TxPower",txPower);
  settings.setValue("IQamp",iqAmp);
  settings.setValue("IQphase",iqPhase);
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
    m_path = settings.value("MRUdir", m_appDir + "/save").toString();
    m_txFirst = settings.value("TxFirst",false).toBool();
    ui->txFirstCheckBox->setChecked(m_txFirst);
  }

  SettingsGroup g {&settings, "Common"};
  m_myCall=settings.value("MyCall","").toString();
  m_myGrid=settings.value("MyGrid","").toString();
  m_idInt=settings.value("IDint",0).toInt();
  m_pttPort=settings.value("PTTport",0).toInt();
  m_astroFont=settings.value("AstroFont",20).toInt();
  m_xpol=settings.value("Xpol",false).toBool();
  ui->actionFind_Delta_Phi->setEnabled(m_xpol);
  m_xpolx=settings.value("XpolX",false).toBool();
  m_saveDir=settings.value("SaveDir",m_appDir + "/save").toString();
  m_azelDir=settings.value("AzElDir",m_appDir).toString();
  m_editorCommand=settings.value("Editor","notepad").toString();
  m_dxccPfx=settings.value("DXCCpfx","").toString();
  m_timeout=settings.value("Timeout",20).toInt();
  txPower=settings.value("TxPower",100).toInt();
  iqAmp=settings.value("IQamp",0).toInt();
  iqPhase=settings.value("IQphase",0).toInt();
  m_applyIQcal=settings.value("ApplyIQcal",0).toInt();
  ui->actionApply_IQ_Calibration->setChecked(m_applyIQcal!=0);
  m_dPhi=settings.value("dPhi",0).toInt();
  m_fCal=settings.value("Fcal",0).toInt();
  m_fAdd=settings.value("FAdd",0).toDouble();
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
  static int nfsample=96000;
  static int nxpol=0;
  static int nsec0=0;
  static int nsum=0;
  static float fgreen;
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
    datcom_.ndiskdat=1;
  } else {
    ndiskdat=0;
    datcom_.ndiskdat=0;
  }
// Get x and y power, polarized spectrum, nkhz, and ihsym
  nb=0;
  if(m_NB) nb=1;
  nfsample=96000;
  if(!m_fs96000) nfsample=95238;
  nxpol=0;
  if(m_xpol) nxpol=1;
  fgreen=m_wide_graph_window->fGreen();
  nadj++;
  if(m_adjustIQ==0) nadj=0;
  symspec_(&k, &nxpol, &ndiskdat, &nb, &m_NBslider, &m_dPhi,
           &nfsample, &fgreen, &m_adjustIQ, &m_applyIQcal,
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
                                     QString {"Amp: %1   Phase: %1"}
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
    int ntr = (ms/1000) % m_TRperiod;
    if((m_diskData && ihsym <= m_waterfallAvg) || (!m_diskData && ntr<ntrz)) {
      for (int i=0; i<NFFT; i++) {
        splot[i] = 1.e30;
      }
    }
    ntrz=ntr;
    n=0;
  }

  if(ihsym<280) m_RxState=0;

  if(m_RxState==0 and ihsym>=280 and !m_diskData) {   //Early decode, t=52 s
    m_RxState=1;
    datcom_.newdat=1;
    datcom_.nagain=0;
    datcom_.nhsym=ihsym;
    QDateTime t = QDateTime::currentDateTimeUtc();
    m_dateTime=t.toString("yyyy-MMM-dd hh:mm");
    decode();                                           //Start the decoder
  }

  if(m_RxState<=1 and ihsym>=302) {   //Decode at t=56 s (for Q65 and data from disk)
    m_RxState=2;
    datcom_.newdat=1;
    datcom_.nagain=0;
    datcom_.nhsym=ihsym;
    QDateTime t = QDateTime::currentDateTimeUtc();
    m_dateTime=t.toString("yyyy-MMM-dd hh:mm");
    decode();                                           //Start the decoder
    if(m_saveAll and !m_diskData) {
      QString fname=m_saveDir + "/" + t.date().toString("yyMMdd") + "_" +
          t.time().toString("hhmm");
      if(m_xpol) fname += ".tf2";
      if(!m_xpol) fname += ".iq";
      *future2 = QtConcurrent::run(savetf2, fname, m_xpol);
      watcher2->setFuture(*future2);
    }
  }

  soundInThread.m_dataSinkBusy=false;
}

void MainWindow::showSoundInError(const QString& errorMsg)
 {QMessageBox::critical(this, tr("Error in SoundIn"), errorMsg);}

void MainWindow::showStatusMessage(const QString& statusMsg)
 {statusBar()->showMessage(statusMsg);}

void MainWindow::on_actionDeviceSetup_triggered()               //Setup Dialog
{
  DevSetup dlg(this);
  dlg.m_myCall=m_myCall;
  dlg.m_myGrid=m_myGrid;
  dlg.m_idInt=m_idInt;
  dlg.m_pttPort=m_pttPort;
  dlg.m_astroFont=m_astroFont;
  dlg.m_xpol=m_xpol;
  dlg.m_xpolx=m_xpolx;
  dlg.m_saveDir=m_saveDir;
  dlg.m_azelDir=m_azelDir;
  dlg.m_editorCommand=m_editorCommand;
  dlg.m_dxccPfx=m_dxccPfx;
  dlg.m_timeout=m_timeout;
  dlg.m_dPhi=m_dPhi;
  dlg.m_fCal=m_fCal;
  dlg.m_fAdd=m_fAdd;
  dlg.m_network=m_network;
  dlg.m_fs96000=m_fs96000;
  dlg.m_nDevIn=m_nDevIn;
  dlg.m_nDevOut=m_nDevOut;
  dlg.m_udpPort=m_udpPort;
  dlg.m_IQswap=m_IQswap;
  dlg.m_dB=m_dB;
  dlg.m_initIQplus=m_initIQplus;
  dlg.m_bIQxt=m_bIQxt;
  dlg.m_cal570=m_cal570;
  dlg.m_TxOffset=m_TxOffset;
  dlg.m_mult570=m_mult570;
  dlg.m_mult570Tx=m_mult570Tx;
  dlg.m_colors=m_colors;

  dlg.initDlg();
  if(dlg.exec() == QDialog::Accepted) {
    m_myCall=dlg.m_myCall;
    m_myGrid=dlg.m_myGrid;
    m_idInt=dlg.m_idInt;
    m_pttPort=dlg.m_pttPort;
    m_astroFont=dlg.m_astroFont;
    if(m_astro_window && m_astro_window->isVisible()) m_astro_window->setFontSize(m_astroFont);
    m_xpol=dlg.m_xpol;
    ui->actionFind_Delta_Phi->setEnabled(m_xpol);
    m_xpolx=dlg.m_xpolx;
    m_saveDir=dlg.m_saveDir;
    m_azelDir=dlg.m_azelDir;
    m_editorCommand=dlg.m_editorCommand;
    m_dxccPfx=dlg.m_dxccPfx;
    m_timeout=dlg.m_timeout;
    m_dPhi=dlg.m_dPhi;
    m_fCal=dlg.m_fCal;
    m_fAdd=dlg.m_fAdd;
    m_wide_graph_window->setFcal(m_fCal);
    m_fs96000=dlg.m_fs96000;
    m_network=dlg.m_network;
    m_nDevIn=dlg.m_nDevIn;
    m_paInDevice=dlg.m_paInDevice;
    m_nDevOut=dlg.m_nDevOut;
    m_paOutDevice=dlg.m_paOutDevice;
    m_udpPort=dlg.m_udpPort;
    m_IQswap=dlg.m_IQswap;
    m_dB=dlg.m_dB;
    m_initIQplus=dlg.m_initIQplus;
    m_bIQxt=dlg.m_bIQxt;
    m_colors=dlg.m_colors;
    m_messages_window->setColors(m_colors);
    m_band_map_window->setColors(m_colors);
    m_cal570=dlg.m_cal570;
    m_TxOffset=dlg.m_TxOffset;
    m_mult570Tx=dlg.m_mult570Tx;
    m_wide_graph_window->m_mult570=m_mult570;
    m_wide_graph_window->m_mult570Tx=m_mult570Tx;
    m_wide_graph_window->m_cal570=m_cal570;
    soundInThread.setSwapIQ(m_IQswap);
    soundInThread.setScale(m_dB);

    if(dlg.m_restartSoundIn) {
      soundInThread.quit();
      soundInThread.wait(1000);
      soundInThread.setNetwork(m_network);
      if(m_fs96000) soundInThread.setRate(96000.0);
      if(!m_fs96000) soundInThread.setRate(95238.1);
      soundInThread.setFadd(m_fAdd);
      if(!m_xpol) soundInThread.setNrx(1);
      if(m_xpol) soundInThread.setNrx(2);
      soundInThread.setInputDevice(m_paInDevice);
      soundInThread.start(QThread::HighestPriority);
    }

    if(dlg.m_restartSoundOut) {
      soundOutThread.quitExecution=true;
      soundOutThread.wait(1000);
      soundOutThread.setOutputDevice(m_paOutDevice);
//      soundOutThread.start(QThread::HighPriority);
    }
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

void MainWindow::closeEvent (QCloseEvent * e)
{
  if (m_gui_timer) m_gui_timer->stop ();
  m_wide_graph_window->saveSettings();
  QFile quitFile(m_appDir + "/.quit");
  quitFile.open(QIODevice::ReadWrite);
  QFile lockFile(m_appDir + "/.lock");
  lockFile.remove();                      // Allow m65 to terminate

  // close pipes
  proc_m65.closeReadChannel (QProcess::StandardOutput);
  proc_m65.closeReadChannel (QProcess::StandardError);

  // flush all input
  proc_m65.setReadChannel (QProcess::StandardOutput);
  proc_m65.readAll ();
  proc_m65.setReadChannel (QProcess::StandardError);
  proc_m65.readAll ();

  proc_m65.disconnect ();
  if (!proc_m65.waitForFinished (1000)) proc_m65.kill();
  quitFile.remove();
  mem_m65.detach();
  if (m_astro_window) m_astro_window->close ();
  if (m_band_map_window) m_band_map_window->close ();
  if (m_messages_window) m_messages_window->close ();
  if (m_wide_graph_window) m_wide_graph_window->close ();
  QMainWindow::closeEvent (e);
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
  "https://www.physics.princeton.edu/pulsar/K1JT/Release_Notes.txt",
                              QUrl::TolerantMode));
}

void MainWindow::on_actionOnline_Users_Guide_triggered()      //Display manual
{
  QDesktopServices::openUrl(QUrl(
  "https://www.physics.princeton.edu/pulsar/K1JT/MAP65_Users_Guide.pdf",
                              QUrl::TolerantMode));
}

void MainWindow::on_actionQSG_Q65_triggered()
{
  QDesktopServices::openUrl (QUrl {"https://physics.princeton.edu/pulsar/k1jt/Q65_Quick_Start.pdf"});
}

void MainWindow::on_actionQSG_MAP65_v3_triggered()
{
  QDesktopServices::openUrl (QUrl {"https://physics.princeton.edu/pulsar/k1jt/WSJTX_2.5.0_MAP65_3.0_Quick_Start.pdf"});
}

void MainWindow::on_actionQ65_Sensitivity_in_MAP65_3_0_triggered()
{
  QDesktopServices::openUrl (QUrl {"https://physics.princeton.edu/pulsar/k1jt/Q65_Sensitivity_in_MAP65.pdf"});
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
    int dbDgrd=0;
    if(m_myCall=="K1JT" and m_idInt<0) dbDgrd=m_idInt;
    *future1 = QtConcurrent::run(getfile, fname, m_xpol, dbDgrd);
    watcher1->setFuture(*future1);
  }
}

void MainWindow::on_actionOpen_next_in_directory_triggered()   //Open Next
{
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
    len=list.at(i).length();
    if(list.at(i)==m_path.right(len)) {
      int n=m_path.length();
      QString fname=m_path.replace(n-len,len,list.at(i+1));
      m_path=fname;
      int i;
      i=fname.indexOf(".iq") - 11;
      if(m_xpol) i=fname.indexOf(".tf2") - 11;
      if(i>=0) {
        lab1->setStyleSheet("QLabel{background-color: #66ff66}");
        lab1->setText(" " + fname.mid(i,len) + " ");
      }
      m_diskData=true;
      int dbDgrd=0;
      if(m_myCall=="K1JT" and m_idInt<0) dbDgrd=m_idInt;
      *future1 = QtConcurrent::run(getfile, fname, m_xpol, dbDgrd);
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
  datcom_.newdat=1;
  if(m_wide_graph_window->m_bForceCenterFreq) {
    datcom_.fcenter=m_wide_graph_window->m_dForceCenterFreq;
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
    datcom_.newdat=0;
    datcom_.nagain=1;
    decode();
  }
}

void MainWindow::freezeDecode(int n)                          //freezeDecode()
{
  if(n==2) {
    ui->tolSpinBox->setValue(5);
    datcom_.ntol=m_tol;
    datcom_.mousedf=0;
  } else {
    ui->tolSpinBox->setValue(qMin(3,ui->tolSpinBox->value()));
    datcom_.ntol=m_tol;
  }
  if(!m_decoderBusy) {
    datcom_.nagain=1;
    datcom_.newdat=0;
    decode();
  }
}

void MainWindow::decode()                                       //decode()
{
  ui->DecodeButton->setStyleSheet(m_pbdecoding_style1);

//  QFile f("mockRTfiles.txt");
//  if(datcom_.nagain==0 && (!m_diskData) && !f.exists()) {
  if(datcom_.nagain==0 && (!m_diskData)) {
    qint64 ms = QDateTime::currentMSecsSinceEpoch() % 86400000;
    int imin=ms/60000;
    int ihr=imin/60;
    imin=imin % 60;
    datcom_.nutc=100*ihr + imin;
  }

  datcom_.idphi=m_dPhi;
  datcom_.mousedf=m_wide_graph_window->DF();
  datcom_.mousefqso=m_wide_graph_window->QSOfreq();
  datcom_.ndepth=m_ndepth;
  datcom_.ndiskdat=0;
  if(m_diskData) datcom_.ndiskdat=1;
  datcom_.neme=0;
  if(ui->actionOnly_EME_calls->isChecked()) datcom_.neme=1;

  int ispan=int(m_wide_graph_window->fSpan());
  if(ispan%2 == 1) ispan++;
  int ifc=int(1000.0*(datcom_.fcenter - int(datcom_.fcenter))+0.5);
  int nfa=m_wide_graph_window->nStartFreq();
  int nfb=nfa+ispan;
  int nfshift=nfa + ispan/2 - ifc;

  datcom_.nfa=nfa;
  datcom_.nfb=nfb;
  datcom_.nfcal=m_fCal;
  datcom_.nfshift=nfshift;
  datcom_.mcall3=0;
  if(m_call3Modified) datcom_.mcall3=1;
  datcom_.ntimeout=m_timeout;
  datcom_.ntol=m_tol;
  datcom_.nxant=0;
  if(m_xpolx) datcom_.nxant=1;
  if(datcom_.nutc < m_nutc0) m_map65RxLog |= 1;  //Date and Time to map65_rx.log
  m_nutc0=datcom_.nutc;
  datcom_.map65RxLog=m_map65RxLog;
  datcom_.nfsample=96000;
  if(!m_fs96000) datcom_.nfsample=95238;
  datcom_.nxpol=0;
  if(m_xpol) datcom_.nxpol=1;
  datcom_.nmode=10*m_modeQ65 + m_modeJT65;
  datcom_.nfast=1;                               //No longer used
  datcom_.nsave=m_nsave;
  datcom_.max_drift=ui->sbMaxDrift->value();

  QString mcall=(m_myCall+"            ").mid(0,12);
  QString mgrid=(m_myGrid+"            ").mid(0,6);
  QString hcall=(ui->dxCallEntry->text()+"            ").mid(0,12);
  QString hgrid=(ui->dxGridEntry->text()+"      ").mid(0,6);

  memcpy(datcom_.mycall, mcall.toLatin1(), 12);
  memcpy(datcom_.mygrid, mgrid.toLatin1(), 6);
  memcpy(datcom_.hiscall, hcall.toLatin1(), 12);
  memcpy(datcom_.hisgrid, hgrid.toLatin1(), 6);
  memcpy(datcom_.datetime, m_dateTime.toLatin1(), 17);
  datcom_.junk1=1234;
  datcom_.junk2=5678;

  //newdat=1  ==> this is new data, must do the big FFT
  //nagain=1  ==> decode only at fQSO +/- Tol

  char *to = (char*)mem_m65.data();
  char *from = (char*) datcom_.d4;
  int size=sizeof(datcom_);
  if(datcom_.newdat==0) {
    int noffset = 4*4*5760000 + 4*4*322*32768 + 4*4*32768;
    to += noffset;
    from += noffset;
    size -= noffset;
  }
  memcpy(to, from, qMin(mem_m65.size(), size-8));
  datcom_.nagain=0;
  datcom_.ndiskdat=0;
  m_map65RxLog=0;
  m_call3Modified=false;

  QFile lockFile(m_appDir + "/.lock");       // Allow m65 to start
  lockFile.remove();
  decodeBusy(true);
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

void MainWindow::m65_error (QProcess::ProcessError)
{
  msgBox("Error starting or running\n" + m_appDir + "/m65 -s\n\n"
         + proc_m65.errorString ());
  QTimer::singleShot (0, this, SLOT (close ()));
}

void MainWindow::editor_error()                                 //editor_error
{
  msgBox("Error starting or running\n" + m_appDir + "/" + m_editorCommand);
}

void MainWindow::readFromStdout()                             //readFromStdout
{
  while(proc_m65.canReadLine())
  {
    QByteArray t=proc_m65.readLine();
    if(t.indexOf("<QuickDecodeDone>") >= 0) {
      m_nsum=t.mid(17,4).toInt();
      m_nsave=t.mid(21,4).toInt();
      lab7->setText (QString {"Avg: %1"}.arg (m_nsum));
      if(m_modeQ65>0) m_wide_graph_window->setDecodeFinished();
    }

    if((t.indexOf("<EarlyFinished>") >= 0) or (t.indexOf("<DecodeFinished>") >= 0)) {
      if(m_widebandDecode) {
        m_messages_window->setText(m_messagesText,m_bandmapText);
        m_band_map_window->setText(m_bandmapText);
        m_widebandDecode=false;
      }
      QFile lockFile(m_appDir + "/.lock");
      lockFile.open(QIODevice::ReadWrite);
      if(t.indexOf("<DecodeFinished>") >= 0) {
        int ndecodes=t.mid(40,5).toInt();
        lab5->setText(QString::number(ndecodes));
        m_map65RxLog=0;
        m_startAnother=m_loopall;
      }
      ui->DecodeButton->setStyleSheet("");
      decodeBusy(false);
      return;
    }

    read_log();

    if(t.indexOf("!") >= 0) {
      int n=t.length();
      int m=2;
#ifdef WIN32
      m=3;
#endif
      if(n>=30 or t.indexOf("Best-fit")>=0) ui->decodedTextBrowser->append(t.mid(1,n-m));
      n=ui->decodedTextBrowser->verticalScrollBar()->maximum();
      ui->decodedTextBrowser->verticalScrollBar()->setValue(n);
      m_messagesText="";
      m_bandmapText="";
    }

    if(t.indexOf("@") >= 0) {
      m_messagesText += t.mid(1);
      m_widebandDecode=true;
    }

    if(t.indexOf("&") >= 0) {
      QString q(t);
      QString callsign=q.mid(5);
      callsign=callsign.mid(0,callsign.indexOf(" "));
      if(callsign.length()>2) {
        if(m_worked[callsign]) {
          q=q.mid(1,4) + "  " + q.mid(5);
        } else {
          q=q.mid(1,4) + " *" + q.mid(5);
        }
        m_bandmapText += q;
      }
    }
    if(t.indexOf("=") >= 0) {
      int n=t.size();
      qDebug() << t.mid(1,n-3).trimmed();
    }
  }
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
    if((bTxTime or bTune) and iptt==0 and !m_txMute) {
      int itx=1;
      int ierr = ptt_(&m_pttPort,&itx,&iptt);       // Raise PTT
      if(ierr != 0) {
        on_stopTxButton_clicked();
        char s[18];
        sprintf(s,"Cannot open COM%d",m_pttPort);
        msgBox(s);
      }

      if(m_bIQxt) m_wide_graph_window->tx570();     // Set Si570 to Tx Freq

      if(!soundOutThread.isRunning()) {
        soundOutThread.start(QThread::HighPriority);
      }
    }
    if((!bTxTime and !bTune) or m_txMute) {
      btxok=false;
    }
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
      f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append);
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
    f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append);
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
    int itx=0;
    ptt_(&m_pttPort,&itx,&iptt);       // Lower PTT
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

  if(m_startAnother) {
    m_startAnother=false;
    on_actionOpen_next_in_directory_triggered();
  }
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
      sprintf(s,"Tx: %s",msgsent);
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
    m_astro_window->astroUpdate(t, m_myGrid, m_hisGrid, fQSO, m_setftx,
                          m_txFreq, m_azelDir, m_xavg);
    m_setftx=0;
    QString utc = t.date().toString(" yyyy MMM dd \n") + t.time().toString();
    ui->labUTC->setText(utc);
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

void MainWindow::lookup()                                       //lookup()
{
  QString hiscall=ui->dxCallEntry->text().toUpper().trimmed();
  ui->dxCallEntry->setText(hiscall);
  QString call3File = m_appDir + "/CALL3.TXT";
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
  QString call3File = m_appDir + "/CALL3.TXT";
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

  QString tmpFile = m_appDir + "/CALL3.TMP";
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
    auto const& old_path = m_appDir + "/CALL3.OLD";
    QFile f0 {old_path};
    if (f0.exists ()) f0.remove ();
    f1.copy (old_path);         // copying as we want to preserve
                                // symlinks
    f1.open (QFile::WriteOnly | QFile::Text); // truncates
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
  int nMHz=int(datcom_.fcenter);
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
  m_modeJT65=1;
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
  m_modeJT65=2;
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
  proc_editor.start (QDir::toNativeSeparators (m_editorCommand), {QDir::toNativeSeparators (m_appDir + "/wsjt.log"), });
}

void MainWindow::on_actionTx_Tune_triggered()
{
  if(g_pTxTune==NULL) {
    g_pTxTune = new TxTune(0);
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
  f.open(QIODevice::ReadOnly);
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
