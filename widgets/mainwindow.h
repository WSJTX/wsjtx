// -*- Mode: C++ -*-
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QLabel>
#include <QThread>
#include <QProcess>
#include <QProgressBar>
#include <QTimer>
#include <QDateTime>
#include <QElapsedTimer>
#include <QMap>
#include <QRegExp>
#include <QRegularExpression>
#include <QList>
#include <QAudioDeviceInfo>
#include <QStringList>
#include <QScopedPointer>
#include <QDir>
#include <QAbstractSocket>
#include <QHostAddress>
#include <QPointer>
#include <QSet>
#include <QHash>
#include <QVector>
#include <QScrollBar>
#include <QTextBlock>
#include <QQueue>
#include <QFuture>
#include <QFutureSynchronizer>
#include <QFutureWatcher>
#include <QDateTime>
#include <array>
#include <memory>

#include "MultiGeometryWidget.hpp"
#include "NonInheritingProcess.hpp"
#include "Audio/AudioDevice.hpp"
#include "Audio/TxIdentity.hpp"
#include "Audio/TxPlaybackDiagnostics.hpp"
#include "Audio/TxPlaybackEvidence.hpp"
#include "Audio/TxRequest.hpp"
#include "Audio/WavLoadCoordinator.hpp"
#include "commons.h"
#include "Radio.hpp"
#include "models/Modes.hpp"
#include "models/FrequencyList.hpp"
#include "Configuration.hpp"
#include "WSPR/WSPRBandHopping.hpp"
#include "Transceiver/Transceiver.hpp"
#include "DisplayManual.hpp"
#include "Network/PSKReporter.hpp"
#include "Network/Cloudlog.hpp"
#include "logbook/logbook.h"
#include "astro.h"
#include "qtextbrowser.h"
#include "widgets/QSYMessageCreator.h"
#include "widgets/QSYMessage.h"
#include "widgets/displaytext.h"
#include "widgets/qsymonitor.h"
#include "MessageBox.hpp"
#include "Network/NetworkAccessManager.hpp"
#include "AutoRespondSelectionLatch.hpp"
#include "AutoRespondScoring.hpp"
#include "HoundTransmissionPolicy.hpp"
#include "QsoProgress.hpp"
#include "DecodeOperatingContext.hpp"
#include "DecoderOutputFramer.hpp"
#include "Ft8MtdDecodeCoordinator.hpp"

#define NUM_JT4_SYMBOLS 206                //(72+31)*2, embedded sync
#define NUM_JT65_SYMBOLS 126               //63 data + 63 sync
#define NUM_JT9_SYMBOLS 85                 //69 data + 16 sync
#define NUM_WSPR_SYMBOLS 162               //(50+31)*2, embedded sync
#define NUM_MSK144_SYMBOLS 144             //s8 + d48 + s8 + d80
#define NUM_Q65_SYMBOLS 85                 //63 data + 22 sync
#define NUM_FT8_SYMBOLS 79
#define NUM_SUPERFOX_SYMBOLS 153
#define NUM_FT4_SYMBOLS 105
#define NUM_FST4_SYMBOLS 160             //240/2 data + 5*8 sync
#define NUM_CW_SYMBOLS 250
#define MAX_NUM_SYMBOLS 250
#define TX_SAMPLE_RATE 48000
#define NRING 3456000
#define MAX_HOUNDS_IN_QUEUE 10

// extern int volatile itone[MAX_NUM_SYMBOLS];   //Audio tones for all Tx symbols
// extern int volatile icw[NUM_CW_SYMBOLS];	    //Dits for CW ID

//--------------------------------------------------------------- MainWindow
namespace Ui {
  class MainWindow;
}

class QWidget;
class QRadioButton;
class QFocusFrame;
class QFrame;
class QButtonGroup;

class QProcessEnvironment;
class QSharedMemory;
class QSplashScreen;
class QSettings;
class QLineEdit;
class QFont;
class QHostInfo;
class EchoGraph;
class FastGraph;
class WideGraph;
class LogQSO;
class Transceiver;
class MessageAveraging;
class ActiveStations;
class FoxLogWindow;
class CabrilloLogWindow;
class ColorHighlighting;
class MessageClient;
class QTime;
class WSPRBandHopping;
class HelpTextWindow;
class EQSL;
class WSPRNet;
class SoundOutput;
class Modulator;
class AudioInputSource;
class Detector;
class SampleDownloader;
class MultiSettings;

namespace DecodedMessageReaction
{
  enum class ContestHint;
  enum class ReactionDisposition;
  enum class WaitDecodeSource;
  struct QsoReactionEffect;
  struct QsoReactionPlan;
  struct QsoReactionSnapshot;
}
class EqualizationToolsDialog;
class DecodedText;
class Cloudlog;

#include "Audio/TxAudioQueue.hpp"
#include "Modulator/JttyTxStream.hpp"

#ifdef WIN32
class MMTTYIF;
#endif

class MainWindow
  : public MultiGeometryWidget<3, QMainWindow>
{
  Q_OBJECT;

public:
  using Frequency = Radio::Frequency;
  using FrequencyDelta = Radio::FrequencyDelta;
  using Mode = Modes::Mode;
  using SpecOp = Configuration::SpecialOperatingActivity;

  enum class JttyTxRejectReason
  {
    Empty,
    EncodingFailed,
    QueueFull,
    BackendRejected,
    Aborted,
    NotAvailable
  };
  Q_ENUM(JttyTxRejectReason)

  static QRegExp const message_alphabet;
  static QRegularExpression const grid_regexp;
  static QRegularExpression const non_r_db_regexp;

  explicit MainWindow(QDir const& temp_directory, bool multiple, MultiSettings *,
                      QSharedMemory *shdmem, unsigned downSampleFactor,
                      QSplashScreen *, QProcessEnvironment const&, bool automated_test,
                      std::unique_ptr<AudioInputSource> audio_input_source = {},
                      std::unique_ptr<SoundOutput> sound_output = {},
                      QString decoder_data_path = {},
                      QWidget *parent = nullptr);
  ~MainWindow();

#ifdef WIN32
  void initMMTTY(quint16 port);
  MMTTYIF *getMmttyIf() const;
#endif

  bool decoderBusy () const
    {return DecodeOwner::None != m_decodeOwner || m_ft8MtdDecodeCoordinator.hasPending ();}
  void set_mode_from_command_line(const QString& mode, bool lock_mode = false);
  bool decoderBackendRunning () const;
  bool diskDataActive () const {return m_diskData;}
  bool monitoringActive () const {return m_monitoring;}
#if defined (WSJT_ENABLE_LIVE_AUDIO_TEST)
  struct LiveAudioTestFt8TransmitRequest
  {
    qint64 session_id {-1};
    qint64 generation {-1};
  };

  bool liveAudioTestMultithreadedFt8Enabled () const {return m_multithreadFT8;}
  int liveAudioTestFt8ThreadCount () const {return m_ft8threads;}
  int liveAudioTestDecodeDepth () const {return m_ndepth & 7;}
  int liveAudioTestFt8Cycles () const {return m_nFT8Cycles;}
  int liveAudioTestFt8Sensitivity () const {return m_ft8Sensitivity;}
  int liveAudioTestFt8DecoderStart () const {return m_ft8DecoderStart;}
  QString liveAudioTestFt8BackpressureDiagnostics () const;
  static constexpr int liveAudioTestDecodeLowFrequency () {return 200;}
  static constexpr int liveAudioTestDecodeHighFrequency () {return 3000;}
  bool configureLiveAudioTestDecodeRange ();
  LiveAudioTestFt8TransmitRequest startLiveAudioTestFt8Transmit (
    qint64 targetPeriodStartMs);
#endif

  Q_SIGNALS:
  void decoderBackendStarted () const;
  void decoderBackendFailed (QString reason) const;
  void decodeCycleStarted (quint64 generation) const;
  void decodeCycleCompleted (quint64 generation) const;
#if defined (WSJT_ENABLE_LIVE_AUDIO_TEST)
  void ft8DecoderInvocation (bool multithreaded, int threadCount, int depth,
                             int cycles, bool subpass, int decoderStart,
                             int halfSymbols, int sampleCount,
                             int lowFrequency, int highFrequency) const;
  void decoderOutputLine (QByteArray line) const;
  void liveAudioTestFt8TransmitStartDecided (qint64 sessionId,
                                             qint64 generation,
                                             qint64 targetPeriodStartMs,
                                             bool accepted,
                                             qint64 actualStartMs) const;
#endif
  void decodedMessageProcessed (QString message) const;
  void decodedMessageDisplayed (QString message) const;
  void jttyTextAccepted(qint64 requestId) const;
  void jttyTextRejected(qint64 requestId, JttyTxRejectReason reason) const;
  void jttyTextCompleted(qint64 requestId) const;
  void jttySessionDrained(qint64 sessionId) const;

  public slots:
  void showSoundInError(const QString& errorMsg);
  void showSoundOutError(const QString& errorMsg);
  void showStatusMessage(const QString& statusMsg);
  // JTTY text submission is asynchronous. The returned request id is completed
  // by accepted/rejected signals; accepted means queued for backend transmit,
  // not finished on RF. Graceful external OFF commands should stop submitting
  // new text and let the JTTY drain path stop TX; use abort_jtty_tx() for hard
  // abort.
  qint64 submitJttyText(QString message);
  void dataSink(qint64 frames);
  void fastSink(qint64 frames);
  void tci_mod_active(bool on) {m_tci_mod_active = on;}
  void diskDat();
  void freezeDecode(int n);
  void guiUpdate();
  void doubleClickOnCall (QString const& line, QString const& word, Qt::KeyboardModifiers);
  void doubleClickOnCall2(QString const& line, QString const& word, Qt::KeyboardModifiers);
  void doubleClickOnFoxQueue(QString const& line, QString const& word, Qt::KeyboardModifiers);
  void doubleClickOnFoxInProgress(QString const& line, QString const& word, Qt::KeyboardModifiers modifiers);
  void readFromStdout();
  void p1ReadFromStdout();
  void setXIT(int n, Frequency base = 0u);
  void setFreq4(int rxFreq, int txFreq);
  void msgAvgDecode2();
  void fastPick(int x0, int x1, int y);
  void skedFreq(double sf);

private:
  enum class DecodeOwner
  {
    None,
    Jt9,
    Wsprd
  };

  enum class Jt9ProcessPhase
  {
    InitialStarting,
    Ready,
    StopRequested,
    Terminating,
    Killing,
    ReplacementStarting,
    Closing
  };

  enum class DecodePublishResult
  {
    Published,
    Unavailable,
    Failed
  };

  struct ActiveJt9Decode
  {
    qint32 generation {0};
    DecodeOperatingContext context;
    bool copiedSamples {false};
    bool obsolete {false};
    Ft8MtdDecodeCoordinator::Stage ft8Stage {Ft8MtdDecodeCoordinator::Stage::None};
    qint64 ft8Period {-1};
  };

  static constexpr int MaxActiveStationRows = 50;
  // Keep this matched with MAX_CALLERS in the Q65 q3list Fortran helpers.
  static constexpr int MaxQ65PileupCallers = 50;

  void change_layout (std::size_t) override;
  void keyPressEvent (QKeyEvent *) override;
  void closeEvent(QCloseEvent *) override;
  void childEvent(QChildEvent *) override;
  bool eventFilter(QObject *, QEvent *) override;
  void showQSYMessage(QString message);
  void save_wave_file(QString const& name, int samples, Frequency frequency,
                      QString const& dgrd);

private slots:
  void initialize_fonts ();
  void ScrollBarPosition(int n);
  void on_actionUse_Dark_Style_triggered (bool checked);
  void on_actionBand_Buttons_triggered ();
  void on_actionVHF_UHF_Buttons_triggered ();
  void on_pb160_clicked();
  void on_pb80_clicked();
  void on_pb60_clicked();
  void on_pb40_clicked();
  void on_pb30_clicked();
  void on_pb20_clicked();
  void on_pb17_clicked();
  void on_pb15_clicked();
  void on_pb12_clicked();
  void on_pb10_clicked();
  void on_pb8_clicked();
  void on_pb6_clicked();
  void on_pb2_clicked();
  void on_pb70_clicked();
  void on_pb50_clicked();
  void on_pb4_clicked();
  void on_pb144_clicked();
  void on_pb220_clicked();
  void on_pb432_clicked();
  void on_pb902_clicked();
  void on_pb23_clicked();
  void on_pb13_clicked();
  void on_pb9_clicked();
  void on_pb5G_clicked();
  void on_pb10G_clicked();
  void on_pb24G_clicked();
  void check_button_color();
  void reset_transmit_controls_after_stop ();
  void stopWRTimeout();
  void stopWCTimeout();
  void bandHoppingTimer();
  void bandHopping();
  void on_houndButton_clicked(bool checked);
  void on_cbHoldTxFreq_clicked (bool);
  void on_ft8Button_clicked();
  void on_ft4Button_clicked();
  void on_msk144Button_clicked();
  void on_q65Button_clicked();
  void on_jt65Button_clicked();
  void on_echoButton_clicked();
  void on_pb15A_clicked();
  void on_pb15C_clicked();
  void on_pb30B_clicked();
  void on_pb60C_clicked();
  void on_pb60D_clicked();
  void on_pb60E_clicked();
  void on_tx1_editingFinished();
  void on_tx2_editingFinished();
  void on_tx3_editingFinished();
  void on_tx4_editingFinished();
  void on_tx5_currentTextChanged (QString const&);
  void on_tx6_editingFinished();
  void on_actionSettings_triggered();
  void on_monitorButton_clicked (bool);
  void on_actionAbout_triggered();
  void on_autoButton_clicked (bool);
  void on_stopTxButton_clicked();
  void on_stopButton_clicked();
  void on_pbBandHopping_clicked();
  void on_actionRelease_Notes_triggered ();
  void on_actionFT8_DXpedition_Mode_User_Guide_triggered();
  void on_actionSuperFox_User_Guide_triggered();
  void on_actionQSG_FST4_triggered();
  void on_actionQSG_Q65_triggered();
  void on_actionQSG_X250_M3_triggered();
  void on_actionQuick_Start_Guide_to_WSJT_X_2_7_and_QMAP_triggered();
  void on_actionOnline_User_Guide_triggered();
  void on_actionLocal_User_Guide_triggered();
  void on_actionWide_Waterfall_triggered();
  void on_actionOpen_triggered();
  void on_actionOpen_next_in_directory_triggered();
  void on_actionDecode_remaining_files_in_directory_triggered();
  void on_actionDelete_all_wav_files_in_SaveDir_triggered();
  void on_actionOpen_log_directory_triggered ();
  void on_actionNone_triggered();
  void on_actionSave_all_triggered();
  void on_actionDefault_event_logging_triggered();
  void on_actionDiagnostic_mode_triggered();
  void on_actionDisable_event_logging_triggered();
  void on_actionDownload_EME_Ephemeris_Chart_triggered();
  void on_actionKeyboard_shortcuts_triggered();
  void on_actionSpecial_mouse_commands_triggered();
  void on_actionSolve_FreqCal_triggered();
  void on_actionCopyright_Notice_triggered();
  void on_actionSWL_Mode_triggered (bool checked);
  void on_DecodeButton_clicked (bool);
  void decode();
  void on_EraseButton_clicked();
  void band_activity_cleared ();
  void rx_frequency_activity_cleared ();
  void on_txFirstCheckBox_stateChanged(int arg1);
  void set_dateTimeQSO(int m_ntx);
  void set_ntx(int n);
  void on_txrb1_toggled(bool status);
  void on_txrb1_doubleClicked ();
  void on_txrb2_toggled(bool status);
  void on_txrb3_toggled(bool status);
  void on_txrb4_toggled(bool status);
  void on_txrb4_doubleClicked ();
  void on_txrb5_toggled(bool status);
  void on_txrb5_doubleClicked ();
  void on_txrb6_toggled(bool status);
  void on_txb1_clicked();
  void on_txb1_doubleClicked ();
  void on_txb2_clicked();
  void on_txb3_clicked();
  void on_txb4_clicked();
  void on_txb4_doubleClicked ();
  void on_txb5_clicked();
  void on_txb5_doubleClicked ();
  void on_txb6_clicked();
  void on_lookupButton_clicked();
  void on_addButton_clicked();
  void on_ignoreButton_clicked();
  void on_comboBoxCQ_activated ();
  void on_DX_Call_Button_clicked (bool checked);
  void wheelEvent(QWheelEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void on_dxCallEntry_textChanged (QString const&);
  void on_dxGridEntry_textChanged (QString const&);
  void on_dxCallEntry_editingFinished();
  void on_dxCallEntry_returnPressed ();
  void on_genStdMsgsPushButton_clicked();
  void on_logQSOButton_clicked();
  void read_txLog();
  void on_actionErase_Tx_Log_triggered();
  void read_ignoreList();
  void addCallsignToignoreList();
  void on_actionErase_Ignore_List_triggered();
  void read_ALLCALL7();
  void remove_old_files(const QString &directoryPath, int daysOld);
  void on_actionJT9_triggered();
  void on_actionJT65_triggered();
  void on_actionJT4_triggered();
  void on_actionFT4_triggered();
  void on_actionFT8_triggered();
  void on_actionFST4_triggered();
  void on_actionFST4W_triggered();
  void on_TxFreqSpinBox_valueChanged(int arg1);
  void on_TxFreqSpinBox_2_valueChanged(int arg1);
  void on_actionSave_decoded_triggered();
  void on_actionQuickDecode_toggled (bool);
  void on_actionMediumDecode_toggled (bool);
  void on_actionDeepestDecode_toggled (bool);

  //ft8md
  void on_actionDecFT8cycles1_triggered();
  void on_actionDecFT8cycles2_triggered();
  void on_actionDecFT8cycles3_triggered();
  void on_actionRXfLow_triggered();
  void on_actionRXfMedium_triggered();
  void on_actionRXfHigh_triggered();
  void on_actionMTAuto_triggered();
  void on_actionMT1_triggered();
  void on_actionMT2_triggered();
  void on_actionMT3_triggered();
  void on_actionMT4_triggered();
  void on_actionMT5_triggered();
  void on_actionMT6_triggered();
  void on_actionMT7_triggered();
  void on_actionMT8_triggered();
  void on_actionMT9_triggered();
  void on_actionMT10_triggered();
  void on_actionMT11_triggered();
  void on_actionMT12_triggered();
  void on_actionFT8SensMin_toggled(bool checked);
  void on_actionlowFT8thresholds_toggled(bool checked);
  void on_actionFT8subpass_toggled(bool checked);
  void on_actionStartTwoStage_toggled(bool checked);
  void on_actionStartThreeStage_toggled(bool checked);
  void on_actionStartEarly_toggled(bool checked);
  void on_actionStartNormal_toggled(bool checked);
  void on_actionStartLate_toggled(bool checked);
  void on_actionFT8WidebandDXCallSearch_toggled(bool checked);
  void on_actionUse_multithreaded_FT8_decoder_triggered(bool checked);

  void bumpFqso(int n);
  void on_actionErase_ALL_TXT_triggered();
  void on_reset_cabrillo_log_action_triggered ();
  void on_actionErase_wsjtx_log_adi_triggered();
  void on_actionErase_WSPR_hashtable_triggered();
  void on_actionErase_list_of_Q65_callers_triggered();
  void on_actionExport_Cabrillo_log_triggered();
  bool startTx2();
  void startP1();
  void stopTx();
  void stopTx2();
  void on_rptSpinBox_valueChanged(int n);
  void killWaveFile();
  void on_tuneButton_clicked (bool);
  void on_pbR2T_clicked();
  void on_pbR2T_2_clicked();
  void on_pbT2R_clicked();
  void on_pbT2R_2_clicked();
  void acceptQSO (QDateTime const&, QString const& call, QString const& grid
                  , Frequency dial_freq, QString const& mode
                  , QString const& rpt_sent, QString const& rpt_received
                  , QString const& tx_power, QString const& comments
                  , QString const& name, QDateTime const& QSO_date_on, QString const& operator_call
                  , QString const& my_call, QString const& my_grid
                  , QString const& exchange_sent, QString const& exchange_rcvd
                  , QString const& propmode, QString const& satellite
                  , QString const& sat_mode
                  , QString const& freqRx, QByteArray const& ADIF);
  void on_bandComboBox_currentIndexChanged (int index);
  void on_bandComboBox_editTextChanged (QString const& text);
  void on_bandComboBox_activated (int index);
  void on_readFreq_clicked();
  void on_RxFreqSpinBox_valueChanged(int n);
  void on_RxFreqSpinBox_2_valueChanged(int n);
  void on_outAttenuation_valueChanged (int);
  void rigOpen ();
  void handle_transceiver_update (Transceiver::TransceiverState const&);
  void handle_transceiver_closing (bool failed);
  void handle_transceiver_failure (QString const& reason);
  void handle_leavingSettings();
  void on_actionAstronomical_data_toggled (bool);
  void on_actionQSYMessage_Creator_triggered();
  void on_actionQSY_Monitor_triggered();
  void alertQSYmessage();
  void on_actionShort_list_of_add_on_prefixes_and_suffixes_triggered();
  void band_changed (Frequency);
  void monitor (bool);
  void end_tuning ();
  void stop_tuning ();
  void stopTuneATU();
  void auto_tx_mode(bool);
  void on_actionMessage_averaging_triggered();
  void on_actionActiveStations_triggered();
  void on_contest_log_action_triggered ();
  void on_fox_log_action_triggered ();
  void on_actionColors_triggered();
  void on_actionInclude_averaging_toggled (bool);
  void on_actionInclude_correlation_toggled (bool);
  void on_actionEnable_AP_DXcall_toggled (bool);
  void on_actionAuto_Clear_Avg_toggled (bool);
  void VHF_features_enabled(bool b);
  void on_sbSubmode_valueChanged(int n);
  void on_cbSendMsg_toggled(bool b);
  void on_cbShMsgs_toggled(bool b);
  void on_cbSWL_toggled(bool b);
  void on_cbTx6_toggled(bool b);
  void on_cbMenus_toggled(bool b);
  void on_cbAutoSeq_toggled(bool b);
  void networkError (QString const&);
  void on_ClrAvgButton_clicked();
  void on_actionWSPR_triggered();
  void on_syncSpinBox_valueChanged(int n);
  void on_TxPowerComboBox_currentIndexChanged(int);
  void on_sbTxPercent_valueChanged(int n);
  void on_cbUploadWSPR_Spots_toggled(bool b);
  void WSPR_config(bool b);
  void uploadWSPRSpots (bool direct_post = false, QString const& decode_text = QString {});
  void TxAgain();
  void uploadResponse(QString const& response);
  void on_WSPRfreqSpinBox_valueChanged(int n);
  void on_sbFST4W_RxFreq_valueChanged(int n);
  void on_sbFST4W_FTol_valueChanged(int n);
  void on_pbTxNext_clicked(bool b);
  void on_actionEcho_Graph_triggered();
  void on_actionEcho_triggered();
  void on_actionFast_Graph_triggered();
  void fast_decode_done();
  void on_actionMeasure_reference_spectrum_triggered();
  void on_actionErase_reference_spectrum_triggered();
  void on_actionMeasure_phase_response_triggered();
  void on_sbTR_valueChanged (int);
  void on_sbTR_FST4W_valueChanged (int);
  void on_sbFtol_valueChanged (int);
  void on_sbFtol_2_valueChanged (int);
  void on_cbFast9_clicked(bool b);
  void on_sbCQTxFreq_valueChanged(int n);
  void on_cbCQTx_toggled(bool b);
  void on_actionMSK144_triggered();
  void on_actionJTTY_triggered();
  void on_actionQ65_triggered();
  void on_actionFreqCal_triggered();
  void splash_done ();
  void on_measure_check_box_stateChanged (int);
  void on_sbNlist_valueChanged(int n);
  void on_sbNslots_valueChanged(int n);
  void on_sbF_Low_valueChanged(int n);
  void on_sbF_High_valueChanged(int n);
  void chk_FST4_freq_range();
  void on_pbFoxReset_clicked();
  void on_pbFreeText_clicked();
  void FoxReset(QString reason);
  void on_comboBoxHoundSort_activated (int index);
  void not_GA_warning_message ();
  void checkMSK144ContestType();
  void on_pbBestSP_clicked();
  void on_RoundRobin_currentTextChanged(QString text);
  void setTxMsg(int n);
  bool stdCall(QString const& w);
  void remote_configure (QString const& mode, quint32 frequency_tolerance, QString const& submode
                         , bool fast_mode, quint32 tr_period, quint32 rx_df, QString const& dx_call
                         , QString const& dx_grid, bool generate_messages);
  void callSandP2(int nline);
  void refreshHoundQueueDisplay();
  void queueActiveWindowHound2(QString text);
  void update_tx5(const QString &qsy_text);
  void reply_tx5(const QString &qsy_text);
  void setQSYMessageCreatorStatus(const bool &QSYMessageCreatorValue);
  void on_rbFixedTone_toggled(bool b);
  void on_rbEchoMessage_toggled(bool b);
  void on_rbEchoCW_toggled(bool b);
  void on_leEchoMessage_textChanged();
  void on_pbSendMessage_clicked();

  void on_pbF1_clicked();
  void on_pbF2_clicked();
  void on_pbF3_clicked();
  void on_pbF4_clicked();
  void on_pbF5_clicked();
  void on_pbF6_clicked();
  void on_pbF7_clicked();
  void on_pbF8_clicked();
#ifdef WIN32
  void logText(const QString &text);
#endif

private:
  enum class DecodeAlertSound { None, DXcall, Wanted };

  void applyExperimentalFT8Filter(const DecodedText& dt, bool& filtered);
  void processFoxSignals(const DecodedText& dt);
  void processSFoxVerification(const DecodedText& dt, bool& filtered);
  DecodedMessageReaction::ReactionDisposition processWaitReplyCall(
    DecodedText const& dt, DecodedMessageReaction::WaitDecodeSource source,
    bool * block_right_display = nullptr);
  bool applyFiltering(const DecodedText& dt, bool& filtered);
  void applyHighlighting(const DecodedText& dt, DisplayText * decodePane, bool updateAlertState,
                         bool& play_Wanted, bool& play_DXcall);
  void cycleRespondMode();
  static DecodeAlertSound selectDecodeAlertSound(bool alertsEnabled, bool dxCallAlertEnabled, bool wantedAlertEnabled,
                                                 bool play_Wanted, bool play_DXcall, bool hasDXCall);
  void playDecodeAlertSound(bool play_Wanted, bool play_DXcall);
  void playDecodeAlertSound(DecodeAlertSound sound);
  void updateRespondTarget(const DecodedText& dt, const QString& text, bool pounce);
  void displayDecodedTextLine(const DecodedText& dt, const QByteArray& line_read, const QString& distance, bool haveFSpread, float fSpread, bool bDisplayPoints);
  QString calculateDistanceAndBearing(const DecodedText& dt);
  void processSuperHoundVerification(const DecodedText& dt, bool& verified);

private:
  Q_SIGNAL void initializeAudioOutputStream (QAudioDeviceInfo,
      unsigned channels, unsigned msBuffered) const;
  Q_SIGNAL void stopAudioOutputStream () const;
  Q_SIGNAL void startAudioInputStream (QAudioDeviceInfo const&,
      int framesPerBuffer, AudioDevice * sink,
      unsigned downSampleFactor, AudioDevice::Channel) const;
  Q_SIGNAL void suspendAudioInputStream () const;
  Q_SIGNAL void resumeAudioInputStream () const;
  Q_SIGNAL void startDetector (AudioDevice::Channel) const;
  Q_SIGNAL void FFTSize (unsigned) const;
  Q_SIGNAL void detectorClose () const;
  Q_SIGNAL void finished () const;
  Q_SIGNAL void transmitFrequency (double) const;
  Q_SIGNAL void endTransmitMessage (bool quick = false) const;
  Q_SIGNAL void tune (bool = true) const;
  Q_SIGNAL void sendMessage (TxEvidence::TxRequest, SoundOutput *) const;
  Q_SIGNAL void startJttyStream (TxEvidence::TxRequest, SoundOutput *);
  Q_SIGNAL void endJttyStream () const;
  Q_SIGNAL void outAttenuationChanged (qreal) const;
  Q_SIGNAL void toggleShorthand () const;
  Q_SIGNAL void reset_audio_input_stream (bool report_dropped_frames) const;

private:
  void set_mode (QString const& mode);
  void dispatchTxRequest (TxEvidence::TxRequest const& request);
  void beginTxEvidenceSession ();
  void beginTxEvidenceGeneration (qint64 committedEndSample = -1,
                                  bool targetKnown = false);
  void recordTxSourceCommit (TxEvidence::TxStartSnapshot const& snapshot);
  void recordRawTxPlayout (TxEvidence::TxRawPlayoutSnapshot const& snapshot);
  void noteTxStopReason (TxEvidence::TxStopReason reason);
  void noteTxModeChange (QString const& mode);
  int txStopTailMs (bool tciAudio) const;
  void stopTxEvidence (int tailMs);
  void captureJttyTxEvidenceTotals (qint64 servedSamples, qint64 totalSamples,
                                    QString const& diagnostic);
  void astroUpdate ();
  void writeAllTxt(QString message);
  void auto_sequence (DecodedText const& message, unsigned start_tolerance, unsigned stop_tolerance);
  void trim_view (bool b);
  void foxTest();
  void setColorHighlighting();
  void chkFT4();
  bool elide_tx1_not_allowed () const;
  void readWidebandDecodes();
  void configActiveStations();
  bool sfox_tx();
  void clearSuperFoxPreparedTx();
  void abortSuperFoxTxStart();
  void displayFoxTxMsgs();
  void jtty_tx(QString message);
#ifdef WIN32
  void handleMmttyTxString(QString message);
  void handleMmttyStartTx();
  void handleMmttyStopTx();
  void handleMmttyAbortTx();
  void handleMmttyJttyAccepted(qint64 requestId);
  void handleMmttyJttyRejected(qint64 requestId, JttyTxRejectReason reason);
  void handleMmttyJttyCompleted(qint64 requestId);
  void handleMmttyJttySessionDrained(qint64 sessionId);
  void startPendingMmttyJttyTx();
  QString jttyRejectReasonText(JttyTxRejectReason reason) const;
#endif
  void execute_jtty_tx(qint64 requestId, QString message);
  void advanceJttyTxQueueEpoch();
  qint64 jttyTxCommittedSamples() const;
  void completeJttyTxEnqueue(qint64 requestId, QString const& message,
                             TxAudioQueueProgress progress, bool newSession,
                             bool useTciAudio);
  void recordAcceptedJttyTextRequest(qint64 requestId, qint64 endSample);
  QVector<qint64> takeCompletedJttyTextRequests(TxAudioQueueEpoch epoch,
                                               qint64 totalAtDrain);
  void clearAcceptedJttyTextRequests(TxAudioQueueEpoch epoch);
  void handleJttyContestSerial(QString const& message);
  void abort_jtty_tx();
  void interruptJttyTx();
  void rejectPendingJttyTciMessages(JttyTxRejectReason reason);
  void sync_tci_tx_volume (bool force = false);
  void onJttyBackendDrained(TxAudioQueueDrainState drain);
  void onJttyBackendEnqueueAccepted(qint64 enqueueId, qint64 sampleCount,
                                    TxAudioQueueProgress progress);
  void onJttyBackendEnqueueFailed(TxAudioQueueEpoch epoch, qint64 enqueueId);
  void handleJttyTxWatchdog();
  void resetJttyTxState();
  void startJttyTxWatchdog(int durationMs);
  void jtty_save_wav();
  bool jtty_key_struck(QKeyEvent * e);
  void jtty_decode(int k);
  void jtty_again();
  QString jtty_msg_expand(QString msg);
  QString specOpLabel() const;
  void initializeFFT(int nsps);
  void initializeFFT(int nsps, int fftSize);
  void setTxButtonsEnabled(bool enabled);
  void setDXInfo(QString const& call, QString const& grid);
  void setDecodeTitles(QString const& lh, QString const& rh);
  void setDecodeHeadings(QString const& lh, QString const& rh);
  void updateDecodeAccessibility();

  bool play_DXcall = false;
  bool play_Wanted = false;
  bool inSettings = false;

  bool m_event_filter_ready {false};
  QProcessEnvironment const& m_env;
  NetworkAccessManager m_network_manager;
  bool m_valid;
  QSplashScreen * m_splash;
  QString m_revision;
  bool m_multiple;
  bool m_automated_test;
  MultiSettings * m_multi_settings;
  QPushButton * m_configurations_button;
  QSettings * m_settings;
  QScopedPointer<Ui::MainWindow> ui;
  QButtonGroup * m_tx_message_button_group {nullptr};
  QFocusFrame * m_main_window_focus_frame {nullptr};
  QFrame * m_message_selector_focus_frame {nullptr};
  bool m_keyboard_focus_active {false};

#ifdef WIN32
  MMTTYIF * m_mmttyif {nullptr};
#endif

  Configuration m_config;
  QDir m_decoderDataDir;
  LogBook m_logBook;            // must be after Configuration construction
  Cloudlog m_cloudlog;
  WSPRBandHopping m_WSPR_band_hopping;
  bool m_WSPR_tx_next;
  MessageBox m_rigErrorMessageBox;
  QScopedPointer<SampleDownloader> m_sampleDownloader;
  QScopedPointer<EqualizationToolsDialog> m_equalizationToolsDialog;

  QScopedPointer<WideGraph> m_wideGraph;
  QScopedPointer<EchoGraph> m_echoGraph;
  QScopedPointer<FastGraph> m_fastGraph;
  QScopedPointer<LogQSO> m_logDlg;
  QScopedPointer<Astro> m_astroWidget;
  QScopedPointer<QSYMessageCreator> m_QSYMessageCreatorWidget;
  QScopedPointer<QSYMessage> m_QSYMessageWidget;
  QScopedPointer<QSYMonitor> m_qsymonitorWidget;
  QScopedPointer<HelpTextWindow> m_shortcuts;
  QScopedPointer<HelpTextWindow> m_prefixes;
  QScopedPointer<HelpTextWindow> m_mouseCmnds;
  QScopedPointer<MessageAveraging> m_msgAvgWidget;
  QScopedPointer<ActiveStations> m_ActiveStationsWidget;
  QScopedPointer<FoxLogWindow> m_foxLogWindow;
  QScopedPointer<CabrilloLogWindow> m_contestLogWindow;
  QScopedPointer<ColorHighlighting> m_colorHighlighting;
  Transceiver::TransceiverState m_rigState;
  Frequency  m_lastDialFreq;
  QString m_lastBand;
  QString m_lastCallsign;
  Frequency  m_dialFreqRxWSPR;  // best guess at WSPR QRG
  bool m_QSYMessageCreatorValue = false;
  bool m_qsymonitorValue = false;

  Detector * m_detector;
  unsigned m_FFTSize;
  AudioInputSource * m_soundInput;
  quint64 m_decodeCycleGeneration {0};
  Modulator * m_modulator;
  QScopedPointer<TxAudioQueue> m_jttyTxQueue;
  JttyTxStream * m_jttyTxStream;
  SoundOutput * m_soundOutput;
  int m_rx_audio_buffer_frames;
  int m_tx_audio_buffer_frames;
  QThread m_audioThread;

  qint64  m_msErase;
  qint64  m_secBandChanged;
  qint64  m_msDecStarted; //ft8md
  qint64  m_freqMoon;
  qint64  m_fullFoxCallTime;
  qint64  m_msEchoTxStart=0;

  Frequency m_freqNominal;
  Frequency m_freqNominalPeriod;
  Frequency m_freqTxNominal;
  Frequency m_msk144basefreq {0};
  quint64  m_mslastTX;   //ft8md
  qint32  m_nlasttx;     //ft8md
  qint32  m_lapmyc;      //ft8md
  Astro::Correction m_astroCorrection;
  bool m_reverse_Doppler;

  double  m_tRemaining;
  double  m_TRperiod;
  double  m_fSpread {0.};
  double  m_s6;
  double  m_fDither {0.};
  double  m_fAudioShift {0.};
  double  m_skedFreq {1296.065};

  float   m_DTtol;
  float   m_t0;
  float   m_t1;
  float   m_t0Pick;
  float   m_t1Pick;
  float   m_fCPUmskrtd;
  float   m_tEcho=0;

  qint32  m_waterfallAvg;
  qint32  m_ntx;
  bool m_gen_message_is_cq;
  bool m_send_RR73;
  qint32  m_timeout;
  qint32  m_XIT;
  qint32  m_setftx;
  qint32  m_ndepth;
  qint32  m_ncandthin; //ft8md

  //ft8md
  qint32  m_nFT8Cycles;
  qint32  m_nFT8SWLCycles;
  qint32  m_nFT8RXfSens;
  qint32  m_ft8threads;
  qint32  m_ft8Sensitivity;
  qint32  m_ft8DecoderStart;
  qint32  m_nsecBandChanged;
  qint32  m_nFT4depth;
  qint32  m_nsym_jtty;
  //ft8md

  qint32  m_sec0;
  qint32  m_RxLog;
  qint32  m_nutc0;
  qint32  m_ntr;
  qint32  m_tx;
  quint64  m_mslastMon;
  int     m_addtx;
  quint32 m_delay;
  qint32  m_hsym;
  qint32  m_nsps;
  qint32  m_hsymStop;
  qint32  m_inGain;
  qint32  m_ncw;
  qint32  m_secID;
  qint32  m_idleMinutes;
  qint32  m_nSubMode;
  qint32  m_nclearave;
  qint32  m_minSync;
  qint32  m_dBm;
  qint32  m_nWSPRdecodes;
  qint32  m_k0;
  qint32  m_kdone;
  qint32  m_nPick;
  FrequencyList_v2_101::const_iterator m_frequency_list_fcal_iter;
  qint32  m_nTx73;
  qint32  m_UTCdisk;
  QDateTime    m_UTCdiskDateTime;
  qint32  m_wait;
  qint32  m_isort;
  qint32  m_max_dB;
  qint32  m_nDXped=0;
  qint32  m_nSortedHounds=0;
  qint32  m_nHoundsCalling=0;
  qint32  m_Nlist=12;
  qint32  m_Nslots=5;
  qint32  m_Nslots0=0;
  qint32  m_nFoxMsgTimes[5]={0,0,0,0,0};
  qint32  m_tAutoOn;
  qint32  m_tFoxTx=0;
  qint32  m_tFoxTx0=0;
  qint32  m_maxStrikes=3;      //Max # of repeats: 3 strikes and you're out
  qint32  m_maxFoxWait=3;      //Max wait time for expected Hound replies
  qint32  m_foxCQtime=10;      //CQs at least every 5 minutes
  qint32  m_tFoxTxSinceCQ=999; //Fox Tx cycles since most recent CQ
  HoundTransmissionPolicy::State m_houndTransmissionState;
  qint32  m_kin0=0;
  qint32  m_earlyDecode=41;
  qint32  m_earlyDecode2=47;
  qint32  m_nDecodes=0;
  qint32  m_maxPoints=-1;
  qint32  m_latestDecodeTime=-1;
  qint32  m_points=-99;
  qint32  m_score=0;
  qint32  m_fDop=0;
  qint32  m_echoSec0=0;
  qint32  m_fetched=0;
  qint32  m_position;
  qint64  m_decoderDiagSequence=0;
  qint64  m_decoderDiagActiveSequence=0;
  qint32  m_nextDecoderGeneration=0;
  qint32  m_decoderDiagStartIhsym=0;
  qint32  m_decoderDiagStartHsymStop=0;
  qint32  m_decoderDiagStartNzhsym=0;
  qint32  m_decoderDiagStartNewdat=0;
  qint32  m_decoderDiagStartNagain=0;
  qint32  m_decoderDiagStartNdiskdat=0;
  double  m_decoderDiagStartTRperiod=0.0;
  QElapsedTimer m_decoderDiagElapsedTimer;
  QString m_decoderDiagStartMode;
  QMap<QString, QDateTime> m_decoderDiagLastSampleUtc;

  bool    m_btxok;		//True if OK to transmit
  bool    m_diskData;
  bool    m_loopall;
  DecodeOwner m_decodeOwner=DecodeOwner::None;
  Jt9ProcessPhase m_jt9ProcessPhase=Jt9ProcessPhase::InitialStarting;
  bool    m_modeLocked = false;
  bool    m_decoderDiagActive=false;
  bool    m_decoderDiagBusyRequestLogged=false;
  bool    m_decoderDiagOverrunLogged=false;
  bool    m_decoderDiagHardHangLogged=false;
  bool    m_decoderDiagAbnormalClear=false;
  bool    m_decoderCompletedSinceStart=false;
  bool    m_jt9PayloadValid=false;
  bool    m_closing=false;
  bool    m_txFirst;
  bool    m_auto;
  bool    m_restart;
  bool    m_generated_message_error;
  bool    m_startAnother;
  ActiveJt9Decode m_activeJt9Decode;
  DecoderOutputFramer m_decoderOutputFramer;
  Ft8MtdDecodeCoordinator m_ft8MtdDecodeCoordinator;

  // start ft8md
  bool    m_FT8EarlyStart;   
  bool    m_FT8WideDxCallSearch; 
  bool    m_skipTx1;
  bool    m_swl; 
  bool    m_filter;
  bool    m_agcc;
  bool    m_hint;
  bool	  m_multithreadFT8; 
  bool 	  m_houndMode;
  bool    m_commonFT8b;
  bool	  m_manualDecode;   
  bool	  m_modeChanged;   
  bool    m_bMyCallStd;
  bool    m_bHisCallStd;
  bool    m_multInst;
  bool    m_bandChanged;
  bool 	  m_lasthint; 
  // end ft8md

  bool    m_saveDecoded;
  bool    m_saveAll;
  bool    m_widebandDecode;
  bool    m_call3Modified;
  bool    m_dataAvailable;
  bool    m_bDecoded;
  bool    m_noSuffix;
  bool    m_decodedText2;
  bool    m_sentFirst73;
  bool	  m_tci_mod_active;
  bool    m_tci;
  bool    m_tci_audio;
  int     m_currentMessageType;
  QString m_currentMessage;
  int     m_lastMessageType;
  QString m_lastMessageSent;
  QString m_tBlankLine;
  bool    m_bShMsgs;
  bool    m_bSWL;
  bool    m_uploadWSPRSpots;
  bool    m_grid6;
  bool    m_tuneup;
  bool    m_bTxTime;
  bool    m_rxDone;
  bool    m_bSimplex; // not using split even if it is available
  bool    m_bEchoTxOK;
  bool    m_bTransmittedEcho;
  bool    m_bEchoTxed;
  bool    m_bFastMode=false;
  bool    m_bFast9;
  bool    m_bFastDecodeCalled;
  bool    m_bDoubleClickAfterCQnnn;
  bool    m_bRefSpec;
  bool    m_bClearRefSpec;
  bool    m_bTrain;
  bool    m_bUseRef;
  bool    m_bFastDone;
  bool    m_bAltV;
  bool    m_bNoMoreFiles;
  bool    m_bDoubleClicked;
  bool    m_bCallingCQ;
  bool    m_bAutoReply;
  QString m_lastloggedcall; //ft8md
  bool    m_contestModeHintShown;
  bool    m_bWarnedSplit=false;
  bool    m_bTUmsg;
  bool    m_bBestSPArmed=false;
  bool    m_bOK_to_chk=false;
  bool    m_bSentReport=false;
  bool    m_discard_decoded_hounds_this_cycle=false;     // if something changes, like frequency, discard decoded messages that may be in-flight.

  SpecOp  m_specOp;

  static constexpr QsoProgress CALLING {QsoProgress::Calling};
  static constexpr QsoProgress REPLYING {QsoProgress::Replying};
  static constexpr QsoProgress REPORT {QsoProgress::Report};
  static constexpr QsoProgress ROGER_REPORT {QsoProgress::RogerReport};
  static constexpr QsoProgress ROGERS {QsoProgress::Rogers};
  static constexpr QsoProgress SIGNOFF {QsoProgress::Signoff};
  QsoProgress m_QSOProgress;

  enum {CALL, GRID, DXCC, MULT};

  int			m_ihsym;
  int			m_nzap;
  int			m_npts8;
  float		m_px;
  float   m_pxmax;
  float		m_df3;
  int			m_iptt0;
  bool		m_btxok0;
  int			m_nsendingsh;
  double	m_onAirFreq0;
  bool		m_first_error;

  char    m_msg[100][80];

  // labels in status bar
  QLabel tx_status_label;
  QLabel config_label;
  QLabel mode_label;
  QLabel last_tx_label;
  QLabel auto_tx_label;
  QLabel band_hopping_label;
  QLabel ndecodes_label;
  QProgressBar progressBar;
  QLabel watchdog_label;
  WavLoadCoordinator m_wav_load_coordinator;
  QFutureWatcher<void> watcher3;
  QFutureSynchronizer<QString> m_saveWAVSynchronizer;
  QFutureWatcher<QString> m_saveWAVWatcher;

  NonInheritingProcess proc_jt9;
  NonInheritingProcess p1;
  NonInheritingProcess p3;

  QProcess p2;
  QProcess p4;

  WSPRNet *wsprNet;
  EQSL *Eqsl;

  QTimer m_guiTimer;
  QTimer m_decoderShutdownTimer;
  QTimer m_decoderTerminateTimer;
  QTimer m_decoderKillTimer;
  QTimer m_decoderStartTimer;
  QTimer stopWRTimer;               //Wait & Reply
  QTimer stopWCTimer;               //Wait & Call
  QTimer ptt1Timer;                 //StartTx delay
  QTimer ptt0Timer;                 //StopTx delay
  QTimer logQSOTimer;
  QTimer killFileTimer;
  QTimer tuneButtonTimer;
  QTimer uploadTimer;
  QTimer tuneATU_Timer;
  QTimer TxAgainTimer;
  QTimer minuteTimer;
  QTimer splashTimer;
  QTimer p1Timer;
  QTimer m_jttyTxWatchdog;
  QTimer m_refSpecTimer;
  AutoRespondSelectionLatch m_autoRespondSelectionLatch;
  AutoRespondScores m_autoRespondScores;
  int m_refSpecSecondsRemaining = 0;

  QString m_path;
  QString m_baseCall;
  QString m_hisCall;
  QString m_hisGrid;
  QString m_appDir;
  QString m_cqStr;
  QString m_palette;
  QString m_dateTime;
  QString m_mode;
  QString m_modeTx;//ft8md
  QString m_fnameWE;            // save path without extension
  QString m_rpt;
  QString m_nextRpt;
  QString m_rptSent;
  QString m_rptRcvd;
  QString m_qsoStart;
  QString m_qsoStop;
  QStringList m_cmndP1;
  QString m_msgSent0;
  QString m_calls;
  QString m_CQtype;
  QString m_opCall;
  QString m_houndCallers;        //Sorted list of Hound callers
  QString m_fm0;
  QString m_fm1;
  QString m_xSent;               //Contest exchange sent
  QString m_xRcvd;               //Contest exchange received
  QString m_currentBand;
  QString m_currentBandPeriod;
  QString m_nextCall;
  QString m_nextGrid;
  QString m_fileDateTime;
  QString m_inQSOwith;
  QString m_BestCQpriority;
  QString m_deCall;
  QString m_deGrid;
  QString m_freeTextMsg;
  QString m_freeTextMsg0;
  std::array<QString, MaxActiveStationRows> m_ready2call;
  std::array<QString, MaxQ65PileupCallers> m_callers;

  QSet<QString> m_pfx;
  QSet<QString> m_sfx;

  struct FoxQSO       //Everything we need to know about QSOs in progress (or recently logged).
  {
    QString grid;       //Hound's declared locator
    QString sent;       //Report sent to Hound
    QString rcvd;       //Report received from Hound
    qint32  ncall;      //Number of times report sent to Hound
    qint32  nRR73;      //Number of times RR73 sent to Hound
    qint32  tFoxRrpt;   //m_tFoxTx (Fox Tx cycle counter) when R+rpt was received from Hound
    qint32  tFoxTxRR73; //m_tFoxTx when RR73 was sent to Hound
  };

  QMap<QString,FoxQSO> m_foxQSO;       //Key = HoundCall, value = parameters for QSO in progress
  QMap<QString,QString> m_loggedByFox; //Key = HoundCall, value = logged band
  QMap<QString,qint32> m_annotated_callsigns;  //Key = HoundCall, value = provided by api call

  struct FixupQSO       //Info for fixing Fox's log from file "FoxQSO.txt"
  {
    QString grid;       //Hound's declared locator
    QString sent;       //Report sent to Hound
    QString rcvd;       //Report received from Hound
    QDateTime QSO_time;
  };
  QMap<QString,FixupQSO> m_fixupQSO;       //Key = HoundCall, value = info for QSO in progress

  struct ActiveCall
  {
    QString grid4;
    QString bands;
    qint32 az;
    qint32 points;
  };
  QMap<QString,ActiveCall> m_activeCall;   //Key = callsign, value = grid4, az, points for ARRL_DIGI

  struct EMECall
  {
    QString grid4;
    double frx;
    double fsked;
    qint32 nsnr;
    qint32 t;
    bool worked;
    bool ready2call;
    QString submode;
  };
  QMap<QString,EMECall> m_EMECall;

  QMap<QString,bool> m_EMEworked;

  struct RecentCall
  {
    qint64 dialFreq;
    qint32 audioFreq;
    qint32 snr;
    qint32 decodeTime;
    bool   txEven;
    bool   ready2call;
  };
  QMap<QString,RecentCall> m_recentCall;   //Key = callsign, value = snr, dialFreq, audioFreq, decodeTime
  struct ARRL_logged
  {
    QDateTime time;
    QString band;
    qint32 points;
  };
  QList<ARRL_logged> m_arrl_log;

  QQueue<QString> m_houndQueue;        //Selected Hounds available for starting a QSO
  QQueue<QString> m_foxQSOinProgress;  //QSOs in progress: Fox has sent a report
  QQueue<qint64>  m_foxRateQueue;

  QDateTime m_dateTimeQSOOn;
  QDateTime m_dateTimeLastTX;
  QDateTime m_dateTimeSentTx3;
  QDateTime m_dateTimeRcvdRR73;
  QDateTime m_dateTimeBestSP;
  QDateTime m_dateTimeSeqStart;        //Nominal start time of Rx sequence about to be decoded

  QSharedMemory *mem_jt9;
  QString m_QSOText;
  unsigned m_downSampleFactor;
  QThread::Priority m_audioThreadPriority;
  bool m_bandEdited;
  bool m_splitMode;
  bool m_monitoring=false;
  bool m_echoRunning=false;
  bool m_tx_when_ready;
  bool m_transmitting;
  bool m_tune;
  bool m_tx_watchdog;           // true when watchdog triggered
  TxEvidence::TxPlaybackDiagnostics m_txPlaybackDiagnostics;
  TxEvidence::TxSessionId m_txEvidenceSession;
  TxEvidence::TxSessionId m_txEvidenceSourceSession;
  TxEvidence::TxGeneration m_txEvidenceGeneration;
  TxEvidence::TxStopReason m_pendingTxStopReason {TxEvidence::TxStopReason::NormalEnd};
#if defined (WSJT_ENABLE_LIVE_AUDIO_TEST)
  qint64 m_liveAudioTestFt8StartWindowOpenMs {-1};
  qint64 m_liveAudioTestFt8StartWindowCloseMs {-1};
  qint64 m_liveAudioTestFt8StartSessionId {-1};
  qint64 m_liveAudioTestFt8StartGeneration {-1};
#endif
  bool m_jttyTxActive;
  bool m_jttyTxUsesTciAudio;
  TxAudioQueueEpoch m_jttyTxQueueEpoch;
  TxAudioQueueProgress m_jttyTxQueueProgress;
  struct PendingJttyTciMessage
  {
    TxAudioQueueEpoch epoch;
    qint64 enqueueId;
    qint64 requestId;
    qint64 sampleCount;
    QString message;
    bool newSession;
  };
  QVector<PendingJttyTciMessage> m_pendingJttyTciMessages;
  struct AcceptedJttyTxRequest
  {
    TxAudioQueueEpoch epoch;
    qint64 requestId;
    qint64 endSample;
  };
  QVector<AcceptedJttyTxRequest> m_acceptedJttyTxRequests;
  qint64 m_jttyTxRequestId;
  qint64 m_jttyTciEnqueueId;
  struct JttyQsoLine
  {
    QString text;        // full text currently shown on this line
    QTextBlock block;    // the decodedTextBrowser2 paragraph holding it
  };
  QVector<JttyQsoLine> m_jttyQsoLines;   // one entry per concurrently-growing JTTY transmission
  int m_jttyLastAllFreqsK = -1;          // detects a restarted decode (new WAV, or "decode again")
  qint32 m_jttyLastSavedWavK0 = -1;      // m_k0 at last JTTY WAV save; skips saving unchanged audio again
  QTextBlock m_jttyAllFreqsGroupStart;   // start of decodedTextBrowser's currently-growing group
#ifdef WIN32
  bool m_mmttyJttyStartRequested;
  bool m_mmttyJttyFinishRequested;
  bool m_mmttyJttyOutputPending;
  QHash<qint64, QString> m_mmttyJttyRequests;
#endif
  bool m_block_pwr_tooltip;
  bool m_PwrBandSetOK;
  bool m_bDisplayedOnce;
  Frequency m_lastMonitoredFrequency;
  double m_toneSpacing;
  QTimer m_heartbeat;
  MessageClient * m_messageClient;
  PSKReporter m_psk_Reporter;
  DisplayManual m_manual;
  QHash<QString, QVariant> m_pwrBandTxMemory; // Remembers power level by band
  QHash<QString, QVariant> m_pwrBandTuneMemory; // Remembers power level by band for tuning
  QByteArray m_geometryNoControls;
  QVector<double> m_phaseEqCoefficients;
  bool m_block_udp_status_updates;
  bool m_useDarkStyle;

  //---------------------------------------------------- private functions
  void readSettings();
  void set_application_font (QFont const&);
  void setDecodedTextFont (QFont const&);
  void writeSettings();
  void createStatusBar();
  void updateStatusBar();
  void updateMainWindowAccessibility();
  void registerMainWindowFocusControls();
  std::array<QRadioButton *, 6> txNextButtons() const;
  std::array<QWidget *, 13> focusIndicatorWidgets() const;
  void showMainWindowFocusIndicator(QWidget *widget);
  void hideMainWindowFocusIndicators();
  void updateTxNextFocusPolicies();
  bool switchTxNextMessage(QKeyEvent const *key_event);
  bool switchMainWindowTab(QKeyEvent const *key_event);
  void genStdMsgs(QString rpt, bool unconditional = false);
  void genCQMsg();
  void clearDX ();
  void lookup();
  QString expandTxMacros(QString const& message) const;
  void ba2msg(QByteArray ba, char* message);
  void msgtype(QString t, QLineEdit* tx);
  void show_generated_message_error ();
  void clear_generated_message_error ();
  void update_generated_message_error ();
  void stub();
  void statusChanged();
  void fixStop();
  void finishReferenceSpectrumMeasurement(bool notify);
  void updateReferenceSpectrumCountdown();
  bool shortList(QString callsign) const;
  void transmit (double snr = 99.);
  void rigFailure (QString const& reason);
  void pskSetLocal ();
  void pskPost(DecodedText const& decodedtext);
  void pskPost(DecodedText const& decodedtext,
               DecodeOperatingContext const& context);
  void displayDialFrequency ();
  void transmitDisplay (bool);
  void processMessage(DecodedText const& message, Qt::KeyboardModifiers = Qt::NoModifier,
                      bool from_udp_reply = false);
  void processSyntheticMessage(DecodedText const& message);
  DecodedMessageReaction::QsoReactionSnapshot qsoReactionSnapshot(
    Qt::KeyboardModifiers modifiers = Qt::NoModifier, bool from_udp_reply = false) const;
  void applyQsoReactionPlan(DecodedMessageReaction::QsoReactionPlan const& plan,
                            DecodedText const& message, bool * block_right_display = nullptr);
  void applyQsoReactionEffect(DecodedMessageReaction::QsoReactionEffect const& effect,
                              DecodedText const& message, bool * block_right_display);
  void showContestHint(DecodedMessageReaction::ContestHint hint);
  void refreshQsoPane(DecodedText const& message);
  void replyToCQ (QTime, qint32 snr, float delta_time, quint32 delta_frequency, QString const& mode, QString const& message_text, bool low_confidence, quint8 modifiers);
  void locationChange(QString const& location);
  void replayDecodes ();
  void postDecode (bool is_new, QString const& message);
  void postWSPRDecode (bool is_new, QStringList message_parts);
  void enable_DXCC_entity (bool on);
  void switch_mode (Mode);
  bool hasMsk144BaseFrequency () const {return m_msk144basefreq > 0;}
  void WSPR_scheduling ();
  void freqCalStep();
  void setRig (Frequency = 0);  // zero frequency means no change
  void WSPR_history(Frequency dialFreq, int ndecodes);
  QString beacon_start_time (int n = 0);
  QString WSPR_message();
  void fast_config(bool b);
  void CQTxFreq();
  void useNextCall();
  void abortQSO();
  void updateRate();
  void write_all(QString txRx, QString message,
                 DecodeOperatingContext const * context = nullptr);
  bool isWorked(int itype, QString key, float fMHz=0, QString="");

  void hound_reply (int foxFrequency);
  QString sortHoundCalls(QString t, int isort, int max_dB);
  void rm_tb4(QString houndCall);
  void read_wav_file (QString const& fname);
  void wav_file_loaded ();
  void update_wav_file_actions ();
  void finishDecodeUi ();
  bool subProcessFailed (QProcess *, int exit_code, QProcess::ExitStatus);
  void subProcessError (QProcess *, QProcess::ProcessError);
  void statusUpdate () const;
  void update_watchdog_label ();
  void invalidate_frequencies_filter ();
  void on_the_minute ();
  void add_child_to_event_filter (QObject *);
  void remove_child_from_event_filter (QObject *);
  void setup_status_bar (bool vhf);
  void tx_watchdog (bool triggered);
  qint64  nWidgets(QString t);
  void displayWidgets(qint64 n);
  QChar current_submode () const; // returns QChar {0} if submode is not appropriate
  void write_transmit_entry (QString const& file_name);
  void selectHound(QString t, bool bTopQueue);
  void removeHoundFromCallingList(QString callsign);
  void houndCallers();
  void updateFoxQSOsInProgressDisplay();
  void foxQueueTopCallCommand();
  void foxRxSequencer(QString msg, QString houndCall, QString rptRcvd);
  bool foxTxSequencer();
  void foxGenWaveform(int i,QString fm);
  void writeFoxQSO (QString const& msg);
  void update_foxLogWindow_rate();
  DecodePublishResult publishDecodeRequest(
      bool copySamples,
      Ft8MtdDecodeCoordinator::Stage ft8Stage = Ft8MtdDecodeCoordinator::Stage::None,
      qint64 ft8Period = -1);
  DecodePublishResult publishPendingFt8Decode ();
  void decode (Ft8MtdDecodeCoordinator::Stage stage);
  qint64 currentFt8DecodePeriod () const;
  bool usesFt8MtdFinal () const;
  int configuredFt8MtdEarlyStageCount () const;
  std::unique_ptr<Ft8MtdDecodeCoordinator::PendingMtdDecode>
    capturePendingFt8MtdDecode (qint64 period) const;
  void cancelPendingFt8Decode (QString const& reason);
  void reportFt8BackpressureDecision (
      Ft8MtdDecodeCoordinator::Decision const& decision, qint64 period);
  void reportFt8BackpressureRecovery (qint64 period);
  void emitFt8DecoderInvocation (decoder_params_t const& params) const;
  bool handleDecoderOutputEvent(DecoderOutputFramer::Event const& event,
                                bool& decodeCompleted);
  DecodeOperatingContext currentDecodeOperatingContext() const;
  bool decodeOperatingContextMatchesCurrent(
      DecodeOperatingContext const& context) const;
  bool pendingFt8DecodeOperatingContextMatchesCurrent(
      DecodeOperatingContext const& context) const;
  bool activeDecodeOperatingContextMatchesCurrent() const;
  bool initializeDecoderSharedMemory();
  void startDecoderProcess();
  bool beginDecode(
      DecodeOwner owner, decoder_params_t const * diagnosticParams = nullptr,
      DecodeOperatingContext const * diagnosticContext = nullptr);
  void endDecode(DecodeOwner owner);
  void updateDecodeControls();
  bool usesJt9Process() const;
  bool decoderRestartInProgress() const;
  void abortJt9Transaction();
  void recoverDecoderAtBoundary(QString const& reason, bool manual);
  void requestDecoderRestart(QString const& reason);
  qint64 decoderDiagnosticElapsedMs() const;
  qint64 decoderRequestDeadlineMs() const;
  bool decoderRequestDeadlineExpired() const;
  void beginDecoderDiagnostic(
      decoder_params_t const * params = nullptr,
      DecodeOperatingContext const * context = nullptr);
  void logDecoderBusyRequest(QString const& reason);
  void logDecoderProgress();
  void logDecoderAbnormalClear(QString const& reason);
  void finishDecoderDiagnostic();
  void clearHungDecoderStatus(QString const& reason);
  bool is77BitMode () const;
  void cease_auto_Tx_after_QSO ();
  Q_SLOT void ARRL_Digi_Display();
  void ARRL_Digi_Update(DecodedText dt);
  void activeWorked(QString call, QString band);
  void read_log();
  void refreshPileupList();
  void handleVerifyMsg(int status, QDateTime ts, QString callsign, QString code, unsigned int hz, QString const &response);
  void writeFoxTxMsgs();
#ifdef FOX_OTP
  QString foxOTPcode();
#endif
};

extern int killbyname(const char* progName);
extern void getDev(int* numDevices,char hostAPI_DeviceName[][50],
                   int minChan[], int maxChan[],
                   int minSpeed[], int maxSpeed[]);
extern int next_tx_state(int pctx);

#endif // MAINWINDOW_H
