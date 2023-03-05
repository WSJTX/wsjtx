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
#include "sleep.h"

#define NFFT 32768

qint16 id[2*60*96000];

QSharedMemory mem_qmap("mem_qmap");            //Memory segment to be shared (optionally) with WSJT-X
int* ipc_wsjtx;

extern const int RxDataFrequency = 96000;

//-------------------------------------------------- MainWindow constructor
MainWindow::MainWindow(QWidget *parent) :
  QMainWindow(parent),
  ui(new Ui::MainWindow),
  m_appDir {QApplication::applicationDirPath ()},
  m_settings_filename {m_appDir + "/qmap.ini"},
  m_astro_window {new Astro {m_settings_filename}},
  m_wide_graph_window {new WideGraph {m_settings_filename}},
  m_gui_timer {new QTimer {this}}
{
  ui->setupUi(this);
//  ui->decodedTextBrowser->clear();
  ui->labUTC->setStyleSheet( \
        "QLabel { background-color : black; color : yellow; }");
  ui->labFreq->setStyleSheet( \
        "QLabel { background-color : black; color : yellow; }");
  ui->labTol1->setStyleSheet( \
        "QLabel { background-color : white; color : black; }");
  ui->labTol1->setFrameStyle(QFrame::Panel | QFrame::Sunken);

  QActionGroup* paletteGroup = new QActionGroup(this);
  ui->actionCuteSDR->setActionGroup(paletteGroup);
  ui->actionLinrad->setActionGroup(paletteGroup);
  ui->actionAFMHot->setActionGroup(paletteGroup);
  ui->actionBlue->setActionGroup(paletteGroup);

  QActionGroup* modeGroup2 = new QActionGroup(this);
  ui->actionQ65A->setActionGroup(modeGroup2);
  ui->actionQ65B->setActionGroup(modeGroup2);
  ui->actionQ65C->setActionGroup(modeGroup2);
  ui->actionQ65D->setActionGroup(modeGroup2);
  ui->actionQ65E->setActionGroup(modeGroup2);

  QActionGroup* saveGroup = new QActionGroup(this);
  ui->actionSave_all->setActionGroup(saveGroup);
  ui->actionNone->setActionGroup(saveGroup);

  setWindowTitle (program_title ());

  connect(&soundInThread, SIGNAL(readyForFFT(int)), this, SLOT(dataSink(int)));
  connect(&soundInThread, SIGNAL(error(QString)), this, SLOT(showSoundInError(QString)));
  connect(&soundInThread, SIGNAL(status(QString)), this, SLOT(showStatusMessage(QString)));
  createStatusBar();
  connect(m_gui_timer, &QTimer::timeout, this, &MainWindow::guiUpdate);

  m_waterfallAvg = 1;
  m_network = true;
  m_restart=false;
  m_myCall="K1JT";
  m_myGrid="FN20qi";
  m_saveDir="";
  m_azelDir="";
  m_loopall=false;
  m_startAnother=false;
  m_saveAll=false;
  m_onlyEME=false;
  m_sec0=-1;
  m_hsym0=-1;
  m_palette="CuteSDR";
  m_nutc0=9999;
  m_kb8rq=false;
  m_NB=false;
  m_mode="Q65";
  m_fs96000=true;
  m_udpPort=50004;
  m_nsave=0;
  m_modeQ65=0;
  m_TRperiod=60;

  xSignalMeter = new SignalMeter(ui->xMeterFrame);
  xSignalMeter->resize(50, 160);

//Attach or create a memory segment to be shared with WSJT-X.
  int memSize=4096;
  if(!mem_qmap.attach()) {
    if(!mem_qmap.create(memSize)) {
      msgBox("Unable to create shared memory segment mem_qmap.");
    }
  }
  ipc_wsjtx = (int*)mem_qmap.data();
  mem_qmap.lock();
  memset(ipc_wsjtx,0,memSize);         //Zero all of shared memory
  mem_qmap.unlock();

  fftwf_import_wisdom_from_filename (QDir {m_appDir}.absoluteFilePath ("qmap_wisdom.dat").toLocal8Bit ());

  readSettings();		             //Restore user's setup params

  m_pbdecoding_style1="QPushButton{background-color: cyan; \
      border-style: outset; border-width: 1px; border-radius: 5px; \
      border-color: black; min-width: 5em; padding: 3px;}";
  m_pbmonitor_style="QPushButton{background-color: #00ff00; \
      border-style: outset; border-width: 1px; border-radius: 5px; \
      border-color: black; min-width: 5em; padding: 3px;}";
  m_pbAutoOn_style="QPushButton{background-color: red; \
      border-style: outset; border-width: 1px; border-radius: 5px; \
      border-color: black; min-width: 5em; padding: 3px;}";

  on_actionAstro_Data_triggered();           //Create the other windows
  on_actionWide_Waterfall_triggered();
  if (m_astro_window) m_astro_window->setFontSize (m_astroFont);

  if(m_modeQ65==1) on_actionQ65A_triggered();
  if(m_modeQ65==2) on_actionQ65B_triggered();
  if(m_modeQ65==3) on_actionQ65C_triggered();
  if(m_modeQ65==4) on_actionQ65D_triggered();
  if(m_modeQ65==5) on_actionQ65E_triggered();

  future1 = new QFuture<void>;
  watcher1 = new QFutureWatcher<void>;
  connect(watcher1, SIGNAL(finished()),this,SLOT(diskDat()));

  future2 = new QFuture<void>;
  watcher2 = new QFutureWatcher<void>;
  connect(watcher2, SIGNAL(finished()),this,SLOT(diskWriteFinished()));

  connect(&watcher3, SIGNAL(finished()),this,SLOT(decoderFinished()));

// Assign input device and start input thread
  soundInThread.setRate(96000.0);
  soundInThread.setBufSize(10*7056);
  soundInThread.setNetwork(m_network);
  soundInThread.setPort(m_udpPort);
  soundInThread.setPeriod(m_TRperiod);
  soundInThread.start(QThread::HighestPriority);

  m_monitoring=true;                           // Start with Monitoring ON
  soundInThread.setMonitoring(m_monitoring);
  m_diskData=false;
  m_tol=500;
  m_wide_graph_window->setTol(m_tol);
  m_wide_graph_window->setFcal(m_fCal);
  m_wide_graph_window->setFsample(96000);

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
  int itimer=1;
  q65c_(&itimer);

  if (soundInThread.isRunning()) {
    soundInThread.quit();
    soundInThread.wait(3000);
  }
  fftwf_export_wisdom_to_filename (QDir {m_appDir}.absoluteFilePath ("qmap_wisdom.dat").toLocal8Bit ());
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
  }

  SettingsGroup g {&settings, "Common"};
  settings.setValue("MyCall",m_myCall);
  settings.setValue("MyGrid",m_myGrid);
  settings.setValue("IDint",m_idInt);
  settings.setValue("AstroFont",m_astroFont);
  settings.setValue("SaveDir",m_saveDir);
  settings.setValue("AzElDir",m_azelDir);
  settings.setValue("Timeout",m_timeout);
  settings.setValue("Fcal",m_fCal);
  settings.setValue("Fadd",m_fAdd);
  settings.setValue("NetworkInput", m_network);
  settings.setValue("FSam96000", m_fs96000);
  settings.setValue("paInDevice",m_paInDevice);
  settings.setValue("Scale_dB",m_dB);
  settings.setValue("UDPport",m_udpPort);
  settings.setValue("PaletteCuteSDR",ui->actionCuteSDR->isChecked());
  settings.setValue("PaletteLinrad",ui->actionLinrad->isChecked());
  settings.setValue("PaletteAFMHot",ui->actionAFMHot->isChecked());
  settings.setValue("PaletteBlue",ui->actionBlue->isChecked());
  settings.setValue("Mode",m_mode);
  settings.setValue("nModeQ65",m_modeQ65);
  settings.setValue("SaveNone",ui->actionNone->isChecked());
  settings.setValue("SaveAll",ui->actionSave_all->isChecked());
  settings.setValue("NEME",m_onlyEME);
  settings.setValue("KB8RQ",m_kb8rq);
  settings.setValue("NB",m_NB);
  settings.setValue("NBslider",m_NBslider);
  settings.setValue("GainX",(double)m_gainx);
  settings.setValue("GainY",(double)m_gainy);
  settings.setValue("PhaseX",(double)m_phasex);
  settings.setValue("PhaseY",(double)m_phasey);
  settings.setValue("MaxDrift",ui->sbMaxDrift->value());
}

//---------------------------------------------------------- readSettings()
void MainWindow::readSettings()
{
  QSettings settings(m_settings_filename, QSettings::IniFormat);
  {
    SettingsGroup g {&settings, "MainWindow"};
    restoreGeometry(settings.value("geometry").toByteArray());
    m_path = settings.value("MRUdir", m_appDir + "/save").toString();
  }

  SettingsGroup g {&settings, "Common"};
  m_myCall=settings.value("MyCall","").toString();
  m_myGrid=settings.value("MyGrid","").toString();
  m_idInt=settings.value("IDint",0).toInt();
  m_astroFont=settings.value("AstroFont",20).toInt();
  m_saveDir=settings.value("SaveDir",m_appDir + "/save").toString();
  m_azelDir=settings.value("AzElDir",m_appDir).toString();
  m_timeout=settings.value("Timeout",20).toInt();
  m_fCal=settings.value("Fcal",0).toInt();
  m_fAdd=settings.value("FAdd",0).toDouble();
  soundInThread.setFadd(m_fAdd);
  m_network = settings.value("NetworkInput",true).toBool();
  m_fs96000 = settings.value("FSam96000",true).toBool();
  m_dB = settings.value("Scale_dB",0).toInt();
  m_udpPort = settings.value("UDPport",50004).toInt();
  soundInThread.setScale(m_dB);
  soundInThread.setPort(m_udpPort);
  ui->actionCuteSDR->setChecked(settings.value(
                                  "PaletteCuteSDR",true).toBool());
  ui->actionLinrad->setChecked(settings.value(
                                 "PaletteLinrad",false).toBool());

  m_modeQ65=settings.value("nModeQ65",2).toInt();
  if(m_modeQ65==1) ui->actionQ65A->setChecked(true);
  if(m_modeQ65==2) ui->actionQ65B->setChecked(true);
  if(m_modeQ65==3) ui->actionQ65C->setChecked(true);
  if(m_modeQ65==4) ui->actionQ65D->setChecked(true);
  if(m_modeQ65==5) ui->actionQ65E->setChecked(true);

  ui->actionNone->setChecked(settings.value("SaveNone",true).toBool());
  ui->actionSave_all->setChecked(settings.value("SaveAll",false).toBool());
  m_saveAll=ui->actionSave_all->isChecked();
  m_onlyEME=settings.value("NEME",false).toBool();
  ui->actionOnly_EME_calls->setChecked(m_onlyEME);
  m_kb8rq=settings.value("KB8RQ",false).toBool();
  m_NB=settings.value("NB",false).toBool();
  ui->NBcheckBox->setChecked(m_NB);
  ui->sbMaxDrift->setValue(settings.value("MaxDrift",0).toInt());
  m_NBslider=settings.value("NBslider",40).toInt();
  ui->NBslider->setValue(m_NBslider);
  m_gainx=settings.value("GainX",1.0).toFloat();
  m_gainy=settings.value("GainY",1.0).toFloat();
  m_phasex=settings.value("PhaseX",0.0).toFloat();
  m_phasey=settings.value("PhaseY",0.0).toFloat();

  if(!ui->actionLinrad->isChecked() && !ui->actionCuteSDR->isChecked() &&
    !ui->actionAFMHot->isChecked() && !ui->actionBlue->isChecked()) {
    on_actionLinrad_triggered();
    ui->actionLinrad->setChecked(true);
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
  static int nfsample=96000;
  static int nsec0=0;
  static int nsum=0;
  static int ndiskdat;
  static int nb;
  static float px=0.0;
  static uchar lstrong[1024];
  static float slimit;
  static double xsum=0.0;

  if(m_diskData) {
    ndiskdat=1;
    datcom_.ndiskdat=1;
  } else {
    ndiskdat=0;
    datcom_.ndiskdat=0;
  }
// Get power, spectrum, nkhz, and ihsym
  nb=0;
  if(m_NB) nb=1;
  nfsample=96000;
  if(!m_fs96000) nfsample=95238;
  symspec_(&k, &ndiskdat, &nb, &m_NBslider, &nfsample,
           &px, s, &nkhz, &ihsym, &nzap, &slimit, lstrong);

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

  lab2->setText (
        QString {" Rx: %1  %2 % "}
        .arg (px, 5, 'f', 1)
        .arg (m_pctZap, 5, 'f', 1)
        );

  xSignalMeter->setValue(px);                   // Update the signal meters
  //Suppress scrolling if WSJT-X is transmitting
  if((m_monitoring and ipc_wsjtx[4] != 1) or m_diskData) {
      m_wide_graph_window->dataSink2(s,nkhz,ihsym,m_diskData,lstrong);
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

  if(ihsym < m_hsymStop) m_decode_called=false;

  if(ihsym >= m_hsymStop and !m_decode_called) {   //Decode at t=56 s (for Q65 and data from disk)
    m_decode_called=true;
    datcom_.newdat=1;
    datcom_.nagain=0;
    datcom_.nhsym=ihsym;
    QDateTime t = QDateTime::currentDateTimeUtc();
    m_dateTime=t.toString("yymmdd_hhmm");
    decode();                                           //Start the decoder
    if(m_saveAll and !m_diskData and m_nTransmitted<10) {
      QString fname=m_saveDir + "/" + t.date().toString("yyMMdd") + "_" +
          t.time().toString("hhmm");
      fname += ".iq";
      *future2 = QtConcurrent::run(savetf2, fname, false);
      watcher2->setFuture(*future2);
    }
    m_nTransmitted=0;
  }

  soundInThread.m_dataSinkBusy=false;
}

void MainWindow::showSoundInError(const QString& errorMsg)
 {QMessageBox::critical(this, tr("Error in SoundIn"), errorMsg);}

void MainWindow::showStatusMessage(const QString& statusMsg)
 {statusBar()->showMessage(statusMsg);}

void MainWindow::on_actionSettings_triggered()
{
  DevSetup dlg(this);
  dlg.m_myCall=m_myCall;
  dlg.m_myGrid=m_myGrid;
  dlg.m_idInt=m_idInt;
  dlg.m_astroFont=m_astroFont;
  dlg.m_saveDir=m_saveDir;
  dlg.m_azelDir=m_azelDir;
  dlg.m_timeout=m_timeout;
  dlg.m_fCal=m_fCal;
  dlg.m_fAdd=m_fAdd;
  dlg.m_network=m_network;
  dlg.m_fs96000=m_fs96000;
  dlg.m_udpPort=m_udpPort;
  dlg.m_dB=m_dB;
  dlg.initDlg();
  if(dlg.exec() == QDialog::Accepted) {
    m_myCall=dlg.m_myCall;
    m_myGrid=dlg.m_myGrid;
    m_idInt=dlg.m_idInt;
    m_astroFont=dlg.m_astroFont;
    if(m_astro_window && m_astro_window->isVisible()) m_astro_window->setFontSize(m_astroFont);
    ui->actionFind_Delta_Phi->setEnabled(false);
    m_saveDir=dlg.m_saveDir;
    m_azelDir=dlg.m_azelDir;
    m_timeout=dlg.m_timeout;
    m_fCal=dlg.m_fCal;
    m_fAdd=dlg.m_fAdd;
    soundInThread.setFadd(m_fAdd);
    m_wide_graph_window->setFcal(m_fCal);
    m_fs96000=dlg.m_fs96000;
    m_network=dlg.m_network;
    m_udpPort=dlg.m_udpPort;
    m_dB=dlg.m_dB;
    soundInThread.setScale(m_dB);

    if(dlg.m_restartSoundIn) {
      soundInThread.quit();
      soundInThread.wait(1000);
      soundInThread.setNetwork(m_network);
      soundInThread.setRate(96000.0);
      soundInThread.setNrx(1);
      soundInThread.start(QThread::HighestPriority);
    }
  }
}

void MainWindow::on_monitorButton_clicked()                  //Monitor
{
  if(m_monitoring or m_loopall) {
    m_monitoring=false;
    soundInThread.setMonitoring(false);
    m_loopall=false;
  } else {
    m_monitoring=true;
    soundInThread.setMonitoring(true);
    m_diskData=false;
  }
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

void MainWindow::keyPressEvent( QKeyEvent *e )                //keyPressEvent
{
  switch(e->key())
  {
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

  lab2 = new QLabel("");
  lab2->setAlignment(Qt::AlignHCenter);
  lab2->setMinimumSize(QSize(80,10));
  lab2->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  statusBar()->addWidget(lab2);

  lab3 = new QLabel("");
  lab3->setAlignment(Qt::AlignHCenter);
  lab3->setMinimumSize(QSize(50,10));
  lab3->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  statusBar()->addWidget(lab3);

  lab4 = new QLabel("");
  lab4->setAlignment(Qt::AlignHCenter);
  lab4->setMinimumSize(QSize(50,10));
  lab4->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  statusBar()->addWidget(lab4);
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
  if (m_astro_window) m_astro_window->close ();
  if (m_wide_graph_window) m_wide_graph_window->close ();
  QMainWindow::closeEvent (e);
}

void MainWindow::msgBox(QString t)                             //msgBox
{
  msgBox0.setText(t);
  msgBox0.exec();
}

void MainWindow::on_actionAstro_Data_triggered()             //Display Astro
{
  if (m_astro_window ) m_astro_window->show();
}

void MainWindow::on_actionWide_Waterfall_triggered()      //Display Waterfalls
{
  m_wide_graph_window->show();
}

void MainWindow::on_actionOpen_triggered()                     //Open File
{
  m_monitoring=false;
  soundInThread.setMonitoring(m_monitoring);
  QString fname;
  fname=QFileDialog::getOpenFileName(this, "Open File", m_path,
                                     "MAP65/QMAP Files (*.iq)");
  if(fname != "") {
    m_path=fname;
    int i;
    i=fname.indexOf(".iq") - 11;
    if(i>=0) {
      lab1->setStyleSheet("QLabel{background-color: #66ff66}");
      lab1->setText(" " + fname.mid(i,15) + " ");
    }
    if(m_monitoring) on_monitorButton_clicked();
    m_diskData=true;
    int dbDgrd=0;
    if(m_myCall=="K1JT" and m_idInt<0) dbDgrd=m_idInt;
    *future1 = QtConcurrent::run(getfile, fname, false, dbDgrd);
    watcher1->setFuture(*future1);
  }
}

void MainWindow::on_actionOpen_next_in_directory_triggered()   //Open Next
{
  int i,len;
  QFileInfo fi(m_path);
  QStringList list;
  list= fi.dir().entryList().filter(".iq");
  for (i = 0; i < list.size()-1; ++i) {
    if(i==list.size()-2) m_loopall=false;
    len=list.at(i).length();
    if(list.at(i)==m_path.right(len)) {
      int n=m_path.length();
      QString fname=m_path.replace(n-len,len,list.at(i+1));
      m_path=fname;
      int i;
      i=fname.indexOf(".iq") - 11;
      if(i>=0) {
        lab1->setStyleSheet("QLabel{background-color: #66ff66}");
        lab1->setText(" " + fname.mid(i,len) + " ");
      }
      m_diskData=true;
      int dbDgrd=0;
      if(m_myCall=="K1JT" and m_idInt<0) dbDgrd=m_idInt;
      *future1 = QtConcurrent::run(getfile, fname, false, dbDgrd);
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
  hsym=2048.0*96000.0/11025.0;         //Samples per JT65 half-symbol
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

void MainWindow::decoderFinished()                      //diskWriteFinished
{
  m_startAnother=m_loopall;
  ui->DecodeButton->setStyleSheet("");
  decodeBusy(false);
  decodes_.nQDecoderDone=1;
  if(m_diskData) decodes_.nQDecoderDone=2;
  mem_qmap.lock();
  decodes_.nWDecoderBusy=ipc_wsjtx[3];                   //Prevent overwriting values
  decodes_.nWTransmitting=ipc_wsjtx[4];                  //written here by WSJT-X
  memcpy((char*)ipc_wsjtx, &decodes_, sizeof(decodes_)); //Send decodes and flags to WSJT-X
  mem_qmap.unlock();
  QString t1;
  t1=t1.asprintf(" %d ",decodes_.ndecodes);
  lab4->setText(t1);
  QDateTime now=QDateTime::currentDateTimeUtc();
}

void MainWindow::on_actionDelete_all_iq_files_in_SaveDir_triggered()
{
  int i;
  QString fname;
  int ret = QMessageBox::warning(this, "Confirm Delete",
      "Are you sure you want to delete all *.iq files in\n" +
       QDir::toNativeSeparators(m_saveDir) + " ?",
       QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
  if(ret==QMessageBox::Yes) {
    QDir dir(m_saveDir);
    QStringList files=dir.entryList(QDir::Files);
    QList<QString>::iterator f;
    for(f=files.begin(); f!=files.end(); ++f) {
      fname=*f;
      i=(fname.indexOf(".iq"));
      if(i==11) dir.remove(fname);
    }
  }
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

void MainWindow::on_DecodeButton_clicked()                    //Decode request
{
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
  m_nDoubleClicked++;
  if(!m_decoderBusy) {
    datcom_.nagain=1;
    datcom_.newdat=0;
    decode();
  }
}

void MainWindow::decode()                                       //decode()
{
//Don't attempt to decode if decoder is already busy, or if we transmitted for 10 s or more.
  if(m_decoderBusy or m_nTransmitted>10) return;
  QString fname="           ";
  ui->DecodeButton->setStyleSheet(m_pbdecoding_style1);

  if(datcom_.nagain==0 && (!m_diskData)) {
    qint64 ms = QDateTime::currentMSecsSinceEpoch() % 86400000;
    int imin=ms/60000;
    int ihr=imin/60;
    imin=imin % 60;
    datcom_.nutc=100*ihr + imin;
  }

  datcom_.mousedf=m_wide_graph_window->DF() + m_fCal;
  datcom_.mousefqso=m_wide_graph_window->QSOfreq();
  datcom_.fselected=datcom_.mousefqso + 0.001*datcom_.mousedf;
  datcom_.ndiskdat=0;
  if(m_diskData) {
    datcom_.ndiskdat=1;
    int i0=m_path.indexOf(".iq");
    if(i0>0) {
      // Compute self Doppler using the filename for Date and Time
      int nyear=m_path.mid(i0-11,2).toInt()+2000;
      int month=m_path.mid(i0-9,2).toInt();
      int nday=m_path.mid(i0-7,2).toInt();
      int nhr=m_path.mid(i0-4,2).toInt();
      int nmin=m_path.mid(i0-2,2).toInt();
      double uth=nhr + nmin/60.0;
      int nfreq=(int)datcom_.fcenter;
      int ndop00;
      astrosub00_(&nyear, &month, &nday, &uth, &nfreq, m_myGrid.toLatin1(),&ndop00,6);
      datcom_.ndop00=ndop00;               //Send self Doppler to decoder, via datcom
      fname=m_path.mid(i0-11,11);
    }
  }
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
  m_nutc0=datcom_.nutc;
  datcom_.junk_1=0;
  datcom_.nfsample=96000;
  if(!m_fs96000) datcom_.nfsample=95238;
  datcom_.nxpol=0;
  datcom_.nmode=10*m_modeQ65;
  datcom_.nsave=m_nsave;
  datcom_.max_drift=ui->sbMaxDrift->value();
  datcom_.ndepth=1;
  if(datcom_.nagain==1)   datcom_.ndepth=3;

  QString mcall=(m_myCall+"            ").mid(0,12);
  QString mgrid=(m_myGrid+"            ").mid(0,6);

  memcpy(datcom_.mycall, mcall.toLatin1(), 12);
  memcpy(datcom_.mygrid, mgrid.toLatin1(), 6);
  if(m_diskData) {
    memcpy(datcom_.datetime, fname.toLatin1(), 11);
  } else {
    memcpy(datcom_.datetime, m_dateTime.toLatin1(), 11);
  }
  datcom_.junk1=1234;                                     //Cecck for these values in m65
  datcom_.junk2=5678;

  char *to = (char*) datcom2_.d4;
  char *from = (char*) datcom_.d4;
  memcpy(to, from, sizeof(datcom_));
  datcom_.nagain=0;
  datcom_.ndiskdat=0;
  m_call3Modified=false;

  decodes_.ndecodes=0;
  decodes_.ncand=0;
  decodes_.nQDecoderDone=0;
  m_fetched=0;
  int itimer=0;
  m_decoder_start_time=QDateTime::currentDateTimeUtc();
  watcher3.setFuture(QtConcurrent::run (std::bind (q65c_, &itimer)));

  decodeBusy(true);
}

void MainWindow::on_EraseButton_clicked()
{
  ui->decodedTextBrowser->clear();
  lab4->clear();
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
  int khsym=0;

  qint64 ms = QDateTime::currentMSecsSinceEpoch() % 86400000;
  int nsec=ms/1000;

  if(m_monitoring) {
    ui->monitorButton->setStyleSheet(m_pbmonitor_style);
  } else {
    ui->monitorButton->setStyleSheet("");
  }

  m_wide_graph_window->updateFreqLabel();

  if(m_startAnother) {
    m_startAnother=false;
    on_actionOpen_next_in_directory_triggered();
  }

  QString t1;
  if(decodes_.ndecodes > m_fetched) {
    while(m_fetched<decodes_.ndecodes) {
      QString t=QString::fromLatin1(decodes_.result[m_fetched]);
      if(m_UTC0!="" and m_UTC0!=t.left(4)) {
        t1="-";
        ui->decodedTextBrowser->append(t1.repeated(56));
      }
      m_UTC0=t.left(4);
      ui->decodedTextBrowser->append(t.trimmed());
      m_fetched++;
    }
  }
  t1="";
  t1=t1.asprintf("%.3f",datcom_.fcenter);
  ui->labFreq->setText(t1);

  if(nsec != m_sec0) {                                     //Once per second
//    qDebug() << "AAA" << nsec << m_fAdd;
    static int n60z=99;
    int n60=nsec%60;
    int itest[5];
    mem_qmap.lock();
    memcpy(&itest, (char*)ipc_wsjtx, 20);
    mem_qmap.unlock();
    if(itest[4]==1) m_nTransmitted++;
//    qDebug() << "AAA" << n60 << itest[0] << itest[1] << itest[2] << itest[3] << itest[4]
//             << m_nTransmitted;
    if(n60<n60z) m_nTransmitted=0;
    n60z=n60;

    if(m_pctZap>30.0) {
      lab2->setStyleSheet("QLabel{background-color: #ff0000}");
    } else {
      lab2->setStyleSheet("");
    }


    if(m_monitoring) {
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
    m_hsym0=khsym;
    m_sec0=nsec;
  }
}

void MainWindow::on_actionQ65A_triggered()
{
  m_modeQ65=1;
  lab3->setStyleSheet("QLabel{background-color: #ffb266}");
  lab3->setText("Q65-60A");
}

void MainWindow::on_actionQ65B_triggered()
{
  m_modeQ65=2;
  lab3->setStyleSheet("QLabel{background-color: #b2ff66}");
  lab3->setText("Q65-60B");
}

void MainWindow::on_actionQ65C_triggered()
{
  m_modeQ65=3;
  lab3->setStyleSheet("QLabel{background-color: #66ffff}");
  lab3->setText("Q65-60C");
}

void MainWindow::on_actionQ65D_triggered()
{
  m_modeQ65=4;
  lab3->setStyleSheet("QLabel{background-color: #b266ff}");
  lab3->setText("Q65-60D");
}

void MainWindow::on_actionQ65E_triggered()
{
  m_modeQ65=5;
  lab3->setStyleSheet("QLabel{background-color: #ff66ff}");
  lab3->setText("Q65-60E");
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

bool MainWindow::isGrid4(QString g)
{
  if(g.length()!=4) return false;
  if(g.mid(0,1)<'A' or g.mid(0,1)>'R') return false;
  if(g.mid(1,1)<'A' or g.mid(1,1)>'R') return false;
  if(g.mid(2,1)<'0' or g.mid(2,1)>'9') return false;
  if(g.mid(3,1)<'0' or g.mid(3,1)>'9') return false;
  return true;
}

void MainWindow::on_actionQuick_Start_Guide_to_Q65_triggered()
{
  QDesktopServices::openUrl (QUrl {"https://wsjt.sourceforge.io/Q65_Quick_Start.pdf"});
}

void MainWindow::on_actionQuick_Start_Guide_to_WSJT_X_2_7_and_QMAP_triggered()
{
  QDesktopServices::openUrl (QUrl {"https://wsjt.sourceforge.io/Quick_Start_WSJT-X_2.7_QMAP.pdf"});
}

