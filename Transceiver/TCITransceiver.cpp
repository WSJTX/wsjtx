#include "Transceiver/TCITransceiver.hpp"

#include <QRegularExpression>
#include <QLocale>
#include <QThread>
#include <qmath.h>

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
#include <QRandomGenerator>
#endif

#include <QString>
#include "widgets/itoneAndicw.h"
#include "Network/NetworkServerLookup.hpp"
#include "moc_TCITransceiver.cpp"

#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QDateTime>
#include <QTimer>

namespace
{
  char const * const TCI_transceiver_1_name {"TCI Client RX1"};
  char const * const TCI_transceiver_2_name {"TCI Client RX2"};

  QString map_mode (Transceiver::MODE mode)
  {
    switch (mode)
      {
      case Transceiver::AM: return "am";
      case Transceiver::CW: return "cw";
//      case Transceiver::CW_R: return "CW-R";
      case Transceiver::USB: return "usb";
      case Transceiver::LSB: return "lsb";
//      case Transceiver::FSK: return "RTTY";
//      case Transceiver::FSK_R: return "RTTY-R";
      case Transceiver::DIG_L: return "digl";
      case Transceiver::DIG_U: return "digu";
      case Transceiver::FM: return "wfm";
      case Transceiver::DIG_FM:
        return "nfm";
      default: break;
      }
    return "";
  }
  static const QString SmTZ(";");
  static const QString SmDP(":");
  static const QString SmCM(",");
  static const QString SmTrue("true");
  static const QString SmFalse("false");

  // Command maps
  static const QString CmdDevice("device");
  static const QString CmdReceiveOnly("receive_only");
  static const QString CmdTrxCount("trx_count");
  static const QString CmdChannelCount("channels_count");
  static const QString CmdVfoLimits("vfo_limits");
  static const QString CmdIfLimits("if_limits");
  static const QString CmdModeList("modulations_list");
  static const QString CmdMode("modulation");
  static const QString CmdReady("ready");
  static const QString CmdStop("stop");
  static const QString CmdStart("start");
  static const QString CmdPreamp("preamp");
  static const QString CmdDds("dds");
  static const QString CmdIf("if");
  static const QString CmdTrx("trx");
  static const QString CmdRxEnable("rx_enable");
  static const QString CmdTxEnable("tx_enable");
  static const QString CmdRxChannelEnable("rx_channel_enable");
  static const QString CmdRitEnable("rit_enable");
  static const QString CmdRitOffset("rit_offset");
  static const QString CmdXitEnable("xit_enable");
  static const QString CmdXitOffset("xit_offset");
  static const QString CmdSplitEnable("split_enable");
  static const QString CmdIqSR("iq_samplerate");
  static const QString CmdIqStart("iq_start");
  static const QString CmdIqStop("iq_stop");
  static const QString CmdCWSpeed("cw_macros_speed");
  static const QString CmdCWDelay("cw_macros_delay");
  static const QString CmdSpot("spot");
  static const QString CmdSpotDelete("spot_delete");
  static const QString CmdFilterBand("rx_filter_band");
  static const QString CmdVFO("vfo");
  static const QString CmdVersion("protocol"); //protocol:esdr,1.2;
  static const QString CmdTune("tune");
  static const QString CmdRxMute("rx_mute");
  static const QString CmdSmeter("rx_smeter");
  static const QString CmdPower("tx_power");
  static const QString CmdSWR("tx_swr");
  static const QString CmdECoderRX("ecoder_switch_rx");
  static const QString CmdECoderVFO("ecoder_switch_channel");
  static const QString CmdAudioSR("audio_samplerate");
  static const QString CmdAudioStart("audio_start");
  static const QString CmdAudioStop("audio_stop");
  static const QString CmdAppFocus("app_focus");
  static const QString CmdVolume("volume");
  static const QString CmdSqlEnable("sql_enable");
  static const QString CmdSqlLevel("sql_level");
  static const QString CmdDrive("drive");
  static const QString CmdTuneDrive("tune_drive");
  static const QString CmdMute("mute");
  static const QString CmdRxSensorsEnable("rx_sensors_enable");
  static const QString CmdTxSensorsEnable("tx_sensors_enable");
  static const QString CmdRxSensors("rx_sensors");
  static const QString CmdTxSensors("tx_sensors");
  static const QString CmdAgcMode("agc_mode");
  static const QString CmdAgcGain("agc_gain");
  static const QString CmdLock("lock");
}

extern "C" {
  void   fil4_(qint16*, qint32*, qint16*, qint32*, short int*);
}
extern dec_data dec_data;

extern float gran();		// Noise generator (for tests only)

#define RAMP_INCREMENT 64  // MUST be an integral factor of 2^16

#if defined (WSJT_SOFT_KEYING)
# define SOFT_KEYING WSJT_SOFT_KEYING
#else
# define SOFT_KEYING 1
#endif

double constexpr TCITransceiver::m_twoPi;

void TCITransceiver::register_transceivers (logger_type *, TransceiverFactory::Transceivers * registry, unsigned id1, unsigned id2)
{
  (*registry)[TCI_transceiver_1_name] = TransceiverFactory::Capabilities {id1, TransceiverFactory::Capabilities::tci, true};
  (*registry)[TCI_transceiver_2_name] = TransceiverFactory::Capabilities {id2, TransceiverFactory::Capabilities::tci, true};
}

static constexpr quint32 AudioHeaderSize = 16u*sizeof(quint32);

TCITransceiver::TCITransceiver (logger_type * logger, std::unique_ptr<TransceiverBase> wrapped,QString const& rignr,
                               QString const& address, bool use_for_ptt,
                               int poll_interval, QObject * parent)
  : PollingTransceiver {logger, poll_interval, parent}
  , wrapped_ {std::move (wrapped)}
  , rx_ {rignr}
  , server_ {address}
  , use_for_ptt_ {use_for_ptt}
  , errortable {tr("ConnectionRefused"),
                tr("RemoteHostClosed"),
                tr("HostNotFound"),
                tr("SocketAccess"),
                tr("SocketResource"),
                tr("SocketTimeout"),
                tr("DatagramTooLarge"),
                tr("Network"),
                tr("AddressInUse"),
                tr("SocketAddressNotAvailable"),
                tr("UnsupportedSocketOperation"),
                tr("UnfinishedSocketOperation"),
                tr("ProxyAuthenticationRequired"),
                tr("SslHandshakeFailed"),
                tr("ProxyConnectionRefused"),
                tr("ProxyConnectionClosed"),
                tr("ProxyConnectionTimeout"),
                tr("ProxyNotFound"),
                tr("ProxyProtocol"),
                tr("Operation"),
                tr("SslInternal"),
                tr("SslInvalidUserData"),
                tr("Temporary"),
                tr("UnknownSocket") }
  , error_ {""}
  , do_snr_ {(poll_interval & do__snr) == do__snr}
  , do_pwr_ {(poll_interval & do__pwr) == do__pwr}
  , rig_power_ {(poll_interval & rig__power) == rig__power}
  , rig_power_off_ {(poll_interval & rig__power_off) == rig__power_off}
  , tci_audio_ {(poll_interval & tci__audio) == tci__audio}
  , commander_ {nullptr}
  , tci_timer1_ {nullptr}
  , tci_loop1_ {nullptr}
  , tci_timer2_ {nullptr}
  , tci_loop2_ {nullptr}
  , tci_timer3_ {nullptr}
  , tci_loop3_ {nullptr}
  , tci_timer4_ {nullptr}
  , tci_loop4_ {nullptr}
  , tci_timer5_ {nullptr}
  , tci_loop5_ {nullptr}
  , tci_timer6_ {nullptr}
  , tci_loop6_ {nullptr}
  , tci_timer7_ {nullptr}
  , tci_loop7_ {nullptr}
  , tci_timer8_ {nullptr}
  , tci_loop8_ {nullptr}
  , wavptr_ {nullptr}
  , m_downSampleFactor {4}
  , m_buffer ((m_downSampleFactor > 1) ?
              new short [max_buffer_size * m_downSampleFactor] : nullptr)
  , m_quickClose {false}
  , m_phi {0.0}
  , m_toneSpacing {0.0}
  , m_fSpread {0.0}
  , m_state {Idle}
  , m_tuning {false}
  , m_cwLevel {false}
  , m_j0 {-1}
  , m_toneFrequency0 {1500.0}
  , wav_file_ {QDir(QStandardPaths::writableLocation (QStandardPaths::DataLocation)).absoluteFilePath ("tx.wav").toStdString()}
{
  m_samplesPerFFT = 6912 / 2;
  tci_Ready = false;
  trxA = 0;
  trxB = 0;
  cntIQ = 0;
  bIQ = false;
  inConnected = false;
  audioSampleRate = 48000u;
  mapCmd_[CmdDevice]       = Cmd_Device;
  mapCmd_[CmdReceiveOnly]  = Cmd_ReceiveOnly;
  mapCmd_[CmdTrxCount]     = Cmd_TrxCount;
  mapCmd_[CmdChannelCount] = Cmd_ChannelCount;
  mapCmd_[CmdVfoLimits]    = Cmd_VfoLimits;
  mapCmd_[CmdIfLimits]     = Cmd_IfLimits;
  mapCmd_[CmdModeList]     = Cmd_ModeList;
  mapCmd_[CmdMode]         = Cmd_Mode;
  mapCmd_[CmdReady]        = Cmd_Ready;
  mapCmd_[CmdStop]         = Cmd_Stop;
  mapCmd_[CmdStart]        = Cmd_Start;
  mapCmd_[CmdPreamp]       = Cmd_Preamp;
  mapCmd_[CmdDds]          = Cmd_Dds;
  mapCmd_[CmdIf]           = Cmd_If;
  mapCmd_[CmdTrx]          = Cmd_Trx;
  mapCmd_[CmdRxEnable]     = Cmd_RxEnable;
  mapCmd_[CmdTxEnable]     = Cmd_TxEnable;
  mapCmd_[CmdRxChannelEnable] = Cmd_RxChannelEnable;
  mapCmd_[CmdRitEnable]    = Cmd_RitEnable;
  mapCmd_[CmdRitOffset]    = Cmd_RitOffset;
  mapCmd_[CmdXitEnable]    = Cmd_XitEnable;
  mapCmd_[CmdXitOffset]    = Cmd_XitOffset;
  mapCmd_[CmdSplitEnable]  = Cmd_SplitEnable;
  mapCmd_[CmdIqSR]         = Cmd_IqSR;
  mapCmd_[CmdIqStart]      = Cmd_IqStart;
  mapCmd_[CmdIqStop]       = Cmd_IqStop;
  mapCmd_[CmdCWSpeed]      = Cmd_CWSpeed;
  mapCmd_[CmdCWDelay]      = Cmd_CWDelay;
  mapCmd_[CmdFilterBand]   = Cmd_FilterBand;
  mapCmd_[CmdVFO]          = Cmd_VFO;
  mapCmd_[CmdVersion]      = Cmd_Version;
  mapCmd_[CmdTune]         = Cmd_Tune;
  mapCmd_[CmdRxMute]       = Cmd_RxMute;
  mapCmd_[CmdSmeter]       = Cmd_Smeter;
  mapCmd_[CmdPower]        = Cmd_Power;
  mapCmd_[CmdSWR]          = Cmd_SWR;
  mapCmd_[CmdECoderRX]     = Cmd_ECoderRX;
  mapCmd_[CmdECoderVFO]    = Cmd_ECoderVFO;
  mapCmd_[CmdAudioSR]      = Cmd_AudioSR;
  mapCmd_[CmdAudioStart]   = Cmd_AudioStart;
  mapCmd_[CmdAudioStop]    = Cmd_AudioStop;
  mapCmd_[CmdAppFocus]     = Cmd_AppFocus;
  mapCmd_[CmdVolume]       = Cmd_Volume;
  mapCmd_[CmdSqlEnable]    = Cmd_SqlEnable;
  mapCmd_[CmdSqlLevel]     = Cmd_SqlLevel;
  mapCmd_[CmdDrive]        = Cmd_Drive;
  mapCmd_[CmdTuneDrive]    = Cmd_TuneDrive;
  mapCmd_[CmdMute]         = Cmd_Mute;
  mapCmd_[CmdRxSensorsEnable] = Cmd_RxSensorsEnable;
  mapCmd_[CmdTxSensorsEnable] = Cmd_TxSensorsEnable;
  mapCmd_[CmdRxSensors]    = Cmd_RxSensors;
  mapCmd_[CmdTxSensors]    = Cmd_TxSensors;
  mapCmd_[CmdAgcMode]      = Cmd_AgcMode;
  mapCmd_[CmdAgcGain]      = Cmd_AgcGain;
  mapCmd_[CmdLock]         = Cmd_Lock;
}

void TCITransceiver::onConnected()
{
  inConnected = true;
  CAT_TRACE ("TCITransceiver entered TCI onConnected and inConnected==true\n");
}

void TCITransceiver::onDisconnected()
{
  inConnected = false;
  CAT_TRACE ("TCITransceiver entered TCI onDisonnected and inConnected==false\n");
}


void TCITransceiver::onError(QAbstractSocket::SocketError err)
{
  qDebug() << "WebInThread::onError";
  CAT_TRACE ("TCITransceiver entered TCI onError and ErrorNumber is " + QString::number(err) + '\n');
  error_ = tr ("TCI websocket error: %1").arg (errortable.at (err));
}

int TCITransceiver::do_start ()
{
  if (tci_audio_) QThread::currentThread()->setPriority(QThread::HighPriority);
  CAT_TRACE ("TCITransceiver entered TCI do_start and tci_Ready is " + QString::number(tci_Ready) + '\n');
  qDebug () << "qDebug says do_start tci_Ready is: " << tci_Ready;
  if (wrapped_) wrapped_->start (0);
  url_.setUrl("ws://" + server_); //server_
  if (url_.host() == "") url_.setHost("localhost");
  if (url_.port() == -1) url_.setPort(40001);

  if (!commander_) {
    commander_ = new QWebSocket {}; // QObject takes ownership
    CAT_TRACE ("TCITransceiver entered TCI do_start and commander created\n");
    //printf ("commander created\n");
    connect(commander_,SIGNAL(connected()),this,SLOT(onConnected()));
    connect(commander_,SIGNAL(disconnected()),this,SLOT(onDisconnected()));
    connect(commander_,SIGNAL(binaryMessageReceived(QByteArray)),this,SLOT(onBinaryReceived(QByteArray)));
    connect(commander_,SIGNAL(textMessageReceived(QString)),this,SLOT(onMessageReceived(QString)));
    connect(commander_,SIGNAL(error(QAbstractSocket::SocketError)),this,SLOT(onError(QAbstractSocket::SocketError)));
  }
  if (!tci_loop1_) {
    tci_loop1_ = new QEventLoop  {this};
  }
  if (!tci_timer1_) {
    tci_timer1_ = new QTimer {this};
    tci_timer1_ -> setSingleShot(true);
    connect( tci_timer1_, &QTimer::timeout, tci_loop1_, &QEventLoop::quit);
    connect( this, &TCITransceiver::tci_done1, tci_loop1_, &QEventLoop::quit);
  }
  if (!tci_loop2_) {
    tci_loop2_ = new QEventLoop  {this};
  }
  if (!tci_timer2_) {
    tci_timer2_ = new QTimer {this};
    tci_timer2_ -> setSingleShot(true);
    connect( tci_timer2_, &QTimer::timeout, tci_loop2_, &QEventLoop::quit);
    connect( this, &TCITransceiver::tci_done2, tci_loop2_, &QEventLoop::quit);
  }
  if (!tci_loop3_) {
    tci_loop3_ = new QEventLoop  {this};
  }
  if (!tci_timer3_) {
    tci_timer3_ = new QTimer {this};
    tci_timer3_ -> setSingleShot(true);
    connect( tci_timer3_, &QTimer::timeout, tci_loop3_, &QEventLoop::quit);
    connect( this, &TCITransceiver::tci_done3, tci_loop3_, &QEventLoop::quit);
  }
  if (!tci_loop4_) {
    tci_loop4_ = new QEventLoop  {this};
  }
  if (!tci_timer4_) {
    tci_timer4_ = new QTimer {this};
    tci_timer4_ -> setSingleShot(true);
    connect( tci_timer4_, &QTimer::timeout, tci_loop4_, &QEventLoop::quit);
    connect( this, &TCITransceiver::tci_done4, tci_loop4_, &QEventLoop::quit);
  }
  if (!tci_loop5_) {
    tci_loop5_ = new QEventLoop  {this};
  }
  if (!tci_timer5_) {
    tci_timer5_ = new QTimer {this};
    tci_timer5_ -> setSingleShot(true);
    connect( tci_timer5_, &QTimer::timeout, tci_loop5_, &QEventLoop::quit);
    connect( this, &TCITransceiver::tci_done5, tci_loop5_, &QEventLoop::quit);
  }
  if (!tci_loop6_) {
    tci_loop6_ = new QEventLoop  {this};
  }
  if (!tci_timer6_) {
    tci_timer6_ = new QTimer {this};
    tci_timer6_ -> setSingleShot(true);
    connect( tci_timer6_, &QTimer::timeout, tci_loop6_, &QEventLoop::quit);
    connect( this, &TCITransceiver::tci_done6, tci_loop6_, &QEventLoop::quit);
  }
  if (!tci_loop7_) {
      tci_loop7_ = new QEventLoop  {this};
  }
  if (!tci_timer7_) {
      tci_timer7_ = new QTimer {this};
      tci_timer7_ -> setSingleShot(true);
      connect( tci_timer7_, &QTimer::timeout, tci_loop7_, &QEventLoop::quit);
      connect( this, &TCITransceiver::tci_done7, tci_loop7_, &QEventLoop::quit);
  }
  if (!tci_loop8_) {
      tci_loop8_ = new QEventLoop  {this};
  }
  if (!tci_timer8_) {
      tci_timer8_ = new QTimer {this};
      tci_timer8_ -> setSingleShot(true);
      connect( tci_timer8_, &QTimer::timeout, tci_loop8_, &QEventLoop::quit);
      connect( this, &TCITransceiver::tci_done8, tci_loop8_, &QEventLoop::quit);
  }

  tx_fifo = 0; tx_top_ = true;
  tci_Ready = false;
  ESDR3 = false;
  HPSDR = false;
  band_change = false;
  trxA = 0;
  trxB = 0;
  busy_rx_frequency_ = false;
  busy_mode_ = false;
  busy_other_frequency_ = false;
  busy_split_ = false;
  busy_drive_ = false;
  busy_PTT_ = false;
  busy_rx2_ = false;
  rx2_ = false;
  requested_rx2_ = false;
  started_rx2_ = false;
  split_ = false;
  requested_split_ = false;
  started_split_ = false;
  PTT_ = false;
  requested_PTT_ = false;
  mode_ = "";
  requested_mode_ = "";
  started_mode_ = "";
  requested_rx_frequency_ = "";
  rx_frequency_ = "";
  requested_other_frequency_ = "";
  other_frequency_ = "";
  requested_drive_ = "";
  drive_ = "";
  level_ = -54;
  txAtten = 45;
  power_ = 0;
  swr_ = 0;
  m_bufferPos = 0;
  m_downSampleFactor =4;
  m_ns = 999;
  audio_ = false;
  requested_stream_audio_ = false;
  stream_audio_ = false;
  _power_ = false;
  CAT_TRACE ("TCITransceiver entered TCI do_start and url " + url_.toString() + " rig_power:" + QString::number(rig_power_) + " rig_power_off:" + QString::number(rig_power_off_) + " tci_audio:" + QString::number(tci_audio_) + " do_snr:" + QString::number(do_snr_) + " do_pwr:" + QString::number(do_pwr_) + '\n');
  commander_->open (url_);
  tci_done6();
  mysleep6(2000);
  busy_split_ = true;
  const QString cmd = CmdSplitEnable + SmDP + "false" + SmTZ;
  sendTextMessage(cmd);
  //mysleep6(500);
  busy_split_ = false;
  if (error_.isEmpty()) {
    tci_Ready = true;
    if (!_power_) {
      if (rig_power_) {
        rig_power(true);
        mysleep6(1000);
        if(!_power_) throw error {tr ("TCI SDR could not be switched on")};
      } else {
        tci_Ready = false;
        throw error {tr ("TCI SDR is not switched on")};
      }
    }
    if (rx_ == "1" && !rx2_) {
      rx2_enable (true);
      if (!rx2_) {
        tci_Ready = false;
        throw error {tr ("TCI RX2 could not be enabled")};
      }
    }
    if (tci_audio_) {
      stream_audio (true);
      mysleep6(500);
      if (!stream_audio_) {
        tci_Ready = false;
        throw error {tr ("TCI Audio could not be switched on")};
      }
    }
    if (ESDR3) {
      const QString cmd = CmdRxSensorsEnable + SmDP + (do_snr_ ? "true" : "false") + SmCM + "500" +  SmTZ;
      sendTextMessage(cmd);
    } else if (do_snr_) {
      const QString cmd = CmdSmeter + SmDP + rx_ + SmCM + "0" +  SmTZ;
      sendTextMessage(cmd);
    }
//    if (!requested_rx_frequency_.isEmpty()) do_frequency(string_to_frequency (requested_rx_frequency_),get_mode(true),false);
//    if (!requested_other_frequency_.isEmpty()) do_tx_frequency(string_to_frequency (requested_other_frequency_),get_mode(true),false);
//    else if (requested_split_ != split_) {rig_split();}

    do_poll ();
    if (ESDR3) {
      const QString cmd = CmdTxSensorsEnable + SmDP + (do_pwr_ ? "true" : "false") + SmCM + "500" +  SmTZ;
      sendTextMessage(cmd);
    }
    if (stream_audio_) do_audio(true);

    CAT_TRACE ("TCITransceiver started\n");
    return 0;
  } else {
    CAT_TRACE("TCITransceiver not started with error " + error_ + '\n');

    if (error_.isEmpty())
      throw error {tr ("TCI could not be opened")};
    else
      throw error {error_};
  }
}

void TCITransceiver::do_stop ()
{
  CAT_TRACE ("TCITransceiver TCI close\n");
  //printf ("TCI close\n");
  if (!commander_) return;
  if (stream_audio_ && tci_Ready && inConnected && _power_) {
    stream_audio (false);
    mysleep1(500);
    CAT_TRACE ("TCI audio closed\n");
    //printf ("TCI audio closed\n");
  }
  if (tci_Ready && inConnected && _power_) {
    requested_other_frequency_ = "";
    busy_split_ = true;
    const QString cmd = CmdSplitEnable + SmDP + "false" + SmTZ;
    sendTextMessage(cmd);
    mysleep1(500);
    busy_split_ = false;
    requested_split_ = false;
    rig_split();
    if (started_mode_ != mode_) sendTextMessage(mode_to_command(started_mode_));
    if (!started_rx2_ && rx2_) {
      rx2_enable (false);
    }
  }
  if (_power_ && rig_power_off_ && tci_Ready && inConnected) {
    rig_power(false);
    mysleep1(500);
    CAT_TRACE ("TCI power down\n");
    //printf ("TCI power down\n");
  }
  tci_Ready = false;
  if (commander_)
  {
    if (inConnected) commander_->close(QWebSocketProtocol::CloseCodeNormal,"end");
    delete commander_, commander_ = nullptr;
    CAT_TRACE ("deleted commander & closed websocket & deleted:");
  }
  if (tci_timer1_)
  {
    if (tci_timer1_->isActive()) tci_timer1_->stop();
    // tci_timer1_->deleteLater(), tci_timer1_ = nullptr;
    CAT_TRACE ("timer1 ");
  }
  if (tci_loop1_)
  {
    tci_loop1_->quit();
    //  tci_loop1_->deleteLater(), tci_loop1_ = nullptr;
    CAT_TRACE ("loop1 ");
  }
  if (tci_timer2_)
  {
    if (tci_timer2_->isActive()) tci_timer2_->stop();
    //  tci_timer2_->deleteLater(), tci_timer2_ = nullptr;
    CAT_TRACE ("timer2 ");
  }
  if (tci_loop2_)
  {
    tci_loop2_->quit();
   //   tci_loop2_->deleteLater(), tci_loop2_ = nullptr;
    CAT_TRACE ("loop2 ");
  }
  if (tci_timer3_)
  {
    if (tci_timer3_->isActive()) tci_timer3_->stop();
    //  tci_timer3_->deleteLater(), tci_timer3_ = nullptr;
    CAT_TRACE ("timer3 ");
  }
  if (tci_loop3_)
  {
    tci_loop3_->quit();
    //  tci_loop3_->deleteLater(), tci_loop3_ = nullptr;
    CAT_TRACE ("loop3 ");
  }
  if (tci_timer4_)
  {
    if (tci_timer4_->isActive()) tci_timer4_->stop();
   // tci_timer4_->deleteLater(), tci_timer4_ = nullptr;
    CAT_TRACE ("timer4 ");
  }
  if (tci_loop4_)
  {
    tci_loop4_->quit();
   // tci_loop4_->deleteLater(), tci_loop4_ = nullptr;
    CAT_TRACE ("loop4 ");
  }
  if (tci_timer5_)
  {
    if (tci_timer5_->isActive()) tci_timer5_->stop();
   // tci_timer5_->deleteLater(), tci_timer5_ = nullptr;
    CAT_TRACE ("timer5 ");
  }
  if (tci_loop5_)
  {
    tci_loop5_->quit();
   // tci_loop5_->deleteLater(), tci_loop5_ = nullptr;
    CAT_TRACE ("loop5 ");
  }
  if (tci_timer6_)
  {
    if (tci_timer6_->isActive()) tci_timer6_->stop();
  //  tci_timer6_->deleteLater(), tci_timer6_ = nullptr;
    CAT_TRACE ("timer6 ");
  }
  if (tci_loop6_)
  {
    tci_loop6_->quit();
  //  tci_loop6_->deleteLater(), tci_loop6_ = nullptr;
    CAT_TRACE ("loop6 ");
  }
  if (tci_timer7_)
  {
      if (tci_timer7_->isActive()) tci_timer7_->stop();
    //  tci_timer7_->deleteLater(), tci_timer7_ = nullptr;
      CAT_TRACE ("timer7 ");
  }
  if (tci_loop7_)
  {
      tci_loop7_->quit();
    //  tci_loop7_->deleteLater(), tci_loop7_ = nullptr;
      CAT_TRACE ("loop7 ");
  }
  if (tci_timer8_)
  {
      if (tci_timer8_->isActive()) tci_timer8_->stop();
   //   tci_timer8_->deleteLater(), tci_timer8_ = nullptr;
      CAT_TRACE ("timer8 ");
  }
  if (tci_loop8_)
  {
      tci_loop8_->quit();
   //   tci_loop8_->deleteLater(), tci_loop8_ = nullptr;
      CAT_TRACE ("loop8 ");
  }

  if (wrapped_) wrapped_->stop ();
  CAT_TRACE ("& closed TCITransceiver\n");
}

void TCITransceiver::onMessageReceived(const QString &str)
{
  qDebug() << "From WEB" << str;
  QStringList cmd_list = str.split(";", SkipEmptyParts);
  for (QString cmds : cmd_list){
    QStringList cmd = cmds.split(":", SkipEmptyParts);
    QStringList args = cmd.last().split(",", SkipEmptyParts);
    Tci_Cmd idCmd = mapCmd_[cmd.first()];
    if (idCmd != Cmd_Power && idCmd != Cmd_SWR && idCmd != Cmd_Smeter && idCmd != Cmd_AppFocus && idCmd != Cmd_RxSensors && idCmd != Cmd_TxSensors) { printf("%s TCI message received:|%s| ",QDateTime::QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),str.toStdString().c_str()); printf("idCmd : %d args : %s\n",idCmd,args.join("|").toStdString().c_str());}
    qDebug() << cmds << idCmd;
    if (idCmd <=0) continue;

    switch (idCmd) {
      case Cmd_Smeter:
        if(args.at(0)==rx_ && args.at(1) == "0") level_ = args.at(2).toInt() + 73;
        break;
      case Cmd_RxSensors:
        if(args.at(0)==rx_) level_ = args.at(1).split(".")[0].toInt() + 73;
        printf("Smeter=%d\n",level_);
        break;
      case Cmd_TxSensors:
        if(args.at(0)==rx_) {
          power_ = 10 * args.at(3).split(".")[0].toInt() + args.at(3).split(".")[1].toInt();
          swr_ = 10 * args.at(4).split(".")[0].toInt() + args.at(4).split(".")[1].toInt();
          printf("Power=%d SWR=%d\n",power_,swr_);
        }
        break;
      case Cmd_SWR:
        printf("%s Cmd_SWR : %s\n",QDateTime::QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),args.join("|").toStdString().c_str());
        swr_ = 10 * args.at(0).split(".")[0].toInt() + args.at(0).split(".")[1].toInt();
        break;
      case Cmd_Power:
        power_ = 10 * args.at(0).split(".")[0].toInt() + args.at(0).split(".")[1].toInt();
        printf("%s Cmd_Power : %s %d\n",QDateTime::QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),args.join("|").toStdString().c_str(),power_);
        break;
      case Cmd_VFO:
        printf("%s Cmd_VFO : %s\n",QDateTime::QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),args.join("|").toStdString().c_str());
        printf("band_change:%d busy_other_frequency_:%d timer1_remaining:%d timer2_remaining:%d",band_change,busy_other_frequency_,tci_timer7_->remainingTime(),tci_timer2_->remainingTime()); //was timer1 and timer2
        if(args.at(0)==rx_ && args.at(1) == "0") {
          if (args.at(2).left(1) != "-") rx_frequency_ = args.at(2);
          CAT_TRACE("Rx VFO Frequency from SDR is :");
          CAT_TRACE(rx_frequency_);
          if (!tci_Ready && requested_rx_frequency_.isEmpty()) requested_rx_frequency_ = rx_frequency_;
          if (busy_rx_frequency_ && !band_change) {
            printf (" cmdvfo0 done1");
            tci_done7(); //was tci_done1 (do_frequency)
          } else if (!tci_timer2_->isActive() && split_) {
            printf (" cmdvfo0 timer2 start 210");
            tci_timer2_->start(210);
          }
        }
        else if (args.at(0)==rx_ && args.at(1) == "1") {
          if (args.at(2).left(1) != "-") other_frequency_ = args.at(2);
          CAT_TRACE("Tx VFO (other) Frequency from SDR is :");
          CAT_TRACE(other_frequency_);
          if (!tci_Ready && requested_other_frequency_.isEmpty()) requested_other_frequency_ = other_frequency_;
          if (band_change && tci_timer7_->isActive()) { //was tci_timer1
            printf (" cmdvfo1 done1");
            band_change = false;
            tci_timer2_->start(210);
            tci_done7(); //was tci_done1 (do_frequency)
          } else if (busy_other_frequency_) {
            printf (" cmdvfo1 done2");
            tci_done2();
          } else if (tci_timer2_->isActive()) {
            printf (" cmdvfo1 timer2 reset 210");
            tci_timer2_->start(210);
          } else if (other_frequency_ != requested_other_frequency_ && tci_Ready && split_ && !tci_timer2_->isActive()) {
            printf (" cmdvfo1 timer2 start 210");
            tci_timer2_->start(210);
          }
        }
        break;
      case Cmd_Mode:
        printf("%s Cmd_Mode : %s\n",QDateTime::QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),args.join("|").toStdString().c_str());
        if(args.at(0)==rx_) {
          if (ESDR3 || HPSDR) {
            if (args.at(1) == "0" ) mode_ = args.at(2).toLower(); else mode_ = args.at(1).toLower();
          } else mode_ = args.at(1);
          if (started_mode_.isEmpty()) started_mode_ = mode_;
          if (busy_mode_) return; // was tci_done1();
          else if (!requested_mode_.isEmpty() && requested_mode_ != mode_ && !band_change) {
            sendTextMessage(mode_to_command(requested_mode_));
          }
        }
        break;
      case Cmd_SplitEnable:
        printf("%s Cmd_SplitEnable : %s\n",QDateTime::QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),args.join("|").toStdString().c_str());
        if(args.at(0)==rx_) {
          if (args.at(1) == "false") split_ = false;
          else if (args.at(1) == "true") split_ = true;
          if (!tci_Ready) {started_split_ = split_;}
          else if (busy_split_) tci_done5();  //was tci_done2
          else if (requested_split_ != split_ && !tci_timer5_->isActive()) { //was tci_timer2
              CAT_TRACE("tci_timer5 started in onMessageReceived-Cmd_SplitEnable");
            tci_timer5_->start(210);  //was tci_timer2
            rig_split();
          }
        }
        break;
      case Cmd_Drive:
        printf("%s Cmd_Drive : %s\n",QDateTime::QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),args.join("|").toStdString().c_str());
        if((!ESDR3 && !HPSDR) || args.at(0)==rx_) {
          if (ESDR3 || HPSDR) drive_ = args.at(1); else drive_ = args.at(0);
          if (requested_drive_.isEmpty()) requested_drive_ = drive_;
          busy_drive_ = false;
        }
        break;
      case Cmd_Volume:
        break;
      case Cmd_Trx:
        printf("%s Cmd_Trx : %s\n",QDateTime::QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),args.join("|").toStdString().c_str());
        if(args.at(0)==rx_) {
          if (args.at(1) == "false") PTT_ = false;
          else if (args.at(1) == "true") PTT_ = true;
          if (tci_Ready && requested_PTT_ == PTT_) tci_done3();
          else if (tci_Ready && !PTT_) {
            requested_PTT_ = PTT_;
            update_PTT(PTT_);
            power_ = 0; if (do_pwr_) update_power (0);
            swr_ = 0; if (do_pwr_) update_swr (0);
          }
        }
        break;
      case Cmd_AudioStart:
        printf("%s Cmd_AudioStart : %s\n",QDateTime::QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),args.join("|").toStdString().c_str());
        if(args.at(0)==rx_) {
          stream_audio_ = true;
          if (tci_Ready) {
            printf ("cmdaudiostart done1\n");
            tci_done7(); //was tci_done1 (do_frequency)
          }
        }
        break;
      case Cmd_RxEnable:
        printf("%s Cmd_RxEnable : %s\n",QDateTime::QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),args.join("|").toStdString().c_str());
        if(args.at(0)=="1") {
          if (args.at(1) == "false") rx2_ = false;
          else if (args.at(1) == "true") rx2_ = true;
          if(!tci_Ready) {requested_rx2_ = rx2_; started_rx2_ = rx2_;}
          else if (tci_Ready && busy_rx2_ && requested_rx2_ == rx2_) {
            tci_done4();
          }
        }
        break;
      case Cmd_AudioStop:
        printf("%s CmdAudioStop : %s\n",QDateTime::QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),args.join("|").toStdString().c_str());
        if(args.at(0)==rx_) {
          stream_audio_ = false;
          if (tci_Ready) {
            printf ("cmdaudiostop done1\n");
          }
        }
        break;
      case Cmd_Start:
        printf("%s CmdStart : %s\n",QDateTime::QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),args.join("|").toStdString().c_str());
        _power_ = true;
        printf ("cmdstart done1\n");
        if (tci_Ready) {
          tci_done7(); //was tci_done1 (do_frequency)
        }
        break;
      case Cmd_Stop:
        printf("%s CmdStop : %s\n",QDateTime::QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),args.join("|").toStdString().c_str());
        if (tci_Ready && PTT_) {
          PTT_ = false;
          requested_PTT_ = PTT_;
          update_PTT(PTT_);
          power_ = 0; if (do_pwr_) update_power (0);
          swr_ = 0; if (do_pwr_) update_swr (0);
          m_state = Idle;
          Q_EMIT tci_mod_active(m_state != Idle);
        }
        _power_ = false;
        if (tci_timer1_->isActive()) {  //was tci_timer1
          printf ("cmdstop done1\n");
          tci_done1();
        } else {
          tci_Ready = false;
        }
        break;
      case Cmd_Version:
        printf("%s CmdVersion : %s\n",QDateTime::QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),args.join("|").toStdString().c_str());
        if(args.at(0)=="ExpertSDR3") ESDR3 = true;
        else if (args.at(0)=="Thetis") HPSDR = true;
        break;
      case Cmd_Device:
        printf("%s CmdDevice : %s\n",QDateTime::QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),args.join("|").toStdString().c_str());
        if((args.at(0)=="SunSDR2DX" || args.at(0)=="SunSDR2PRO") && !ESDR3) tx_top_ = false;
        printf ("tx_top_:%d\n",tx_top_);
        break;
      case Cmd_Ready:
        printf("%s CmdReady : %s\n",QDateTime::QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),args.join("|").toStdString().c_str());
        tci_done7(); //was tci_done1 (do_frequency)
        break;

      default:
        break;
    }
  }
}

void TCITransceiver::sendTextMessage(const QString &message)
{
  if (inConnected) {
    commander_->sendTextMessage(message);
    CAT_TRACE ("TCI Command Sent: " + message + '\n');
  }
}

void TCITransceiver::onBinaryReceived(const QByteArray &data)
{
  Data_Stream *pStream = (Data_Stream*)(data.data());
  if (pStream->type != last_type) {
    last_type = pStream->type;
  }
  if (pStream->type == Iq_Stream){
    bool tx = false;
    if (pStream->receiver == 0){
      tx = trxA == 0;
      trxA = 1;
    }
    if (pStream->receiver == 1) {
      tx = trxB == 0;
      trxB = 1;
    }
    printf("sendIqData\n");
    emit sendIqData(pStream->receiver,pStream->length,pStream->data,tx);
    qDebug() << "IQ" << data.size() << pStream->length;
  } else if (pStream->type == RxAudioStream && audio_  && pStream->receiver == rx_.toUInt()) {
    writeAudioData(pStream->data,pStream->length);
  } else if (pStream->type == TxChrono &&  pStream->receiver == rx_.toUInt()) {
    mtx_.lock(); tx_fifo += 1; tx_fifo &= 7;
    int ssize = AudioHeaderSize+pStream->length*sizeof(float)*2;
    quint16 tehtud;
    if (m_tx1[tx_fifo].size() != ssize) m_tx1[tx_fifo].resize(ssize);
    Data_Stream * pOStream1 = (Data_Stream*)(m_tx1[tx_fifo].data());
    pOStream1->receiver = pStream->receiver;
    pOStream1->sampleRate = pStream->sampleRate;
    pOStream1->format = pStream->format;
    pOStream1->codec = 0;
    pOStream1->crc = 0;
    pOStream1->length = pStream->length;
    pOStream1->type = TxAudioStream;
    tehtud = readAudioData(pOStream1->data,pOStream1->length, txAtten);
    if (tehtud && tehtud != pOStream1->length) {
      quint16 valmis = tehtud;
      tehtud = readAudioData(pOStream1->data + valmis,pOStream1->length - valmis,txAtten);
    }
    tx_fifo2 = tx_fifo; mtx_.unlock();
    if (!inConnected || commander_->sendBinaryMessage(m_tx1[tx_fifo2]) != m_tx1[tx_fifo2].size()) {
    }
  }
}

void TCITransceiver::txAudioData(quint32 len, float * data)
{
  QByteArray tx;
  tx.resize(AudioHeaderSize+len*sizeof(float)*2);
  Data_Stream * pStream = (Data_Stream*)(tx.data());
  pStream->receiver = 0;
  pStream->sampleRate = audioSampleRate;
  pStream->format = 3;
  pStream->codec = 0;
  pStream->crc = 0;
  pStream->length = len;
  pStream->type = TxAudioStream;
  memcpy(pStream->data,data,len*sizeof(float)*2);
  commander_->sendBinaryMessage(tx);
}

quint32 TCITransceiver::writeAudioData (float * data, qint32 maxSize)
{
  static unsigned mstr0=999999;
  qint64 ms0 = QDateTime::currentMSecsSinceEpoch() % 86400000;
  unsigned mstr = ms0 % int(1000.0*m_period); // ms into the nominal Tx start time
  if(mstr < mstr0/2) {              //When mstr has wrapped around to 0, restart the buffer
    dec_data.params.kin = 0;
    m_bufferPos = 0;
  }
  mstr0=mstr;

  if(data != NULL) {
  // block below adjusts receive audio attenuation
  quint64 i = 0;
  float * data1 = (float*)malloc(maxSize * sizeof(float));
  for(i=0;i<(quint64)maxSize;i++) {
    data1[i] = ((float)(pow(10, 0.05*rxAtten) * data[i]));
  }

  // no torn frames
  Q_ASSERT (!(maxSize % static_cast<qint32> (bytesPerFrame)));

  // these are in terms of input frames (not down sampled)
  size_t framesAcceptable ((sizeof (dec_data.d2) /
                               sizeof (dec_data.d2[0]) - dec_data.params.kin) * m_downSampleFactor);
  size_t framesAccepted (qMin (static_cast<size_t> (maxSize /
                                                    bytesPerFrame), framesAcceptable));

  if (framesAccepted < static_cast<size_t> (maxSize / bytesPerFrame)) {
    qDebug () << "dropped " << maxSize / bytesPerFrame - framesAccepted
             << " frames of data on the floor!"
             << dec_data.params.kin << mstr;
  }

  for (unsigned remaining = framesAccepted; remaining; ) {
    size_t numFramesProcessed (qMin (m_samplesPerFFT *
                                     m_downSampleFactor - m_bufferPos, remaining));

    if(m_downSampleFactor > 1) {
      store (&data1[(framesAccepted - remaining) * bytesPerFrame],
            numFramesProcessed, &m_buffer[m_bufferPos]);
      m_bufferPos += numFramesProcessed;

      if(m_bufferPos==m_samplesPerFFT*m_downSampleFactor) {
        qint32 framesToProcess (m_samplesPerFFT * m_downSampleFactor);
        qint32 framesAfterDownSample (m_samplesPerFFT);
        if(m_downSampleFactor > 1 && dec_data.params.kin>=0 &&
            dec_data.params.kin < (NTMAX*12000 - framesAfterDownSample)) {
          fil4_(&m_buffer[0], &framesToProcess, &dec_data.d2[dec_data.params.kin],
                &framesAfterDownSample, &dec_data.d2[dec_data.params.kin]);
          dec_data.params.kin += framesAfterDownSample;
        } else {
          qDebug() << "framesToProcess     = " << framesToProcess;
          qDebug() << "dec_data.params.kin = " << dec_data.params.kin;
          qDebug() << "framesAfterDownSample" << framesAfterDownSample;
        }
        Q_EMIT tciframeswritten (dec_data.params.kin);
        m_bufferPos = 0;
      }

    } else {
      store (&data1[(framesAccepted - remaining) * bytesPerFrame],
            numFramesProcessed, &dec_data.d2[dec_data.params.kin]);
      m_bufferPos += numFramesProcessed;
      dec_data.params.kin += numFramesProcessed;
      if (m_bufferPos == static_cast<unsigned> (m_samplesPerFFT)) {
        Q_EMIT tciframeswritten (dec_data.params.kin);
        m_bufferPos = 0;
      }
    }
    remaining -= numFramesProcessed;
  }

  free(data1);
}
  return maxSize;    // we drop any data past the end of the buffer on
  // the floor until the next period starts
}

void TCITransceiver::rx2_enable (bool on)
{
  printf("%s TCI rx2_enable:%d->%d busy:%d\n",QDateTime::QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),rx2_,on,busy_rx2_);
  if (busy_rx2_) return;
  requested_rx2_ = on;
  busy_rx2_ = true;
  const QString cmd = CmdRxEnable + SmDP + "1" + SmCM + (requested_rx2_ ? "true" : "false") + SmTZ;
  sendTextMessage(cmd);
  mysleep4(1000);
  busy_rx2_ = false;
}

void TCITransceiver::rig_split ()
{
  printf("%s TCI rig_split:%d->%d busy:%d\n",QDateTime::QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),split_,requested_split_,busy_split_);
  if (busy_split_) return;
  if (tci_timer5_->isActive()) mysleep5(0);
  busy_split_ = true;
  const QString cmd = CmdSplitEnable + SmDP + rx_ + SmCM +  "false" + SmTZ;  // changed from below so split always false
  //const QString cmd = CmdSplitEnable + SmDP + rx_ + SmCM + (requested_split_ ? "true" : "false") + SmTZ;
  sendTextMessage(cmd);
  mysleep5(500);
  busy_split_ = false;
  if (requested_split_ == split_) update_split (split_);
}

void TCITransceiver::rig_power (bool on)
{
  TRACE_CAT ("TCITransceiver", on << state ());
  printf("%s TCI rig_power:%d _power_:%d\n",QDateTime::QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),on,_power_);
  if (on != _power_) {
    if (on) {
      const QString cmd = CmdStart + SmTZ;
      sendTextMessage(cmd);
    } else {
      const QString cmd = CmdStop + SmTZ;
      sendTextMessage(cmd);
    }
  }
}

void TCITransceiver::stream_audio (bool on)
{
  TRACE_CAT ("TCITransceiver", on << state ());
  if (on != stream_audio_ && tci_Ready) {
    requested_stream_audio_ = on;
    if (on) {
      const QString cmd = CmdAudioStart + SmDP + rx_ + SmTZ;
      sendTextMessage(cmd);
    } else {
      const QString cmd = CmdAudioStop + SmDP + rx_ + SmTZ;
      sendTextMessage(cmd);
    }
  }
}

void TCITransceiver::do_audio (bool on)
{
  TRACE_CAT ("TCITransceiver", on << state ());
  if (on) {
    dec_data.params.kin = 0;
    m_bufferPos = 0;
  }
  audio_ = on;
}

void TCITransceiver::do_period (double period)
{
  TRACE_CAT ("TCITransceiver", period << state ());
  m_period = period;
}

void TCITransceiver::do_volume (qreal volume)
{
  rxAtten = volume;
}


void TCITransceiver::do_txvolume (qreal txvolume)
{
  txAtten = txvolume;
}

void TCITransceiver::do_blocksize (qint32 blocksize)
{
  TRACE_CAT ("TCITransceiver", blocksize << state ());
  m_samplesPerFFT = blocksize;
}

void TCITransceiver::do_ptt (bool on)
{
  TRACE_CAT ("TCITransceiver", on << state ());
  if (use_for_ptt_) {
    if (on != PTT_) {
      if (!inConnected && !error_.isEmpty()) throw error {error_};
      else if (busy_PTT_ || !tci_Ready || !_power_) return;
      else busy_PTT_ = true;
      requested_PTT_ = on;
      if (ESDR3) {
        const QString cmd = CmdTrx + SmDP + rx_ + SmCM + (on ? "true" : "false") + SmCM + "tci"+ SmTZ;
        sendTextMessage(cmd);
      } else {
        const QString cmd = CmdTrx + SmDP + rx_ + SmCM + (on ? "true" : "false") + SmTZ;
        sendTextMessage(cmd);
      }
      mysleep3(1000);
      busy_PTT_ = false;
      if (requested_PTT_ == PTT_) {
        update_PTT(PTT_);
        if (PTT_ && do_snr_) {
          update_level (-54);
        } else {
          power_ = 0;
          if (do_pwr_) update_power (0);
          swr_ = 0;
          if (do_pwr_) update_swr (0);
        }
      } else {
        printf("%s TCI failed set ptt %d->%d\n",QDateTime::QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),PTT_,requested_PTT_);
        error_ = tr ("TCI failed to set ptt");
        tci_Ready = false;
        throw error {tr ("TCI failed to set ptt")};
      }
    } else update_PTT(on);
  }
  else
  {
    TRACE_CAT ("TCITransceiver", "TCI should use PTT via CAT");
    throw error {tr ("TCI should use PTT via CAT")};
  }
}

void TCITransceiver::do_frequency (Frequency f, MODE m, bool no_ignore)
{
  qDebug() << no_ignore;
  TRACE_CAT ("TCITransceiver", f << state ());
  auto f_string = frequency_to_string (f);
  if (busy_rx_frequency_) {
    return;
  } else {
    requested_rx_frequency_ = f_string;
    requested_mode_ = map_mode (m);
  }
  if (tci_Ready && _power_) {
    if ((rx_frequency_ != requested_rx_frequency_)) {
      busy_rx_frequency_ = true;
      band_change = abs(rx_frequency_.toInt()-requested_rx_frequency_.toInt()) > 1000000;
      const QString cmd = CmdVFO + SmDP + rx_ + SmCM + "0" + SmCM + requested_rx_frequency_ + SmTZ;
      if(f > 100000 && f< 250000000000) sendTextMessage(cmd);
      mysleep7(2000);
      // if (band_change) mysleep7(500);
      band_change2 = abs(rx_frequency_.toInt()-requested_rx_frequency_.toInt()) > 1000000;
      if (!band_change2) update_rx_frequency (f);
      //if (requested_rx_frequency_ == rx_frequency_) update_rx_frequency (f);
      else {
        printf("%s TCI failed set rxfreq:%s->%s\n",QDateTime::QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),rx_frequency_.toStdString().c_str(),requested_rx_frequency_.toStdString().c_str());
        error_ = tr ("TCI failed set rxfreq");
        CAT_TRACE("TCI failed set rxfreq.  Rx freq is:");
        CAT_TRACE(rx_frequency_);
        CAT_TRACE("TCI failed set rxfreq.  Requested Rx freq is:");
        CAT_TRACE(requested_rx_frequency_);
        printf("rx_frequency is %s and requested_rx_frequency is %s\n",rx_frequency_.toStdString().c_str(),requested_rx_frequency_.toStdString().c_str());
        tci_Ready = false;
        throw error {tr ("TCI failed set rxfreq")};
      }
      busy_rx_frequency_ = false;
    } else update_rx_frequency (string_to_frequency (rx_frequency_));

    if (!requested_mode_.isEmpty() && requested_mode_ != mode_ && !busy_mode_) {
      busy_mode_ = true;
      sendTextMessage(mode_to_command(requested_mode_));
      mysleep7(1000);
      if (requested_mode_.isEmpty() || requested_mode_ == mode_) update_mode (m);
      else {
        printf("%s TCI failed set mode %s->%s",QDateTime::QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),mode_.toStdString().c_str(),requested_mode_.toStdString().c_str());
        error_ = tr ("TCI failed set mode");
        //        tci_Ready = false;
        //        throw error {tr ("TCI failed set mode")};
      }
      busy_mode_ = false;
    }
  } else {
    update_rx_frequency (f);
    update_mode (m);
  }
}

void TCITransceiver::do_tx_frequency (Frequency tx, MODE mode, bool no_ignore)
{
  TRACE_CAT ("%s TCITransceiver", tx << state ());
  auto f_string = frequency_to_string (tx);
  if (tci_Ready && busy_other_frequency_ && no_ignore) {
      CAT_TRACE("TCI do_txfrequency critical no_ignore set tx vfo will be missed\n");
    printf("%s TCI do_txfrequency critical no_ignore set tx vfo will be missed\n",QDateTime::QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str());
  }
  if (busy_other_frequency_) return;
  requested_other_frequency_ = f_string;
  requested_mode_ = map_mode (mode);
  if (tx) {
    requested_split_ = true;
    if (tci_Ready && _power_) {
      if (band_change && !tci_timer2_->isActive()) {
        if (!HPSDR) {
          mysleep2(210);
        } else {
          mysleep2(2000);
        }
      } else {
        if (tci_timer2_->isActive()) mysleep2(0);
      }
      if (requested_split_ != split_) rig_split();
      else update_split (split_);
      if (other_frequency_ != requested_other_frequency_) {
        busy_other_frequency_ = true;
        printf("%s TCI VFO1 command sent\n",QDateTime::QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str());
        const QString cmd = CmdVFO + SmDP + rx_ + SmCM + "1" + SmCM + requested_other_frequency_ + SmTZ;
        if(tx > 100000 && tx < 250000000000) sendTextMessage(cmd);
        mysleep2(1000);
        other_band_change = abs(other_frequency_.toInt()-requested_other_frequency_.toInt()) > 1000000;
        if (!other_band_change) update_other_frequency (tx);
       // if (requested_other_frequency_ == other_frequency_) update_other_frequency (tx);
        else {
          printf("%s TCI failed set txfreq:%s->%s\n",QDateTime::QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),other_frequency_.toStdString().c_str(),requested_other_frequency_.toStdString().c_str());
          CAT_TRACE("TCI failed set txfreq");
          //            error_ = tr ("TCI failed set txfreq");
          //            tci_Ready = false;
          //            throw error {tr ("TCI failed set txfreq")};
        }
        busy_other_frequency_ = false;
      } else update_other_frequency (string_to_frequency (other_frequency_));

    } else {
      update_split (requested_split_);
      update_other_frequency (tx);
    }
  }
  else {
    requested_split_ = false;
    requested_other_frequency_ = "";
    if (tci_Ready && _power_) {
      if (band_change) {
        mysleep2(2000);
        if (!HPSDR) mysleep2(200);
      }
      if (tci_timer2_->isActive()) mysleep2(0);
      if (requested_split_ != split_) rig_split();
      else update_split (split_);
      update_other_frequency (tx);
    } else {
      update_split (requested_split_);
      update_other_frequency (tx);
    }
  }
}

//do_mode is never called.
void TCITransceiver::do_mode (MODE m)
{
  TRACE_CAT ("TCITransceiver", m << state ());
  auto m_string = map_mode (m);
  if (requested_mode_ != m_string) requested_mode_ = m_string;
  if (!requested_mode_.isEmpty() && mode_ != requested_mode_ && !busy_mode_) {
    busy_mode_ = true;
    sendTextMessage(mode_to_command(requested_mode_));
        mysleep8(1000);
    if (requested_mode_ == mode_) update_mode (m);
    else {
      printf("%s TCI failed set mode %s->%s",QDateTime::QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),mode_.toStdString().c_str(),requested_mode_.toStdString().c_str());
      error_ = tr ("TCI failed set mode");
      //    tci_Ready = false;
      //    throw error {tr ("TCI failed set mode")};
    }
    busy_mode_ = false;
  }
}

void TCITransceiver::do_poll ()
{
  if (!error_.isEmpty()) {tci_Ready = false; throw error {error_};}
  else if (!tci_Ready) throw error {tr ("TCI could not be opened")};
  update_rx_frequency (string_to_frequency (rx_frequency_));
  update_split(split_);
  if (state ().split ()) {
    if (other_frequency_ == requested_other_frequency_) update_other_frequency (string_to_frequency (other_frequency_));
    else if (!busy_rx_frequency_) do_tx_frequency(string_to_frequency (requested_other_frequency_),get_mode(true),false);
  }
  update_mode (get_mode());
  if (do_pwr_ && PTT_) {update_power (power_ * 100); update_swr (swr_*10);}
  if (do_snr_ && !PTT_) {
    update_level (level_);
    if(!ESDR3) {
      const QString cmd = CmdSmeter + SmDP + rx_ + SmCM + "0" +  SmTZ;
      sendTextMessage(cmd);
    }
  }
}

auto TCITransceiver::get_mode (bool requested) -> MODE
{
  MODE m {UNK};
  if (requested) {
    if ("am" == requested_mode_)
    {
      m = AM;
    }
    else if ("cw" == requested_mode_)
    {
      m = CW;
    }
    else if ("wfm" == requested_mode_)
    {
      m = FM;
    }
    else if ("nfm" == requested_mode_)
    {
      m = DIG_FM;
    }
    else if ("lsb" == requested_mode_)
    {
      m = LSB;
    }
    else if ("usb" == requested_mode_)
    {
      m = USB;
    }
    else if ("digl" == requested_mode_)
    {
      m = DIG_L;
    }
    else if ("digu" == requested_mode_)
    {
      m = DIG_U;
    }
  } else {
    if ("am" == mode_)
    {
      m = AM;
    }
    else if ("cw" == mode_)
    {
      m = CW;
    }
    else if ("wfm" == mode_)
    {
      m = FM;
    }
    else if ("nfm" == mode_)
    {
      m = DIG_FM;
    }
    else if ("lsb" == mode_)
    {
      m = LSB;
    }
    else if ("usb" == mode_)
    {
      m = USB;
    }
    else if ("digl" == mode_)
    {
      m = DIG_L;
    }
    else if ("digu" == mode_)
    {
      m = DIG_U;
    }
  }
  return m;
}

QString TCITransceiver::mode_to_command (QString m_string) const
{
  //    if (ESDR3) {
  //      const QString cmd = CmdMode + SmDP + rx_ + SmCM + m_string.toUpper() + SmTZ;
  //      return cmd;
  //    } else {
  const QString cmd = CmdMode + SmDP + rx_ + SmCM + m_string + SmTZ;
  return cmd;
  //    }
}

QString TCITransceiver::frequency_to_string (Frequency f) const
{
  // number is localized and in kHz, avoid floating point translation
  // errors by adding a small number (0.1Hz)
  auto f_string = QString {}.setNum(f);
  printf ("frequency_to_string3 |%s|\n",f_string.toStdString().c_str());
  return f_string;
}

auto TCITransceiver::string_to_frequency (QString s) const -> Frequency
{
  // temporary hack because Commander is returning invalid UTF-8 bytes
  s.replace (QChar {QChar::ReplacementCharacter}, locale_.groupSeparator ());

  bool ok;

  auto f = QLocale::c ().toDouble (s, &ok); // temporary fix

  if (!ok)
  {
    printf("Frequency rejected is ***%s***\n",s.toStdString().c_str());
    // throw error {tr ("TCI sent an unrecognized frequency") + " |" + s + "|"};
  }
  return f;
}

/*
 * mysleep1 is used in do_stop
 * mysleep2 is used in do_tx_frequency
 * mysleep3 is used in do_ptt
 * mysleep4 is used in rx2_enable
 * mysleep5 is used in rig_split
 * mysleep6 is used in do_start
 * mysleep7 is used in do_frequency
 * mysleep8 is used in do_mode which is never called
 * */

void TCITransceiver::mysleep1 (int ms)
{
  //printf("%s TCI sleep1 start %d %d\n",QDateTime::QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),ms,tci_timer1_->isActive());
  if (ms) tci_timer1_->start(ms);
  tci_loop1_->exec();
  if (tci_timer1_->isActive() && tci_Ready) tci_timer1_->stop();
}
void TCITransceiver::mysleep2 (int ms)
{
  if (ms) tci_timer2_->start(ms);
  tci_loop2_->exec();
  if (tci_timer2_->isActive()) tci_timer2_->stop();
}
void TCITransceiver::mysleep3 (int ms)
{
  if (ms) tci_timer3_->start(ms);
  tci_loop3_->exec();
  if (tci_timer3_->isActive()) tci_timer3_->stop();
}
void TCITransceiver::mysleep4 (int ms)
{
  if (ms) tci_timer4_->start(ms);
  tci_loop4_->exec();
  if (tci_timer4_->isActive()) tci_timer4_->stop();
}
void TCITransceiver::mysleep5 (int ms)
{
  if((tci_loop5_ && (tci_loop5_->isRunning())) || (tci_timer5_ && tci_timer5_->isActive())) return;
  if (ms) tci_timer5_->start(ms);
  tci_loop5_->exec();
  if (tci_timer5_->isActive()) tci_timer5_->stop();
}
void TCITransceiver::mysleep6 (int ms)
{
  if (ms) tci_timer6_->start(ms);
  tci_loop6_->exec();
  if (tci_timer6_->isActive()) tci_timer6_->stop();
}
void TCITransceiver::mysleep7 (int ms)
{
    if (ms) tci_timer7_->start(ms);
    tci_loop7_->exec();
    if (tci_timer7_->isActive()) tci_timer7_->stop();
}
void TCITransceiver::mysleep8 (int ms)
{
    if (ms) tci_timer8_->start(ms);
    tci_loop8_->exec();
    if (tci_timer8_->isActive()) tci_timer8_->stop();
}
// Modulator part

void TCITransceiver::do_modulator_start (QString mode, unsigned symbolsLength, double framesPerSymbol,
                                        double frequency, double toneSpacing, bool synchronize, bool fastMode, double dBSNR, double TRperiod)
{
  // Time according to this computer which becomes our base time
  qint64 ms0 = QDateTime::currentMSecsSinceEpoch() % 86400000;
  qDebug() << "ModStart" << QDateTime::QDateTime::currentDateTimeUtc().toString("hh:mm:ss.sss");
  unsigned mstr = ms0 % int(1000.0*m_period); // ms into the nominal Tx start time
  if (m_state != Idle) {
    //    stop ();
    throw error {tr ("TCI modulator not Idle")};
  }
  m_quickClose = false;
  m_symbolsLength = symbolsLength;
  m_isym0 = std::numeric_limits<unsigned>::max (); // big number
  m_frequency0 = 0.;
  m_phi = 0.;
  m_addNoise = dBSNR < 0.;
  m_nsps = framesPerSymbol;
  m_trfrequency = frequency;
  m_amp = std::numeric_limits<qint16>::max ();
  m_toneSpacing = toneSpacing;
  m_bFastMode=fastMode;
  m_TRperiod=TRperiod;
  unsigned delay_ms=1000;

  if((mode=="FT8" and m_nsps==1920) or (mode=="FST4" and m_nsps==720)) delay_ms=500;  //FT8, FST4-15
  if((mode=="FT8" and m_nsps==1024)) delay_ms=400;            //SuperFox Qary Polar Code transmission
  if(mode=="Q65" and m_nsps<=3600) delay_ms=500;              //Q65-15 and Q65-30
  if(mode=="FT4") delay_ms=300;                               //FT4

  // noise generator parameters
  if (m_addNoise) {
    m_snr = qPow (10.0, 0.05 * (dBSNR - 6.0));
    m_fac = 3000.0;
    if (m_snr > 1.0) m_fac = 3000.0 / m_snr;
  }

  // round up to an exact portion of a second that allows for startup delays
  auto mstr2 = mstr - delay_ms;
  if (mstr <= delay_ms) {
    m_ic = 0;
  } else {
    m_ic = mstr2 * (audioSampleRate / 1000);
  }

  m_silentFrames = 0;
  // calculate number of silent frames to send
  if (m_ic == 0 && synchronize && !m_tuning)	{
    m_silentFrames = audioSampleRate / (1000 / delay_ms) - (mstr * (audioSampleRate / 1000));
  }
  m_state = (synchronize && m_silentFrames) ?
                Synchronizing : Active;
  printf("%s TCI modulator startdelay_ms=%d ASR=%d mstr=%d mstr2=%d m_ic=%d s_Frames=%lld synchronize=%d m_tuning=%d State=%d\n",QDateTime::QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),delay_ms,audioSampleRate,mstr,mstr2,m_ic,m_silentFrames,synchronize,m_tuning,m_state);
  Q_EMIT tci_mod_active(m_state != Idle);
}

void TCITransceiver::do_tune (bool newState)
{
  m_tuning = newState;
  if (!m_tuning) do_modulator_stop (true);
}

void TCITransceiver::do_modulator_stop (bool quick)
{
  m_quickClose = quick;
  if(m_state != Idle) {
    m_state = Idle;
    Q_EMIT tci_mod_active(m_state != Idle);
  }
  tx_audio_ = false;
}

quint16 TCITransceiver::readAudioData (float * data, qint32 maxSize, qreal txAtten)
{
  double toneFrequency=1500.0;
  if(m_nsps==6) {
    toneFrequency=1000.0;
    m_trfrequency=1000.0;
    m_frequency0=1000.0;
  }
  if(maxSize==0) return 0;

  qreal newVolume = pow(2.22222 * (45 - txAtten) * 0.01,2);
  //printf("txAtten is %f and newVolume is %f\n",txAtten, newVolume);

  qint64 numFrames (maxSize/bytesPerFrame);
  float * samples (reinterpret_cast<float *> (data));
  float * end (samples + numFrames * bytesPerFrame);
  qint64 framesGenerated (0);

  switch (m_state)
  {
    case Synchronizing:
    {
      if (m_silentFrames)	{  // send silence up to first second
        framesGenerated = qMin (m_silentFrames, numFrames);
        for ( ; samples != end; samples = load (0, samples)) { // silence
        }
        m_silentFrames -= framesGenerated;
        return framesGenerated * bytesPerFrame;
      }
      m_state = Active;
      Q_EMIT tci_mod_active(m_state != Idle);
      m_cwLevel = false;
      m_ramp = 0;		// prepare for CW wave shaping
    }
      // fall through

    case Active:
    {
      unsigned int isym=0;
      qint16 sample=0;
      if(!m_tuning) isym=m_ic/(4.0*m_nsps);          // Actual fsample=48000
      bool slowCwId=((isym >= m_symbolsLength) && (icw[0] > 0));
      m_nspd=2560;                 // 22.5 WPM

      if(m_TRperiod > 16.0 && slowCwId) {     // Transmit CW ID?
        m_dphi = m_twoPi*m_trfrequency/audioSampleRate;
        unsigned ic0 = m_symbolsLength * 4 * m_nsps;
        unsigned j(0);

        while (samples != end) {
          j = (m_ic - ic0)/m_nspd + 1; // symbol of this sample
          bool level {bool (icw[j])};
          m_phi += m_dphi;
          if (m_phi > m_twoPi) m_phi -= m_twoPi;
          sample=0;
          float amp=32767.0;
          float x=0.0;
          if(m_ramp!=0) {
            x=qSin(float(m_phi));
            if(SOFT_KEYING) {
              amp=qAbs(qint32(m_ramp));
              if(amp>32767.0) amp=32767.0;
            }
            sample=round(newVolume * amp * x);
          }
          if (int (j) <= icw[0] && j < NUM_CW_SYMBOLS) { // stopu condition
            samples = load (postProcessSample (sample), samples);
            ++framesGenerated;
            ++m_ic;
          } else {
            m_state = Idle;
            Q_EMIT tci_mod_active(m_state != Idle);
            return framesGenerated * bytesPerFrame;
          }

          // adjust ramp
          if ((m_ramp != 0 && m_ramp != std::numeric_limits<qint16>::min ()) || level != m_cwLevel) {
            // either ramp has terminated at max/min or direction has changed
            m_ramp += RAMP_INCREMENT; // ramp
          }
          m_cwLevel = level;
        }
        return framesGenerated * bytesPerFrame;
      } //End of code for CW ID

      double const baud (12000.0 / m_nsps);
      // fade out parameters (no fade out for tuning)
      unsigned int i0,i1;
      if(m_tuning) {
        i1 = i0 =  (m_bFastMode ? 999999 : 9999) * m_nsps;
      } else {
        i0=(m_symbolsLength - 0.017) * 4.0 * m_nsps;
        i1= m_symbolsLength * 4.0 * m_nsps;
      }
      if(m_bFastMode and !m_tuning) {
        i1=m_TRperiod*48000.0 - 24000.0;
        i0=i1-816;
      }

      sample=0;
      for (unsigned i = 0; i < numFrames && m_ic <= i1; ++i) {
        isym=0;
        if(!m_tuning and m_TRperiod!=3.0) isym=m_ic/(4.0*m_nsps);   //Actual fsample=48000
        if(m_bFastMode) isym=isym%m_symbolsLength;
        if (isym != m_isym0 || m_trfrequency != m_frequency0) {
          if(itone[0]>=100) {
            m_toneFrequency0=itone[0];
          } else {
            if(m_toneSpacing==0.0) {
              m_toneFrequency0=m_trfrequency + itone[isym]*baud;
            } else {
              m_toneFrequency0=m_trfrequency + itone[isym]*m_toneSpacing;
            }
          }
          m_dphi = m_twoPi * m_toneFrequency0 / audioSampleRate;
          m_isym0 = isym;
          m_frequency0 = m_trfrequency;         //???
        }

        int j=m_ic/480;
        if(m_fSpread>0.0 and j!=m_j0) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
          float x1=QRandomGenerator::global ()->generateDouble ();
          float x2=QRandomGenerator::global ()->generateDouble ();
#else
          float x1=(float)qrand()/RAND_MAX;
          float x2=(float)qrand()/RAND_MAX;
#endif
          toneFrequency = m_toneFrequency0 + 0.5*m_fSpread*(x1+x2-1.0);
          m_dphi = m_twoPi * toneFrequency / audioSampleRate;
          m_j0=j;
        }

        m_phi += m_dphi;
        if (m_phi > m_twoPi) m_phi -= m_twoPi;
        //ramp for first tone
        if (m_ic==0) m_amp = m_amp * 0.008144735;
        if (m_ic > 0 and  m_ic < 191) m_amp = m_amp / 0.975;
        //ramp for last tone
        if (m_ic > i0) m_amp = 0.99 * m_amp;
        if (m_ic > i1) m_amp = 0.0;
        sample=qRound(newVolume * m_amp*qSin(m_phi));

        //transmit from a precomputed FT8 wave[] array:
        if(!m_tuning and (m_toneSpacing < 0.0)) {
          m_amp=32767.0;
          sample=qRound(newVolume * m_amp*foxcom_.wave[m_ic]);
        }
        samples = load (postProcessSample (sample), samples);
        ++framesGenerated;
        ++m_ic;
      }

      if (m_amp == 0.0) { // TODO G4WJS: compare double with zero might not be wise
        if (icw[0] == 0) {
          // no CW ID to send
          m_state = Idle;
          Q_EMIT tci_mod_active(m_state != Idle);
          return framesGenerated * bytesPerFrame;
        }
        m_phi = 0.0;
      }

      m_frequency0 = m_trfrequency;
      // done for this chunk - continue on next call
      if (samples != end && framesGenerated) {
      }
      while (samples != end) { // pad block with silence
        samples = load (0, samples);
        ++framesGenerated;
      }
      return framesGenerated * bytesPerFrame;
    }
      // fall through

    case Idle:
      while (samples != end) samples = load (0, samples);
  }

  Q_ASSERT (Idle == m_state);
  return 0;
}

qint16 TCITransceiver::postProcessSample (qint16 sample) const
{
  if (m_addNoise) {  // Test frame, we'll add noise
    qint32 s = m_fac * (gran () + sample * m_snr / 32768.0);
    if (s > std::numeric_limits<qint16>::max ()) {
      s = std::numeric_limits<qint16>::max ();
    }
    if (s < std::numeric_limits<qint16>::min ()) {
      s = std::numeric_limits<qint16>::min ();
    }
    sample = s;
  }
  return sample;
}
