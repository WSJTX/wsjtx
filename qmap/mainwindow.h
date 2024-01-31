#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include <QtGui>
#include <QtWidgets>
#include <QPointer>
#include <QScopedPointer>
#include <QLabel>
#include <QDateTime>
#include <QHash>
#include "getfile.h"
#include "soundin.h"
#include "signalmeter.h"
#include "commons.h"
#include "sleep.h"
#include <QtConcurrent/QtConcurrent>

#define NFFT 32768
#define NSMAX 5760000

//--------------------------------------------------------------- MainWindow
namespace Ui {
  class MainWindow;
}

class QTimer;
class Astro;
class WideGraph;

class MainWindow : public QMainWindow
{
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = 0);
  ~MainWindow();
  bool m_network;

public slots:
  void showSoundInError(const QString& errorMsg);
  void showStatusMessage(const QString& statusMsg);
  void dataSink(int k);
  void diskDat(int iret);
  void decoderFinished();
  void freezeDecode(int n);
  void guiUpdate();

private:
  virtual void keyPressEvent (QKeyEvent *) override;
  virtual bool eventFilter (QObject *, QEvent *) override;
  virtual void closeEvent (QCloseEvent *) override;

private slots:
  void on_monitorButton_clicked();
  void on_actionExit_triggered();
  void on_actionAbout_triggered();
  void on_actionLinrad_triggered();
  void on_actionCuteSDR_triggered();
  void on_tolSpinBox_valueChanged(int arg1);
  void on_actionAstro_Data_triggered();
  void on_actionWide_Waterfall_triggered();
  void on_actionOpen_triggered();
  void on_actionOpen_next_in_directory_triggered();
  void on_actionDecode_remaining_files_in_directory_triggered();
  void on_actionDelete_all_iq_files_in_SaveDir_triggered();
  void on_actionNone_triggered();
  void on_actionSave_all_triggered();
  void on_DecodeButton_clicked();
  void decode();
  void decodeBusy(bool b);
  void on_EraseButton_clicked();
  void bumpDF(int n);
  void on_actionSettings_triggered();
  void on_NBcheckBox_toggled(bool checked);
  void on_NBslider_valueChanged(int value);
  void on_actionAFMHot_triggered();
  void on_actionBlue_triggered();
  void on_actionQ65A_triggered();
  void on_actionQ65B_triggered();
  void on_actionQ65C_triggered();
  void on_actionQ65D_triggered();
  void on_actionQ65E_triggered();
  void on_actionQuick_Start_Guide_to_Q65_triggered();
  void on_actionQuick_Start_Guide_to_WSJT_X_2_7_and_QMAP_triggered();
  void on_actionAlso_Q65_30x_toggled(bool b);
  void on_sbMaxDrift_valueChanged(int arg1);
  void on_actionSave_decoded_triggered();
  void on_actionExport_wav_file_at_fQSO_triggered();

  void on_actionExport_wav_file_at_fQSO_30a_triggered();

  void on_actionExport_wav_file_at_fQSO_30b_triggered();

private:
  Ui::MainWindow *ui;
  QString m_appDir;
  QString m_settings_filename;
  QScopedPointer<Astro> m_astro_window;
  QScopedPointer<WideGraph> m_wide_graph_window;
  QPointer<QTimer> m_gui_timer;
  qint32  m_waterfallAvg;
  qint32  m_DF;
  qint32  m_tol;
  qint32  m_astroFont;
  qint32  m_fCal;
  qint32  m_sec0;
  qint32  m_nutc0;
  qint32  m_nrx;
  qint32  m_hsym0;
  qint32  m_paInDevice;
  qint32  m_udpPort;
  qint32  m_NBslider;
  qint32  m_TRperiod;
  qint32  m_modeQ65;
  qint32  m_dB;
  qint32  m_fetched=0;
  qint32  m_hsymStop=390;             //390*0.15 = 58.5 s
  qint32  m_nTx30a=0;
  qint32  m_nTx30b=0;
  qint32  m_nTx60=0;
  qint32  m_nDoubleClicked=0;
  qint32  m_nline=0;
  qint32  m_WSJTX_TRperiod=0;
  qint32  m_dop00=0;
  qint32  m_dop58=0;
  qint32  m_n60;

  double  m_fAdd;
  double  m_xavg;

  bool    m_monitoring;
  bool    m_diskData;
  bool    m_loopall;
  bool    m_decoderBusy=false;
  bool    m_restart;
  bool    m_startAnother;
  bool    m_saveAll;
  bool    m_saveDecoded;
  bool    m_NB;
  bool    m_decode_called=false;
  bool    m_bAlso30=true;
  bool    m_bDiskDatBusy=false;
  bool    m_bWTransmitting=false;
  bool    m_bDecodeAgain=false;

  float   m_pctZap;

  int     m_myCallColor;

  QRect   m_wideGraphGeom;

  QLabel* lab1;                            // labels in status bar
  QLabel* lab2;                            // labels in status bar
  QLabel* lab3;                            // labels in status bar
  QLabel* lab4;
  QLabel* lab5;
  QLabel* lab6;
  QLabel* lab7;                   //Why still needed?

  QMessageBox msgBox0;

  QFutureWatcher<void> watcher3;     //For decoder

  QString m_path;
  QString m_pbdecoding_style1;
  QString m_pbmonitor_style;
  QString m_pbAutoOn_style;
  QString m_myCall;
  QString m_myGrid;
  QString m_hisCall;
  QString m_hisGrid;
  QString m_saveDir;
  QString m_azelDir;
  QString m_palette;
  QString m_dateTime;
  QString m_mode;
  QString m_UTC0="";
  QString m_revision;
  QString m_saveFileName;

  QDateTime m_dateTimeSeqStart;        //Nominal start time of Rx sequence about to be decoded
  QHash<QString,bool> m_worked;
  SignalMeter *xSignalMeter;
  SoundInThread soundInThread;             //Instantiate the audio threads

  //---------------------------------------------------- private functions
  void readSettings();
  void writeSettings();
  void createStatusBar();
  void updateStatusBar();
  void msgBox(QString t);
  bool isGrid4(QString g);
};

extern void getfile(QString fname, bool xpol, int idInt);
extern void save_iq(QString fname);
extern int killbyname(const char* progName);

extern "C" {
//----------------------------------------------------- C and Fortran routines
  void symspec_(int* k, int* ndiskdat, int* nb, int* m_NBslider, int* nfsample,
                float* px, float s[], int* nkhz, int* nhsym,
                int* nzap, float* slimit, uchar lstrong[]);

  void astrosub00_ (int* nyear, int* month, int* nday, double* uth, int* nfreq,
                    const char* mygrid, int* ndop00, int len1);

  void q65c_();

  void all_done_();

  void zaptx_(float d4[], int* k0, int* k);

  void save_qm_(const char* fname, const char* prog_id, const char* mycall, const char* mygrid,
                float d4[], int* ntx30a, int* ntx30b, double* fcenter, int* nutc,
                int* dop00, int* dop58, int len1, int len2, int len3, int len4);

  void read_qm_(const char* fname, int* iret, int len);

  }

#endif // MAINWINDOW_H
