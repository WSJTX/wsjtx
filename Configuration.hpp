#ifndef CONFIGURATION_HPP_
#define CONFIGURATION_HPP_

#include <QObject>
#include <QFont>
#include <QString>

#include "Radio.hpp"
#include "models/IARURegions.hpp"
#include "Audio/AudioDevice.hpp"
#include "Transceiver/Transceiver.hpp"
#include "otpgenerator.h"

#include "pimpl_h.hpp"

class QSettings;
class QWidget;
class QAudioDeviceInfo;
class QDir;
class QNetworkAccessManager;
class Bands;
class FrequencyList_v2_101;
class StationList;
class QStringListModel;
class LotWUsers;
class Cloudlog;
class DecodeHighlightingModel;
class LogBook;

//
// Class Configuration
//
//  Encapsulates the control, access  and, persistence of user defined
//  settings for the wsjtx GUI.  Setting values are accessed through a
//  QDialog window containing concept orientated tab windows.
//
// Responsibilities
//
//  Provides management of  the CAT and PTT  rig interfaces, providing
//  control access  via a minimal generic  set of Qt slots  and status
//  updates via Qt signals.  Internally  the rig control capability is
//  farmed out  to a  separate thread  since many  of the  rig control
//  functions are blocking.
//
//  All user  settings required by  the wsjtx GUI are  exposed through
//  query methods.  Settings only become  visible once they  have been
//  accepted by the user which is  done by clicking the "OK" button on
//  the settings dialog.
//
//  The QSettings instance  passed to the constructor is  used to read
//  and write user settings.
//
//  Pointers to three QAbstractItemModel  objects are provided to give
//  access to amateur band  information, user working frequencies and,
//  user operating  band information.  These porovide  consistent data
//  models that can  be used in GUI lists or  tables or simply queried
//  for user defined bands, default operating frequencies and, station
//  descriptions.
//
class Configuration final
  : public QObject
{
  Q_OBJECT

public:
  using MODE = Transceiver::MODE;
  using TransceiverState = Transceiver::TransceiverState;
  using Frequency = Radio::Frequency;
  using port_type = quint16;

  enum DataMode {data_mode_none, data_mode_USB, data_mode_data};
  Q_ENUM (DataMode)
  enum Type2MsgGen {type_2_msg_1_full, type_2_msg_3_full, type_2_msg_5_only};
  Q_ENUM (Type2MsgGen)

  explicit Configuration (QNetworkAccessManager *, QDir const& temp_directory, QSettings * settings,
                          LogBook * logbook, QWidget * parent = nullptr);
  ~Configuration ();

  void select_tab (int);
  int exec ();
  bool is_active () const;

  QDir temp_dir () const;
  QDir doc_dir () const;
  QDir data_dir () const;
  QDir writeable_data_dir () const;

  QAudioDeviceInfo const& audio_input_device () const;
  AudioDevice::Channel audio_input_channel () const;
  QAudioDeviceInfo const& audio_output_device () const;
  AudioDevice::Channel audio_output_channel () const;

  // These query methods should be used after a call to exec() to
  // determine if either the audio input or audio output stream
  // parameters have changed. The respective streams should be
  // re-opened if they return true.
  bool restart_audio_input () const;
  bool restart_audio_output () const;
  bool restart_tci () const;

  QString my_callsign () const;
  QString my_grid () const;
  QString Field_Day_Exchange() const;
  QString RTTY_Exchange() const;
  QString Contest_Name() const;
  QString Blacklist1() const;
  QString Blacklist2() const;
  QString Blacklist3() const;
  QString Blacklist4() const;
  QString Blacklist5() const;
  QString Blacklist6() const;
  QString Blacklist7() const;
  QString Blacklist8() const;
  QString Blacklist9() const;
  QString Blacklist10() const;
  QString Blacklist11() const;
  QString Blacklist12() const;
  QString Whitelist1() const;
  QString Whitelist2() const;
  QString Whitelist3() const;
  QString Whitelist4() const;
  QString Whitelist5() const;
  QString Whitelist6() const;
  QString Whitelist7() const;
  QString Whitelist8() const;
  QString Whitelist9() const;
  QString Whitelist10() const;
  QString Whitelist11() const;
  QString Whitelist12() const;
  QString Pass1() const;
  QString Pass2() const;
  QString Pass3() const;
  QString Pass4() const;
  QString Pass5() const;
  QString Pass6() const;
  QString Pass7() const;
  QString Pass8() const;
  QString Pass9() const;
  QString Pass10() const;
  QString Pass11() const;
  QString Pass12() const;
  QString Territory1() const;
  QString Territory2() const;
  QString Territory3() const;
  QString Territory4() const;
  QString highlight_orange_callsigns() const;
  QString highlight_blue_callsigns() const;
  QString voicesPath() const;
  bool PWR_and_SWR() const;
  bool check_SWR() const;

  void setEU_VHF_Contest();
  QFont text_font () const;
  QFont decoded_text_font () const;
  qint32 id_interval () const;
  qint32 align_steps () const;
  qint32 align_steps2 () const;
  qint32 ntrials() const;
  qint32 volume() const;
  qint32 aggressive() const;
  qint32 RxBandwidth() const;
  double degrade() const;
  double txDelay() const;
  bool tci_audio() const;
  bool id_after_73 () const;
  bool tx_QSY_allowed () const;
  bool progressBar_red () const;
  bool spot_to_psk_reporter () const;
  bool psk_reporter_tcpip () const;
  bool send_to_eqsl () const;
  QString eqsl_username () const;
  QString eqsl_passwd () const;
  QString eqsl_nickname () const;
  bool monitor_off_at_startup () const;
  bool monitor_last_used () const;
  bool log_as_RTTY () const;
  bool report_in_comments () const;
  bool specOp_in_comments () const;
  bool cloudlog_enabled () const;
  QString cloudlog_api_url() const;
  QString cloudlog_api_key() const;
  qint32 cloudlog_api_station_id() const;
  bool prompt_to_log () const;
  bool autoLog() const;
  bool contestingOnly() const;
  bool ZZ00() const;
  bool log4digitGrids() const;
  bool decodes_from_top () const;
  bool insert_blank () const;
  bool detailed_blank () const;
  bool DXCC () const;
  bool GridMap() const;
  bool GridMapAll() const;
  bool ppfx() const;
  bool miles () const;
  bool quick_call () const;
  bool disable_TX_on_73 () const;
  bool force_call_1st() const;
  bool alternate_bindings() const;
  int watchdog () const;
  int tune_watchdog_time () const;
  bool tune_watchdog () const;
  bool TX_messages () const;
  bool split_mode () const;
  bool enable_VHF_features () const;
  bool decode_at_52s () const;
  bool kHz_without_k () const;
  bool button_coloring_disabled () const;
  bool Wait_features_enabled () const;
  bool showDistance() const ;
  bool showAzimuth() const ;
  bool align() const;
  bool repeat_Tx () const;
  bool auto_astro () const;
  bool single_decode () const;
  bool twoPass() const;
  bool highDPI() const;
  bool largerTabWidget() const;
  bool superFox() const;
  bool bFox() const;
  bool bHound() const;
  bool bLowSidelobes() const;
  bool x2ToneSpacing() const;
  bool x4ToneSpacing() const;
  bool MyDx() const;
  bool CQMyN() const;
  bool NDxG() const;
  bool NN() const;
  bool EMEonly() const;
  bool post_decodes () const;
  QString opCall() const;
  void opCall (QString const&);
  QString udp_server_name () const;
  port_type udp_server_port () const;
  QStringList udp_interface_names () const;
  int udp_TTL () const;
  QString n1mm_server_name () const;
  port_type n1mm_server_port () const;
  bool valid_n1mm_info () const;
  bool broadcast_to_n1mm() const;
  bool broadcast_to_cloudlog() const;
  bool lowSidelobes() const;
  bool accept_udp_requests () const;
  bool udpWindowToFront () const;
  bool udpWindowRestore () const;
  Bands * bands ();
  Bands const * bands () const;
  IARURegions::Region region () const;
  FrequencyList_v2_101 * frequencies ();
  FrequencyList_v2_101 const * frequencies () const;
  StationList * stations ();
  StationList const * stations () const;
  QStringListModel * macros ();
  QStringListModel const * macros () const;
  QDir save_directory () const;
  QDir azel_directory () const;
  QString rig_name () const;
  Type2MsgGen type_2_msg_gen () const;
  bool AzElExtraLines () const;
  bool pwrBandTxMemory () const;
  bool pwrBandTuneMemory () const;
  LotWUsers const& lotw_users () const;
  Cloudlog const& cloudlog () const;
  DecodeHighlightingModel const& decode_highlighting () const;
  bool highlight_by_mode () const;
  bool highlight_only_fields () const;
  bool include_WAE_entities () const;
  bool highlight_73 () const;
  bool highlight_orange () const;
  bool highlight_blue () const;
  bool bSpecialOp () const;
  bool show_country_names () const;
  bool alternate_erase_button () const;
  void setSpecial_Q65_Pileup();
  void setSpecial_Hound();
  void setSpecial_Fox();
  void setSpecial_None();
  void setSpecial_On();
  void toggle_SF();
  bool highlight_DXcall () const;
  bool clear_DXcall () const;
  bool highlight_DXgrid () const;
  bool clear_DXgrid () const;
  bool erase_BandActivity () const;
  bool set_RXtoTX () const;
  bool Individual_Contest_Name() const;
  bool NCCC_Sprint() const;
  bool Blacklisted() const;
  bool Whitelisted() const;
  bool AlwaysPass() const;
  bool filters_for_Wait_and_Pounce_only() const;
  bool filters_for_word2() const;
  bool twoDays() const;
  bool alert_CQ () const;
  bool alert_MyCall () const;
  bool alert_DXCC () const;
  bool alert_DXCCOB () const;
  bool alert_Grid () const;
  bool alert_GridOB () const;
  bool alert_Continent () const;
  bool alert_ContinentOB () const;
  bool alert_CQZ () const;
  bool alert_CQZOB () const;
  bool alert_ITUZ () const;
  bool alert_ITUZOB () const;
  bool alert_DXcall () const;
  bool alert_Wanted () const;
  bool alert_QSYmessage () const;
  bool alert_Enabled () const;
  bool validate_otp_seed(QString);
  QString OTPSeed() const;
  QString OTPUrl() const;
  bool OTPEnabled() const;
  bool ShowOTP() const;
  unsigned int OTPinterval() const;
//                                      0       1      2         3       4       5      6     7        8           9
  enum class SpecialOperatingActivity {NONE, NA_VHF, EU_VHF, FIELD_DAY, RTTY, WW_DIGI, FOX, HOUND, ARRL_DIGI, Q65_PILEUP};
  SpecialOperatingActivity special_op_id () const;

  struct CalibrationParams
  {
    CalibrationParams ()
      : intercept {0.}
      , slope_ppm {0.}
    {
    }

    CalibrationParams (double the_intercept, double the_slope_ppm)
      : intercept {the_intercept}
      , slope_ppm {the_slope_ppm}
    {
    }

    double intercept;           // Hertz
    double slope_ppm;           // Hertz
  };

  // Temporarily enable or disable calibration adjustments.
  void enable_calibration (bool = true);

  // Set the calibration parameters and enable calibration corrections.
  void set_calibration (CalibrationParams);

  // Set the dynamic grid which is only used if configuration setting is enabled.
  void set_location (QString const&);

  // This method queries if a CAT and PTT connection is operational.
  bool is_transceiver_online () const;

  // Start the rig connection, safe and normal to call when rig is
  // already open.
  bool transceiver_online ();

  // check if a real rig is configured
  bool is_dummy_rig () const;
  bool is_tci () const;

  // Frequency resolution of the rig
  //
  //  0 - 1Hz
  //  1 - 10Hz rounded
  // -1 - 10Hz truncated
  //  2 - 100Hz rounded
  // -2 - 100Hz truncated
  int transceiver_resolution () const;

  // Close down connection to rig.
  void transceiver_offline ();

  void set_CTY_DAT_version(QString const& version);
  void read_CALL3_version ();

  // Set transceiver frequency in Hertz.
  Q_SLOT void transceiver_frequency (Frequency);

  // Setting a non zero TX frequency means split operation
  // rationalise_mode means ensure TX uses same mode as RX.
  Q_SLOT void transceiver_tx_frequency (Frequency = 0u);

  // Set transceiver mode.
  Q_SLOT void transceiver_mode (MODE);

  // Set/unset PTT.
  //
  // Note that this must be called even if VOX PTT is selected since
  // the "Emulate Split" mode requires PTT information to coordinate
  // frequency changes.
  Q_SLOT void transceiver_ptt (bool = true);
//  Q_SLOT void transceiver_tune (bool = true);

  // Set/unset Audio streaming for TCI.
  //
  Q_SLOT void transceiver_audio (bool = false);

  // Set/unset Tune for TCI.
  //
  Q_SLOT void transceiver_tune (bool = false);

  // Set period for TCI audio
  //
  Q_SLOT void transceiver_period (double = 15.0);

  // Set blocksize for TCI audio.
  //
  Q_SLOT void transceiver_blocksize (qint32 = 6912 / 2);

  // Set modulation start TCI audio
  //
  Q_SLOT void transceiver_modulator_start (QString="FT8", unsigned = 79, double = 1920.0, double = 1500.0, double = -3.0, bool = true, bool=false, double = 99., double = 60.0);

  // Set modulation start TCI audio
  //
  Q_SLOT void transceiver_modulator_stop (bool = false);

  // Set spread for TCI audio
  //
  Q_SLOT void transceiver_spread (double = 0.0);

  // Set nsym for TCI audio
  //
  Q_SLOT void transceiver_nsym (int = 79);

  // Set trfrequency for TCI audio
  //
  Q_SLOT void transceiver_trfrequency (double = 1500.0);

  // Attempt to (re-)synchronise transceiver state.
  //
  // Force signal guarantees either a transceiver_update or a
  // transceiver_failure signal.
  //
  // The enforce_mode_and_split parameter ensures that future
  // transceiver updates have the correct mode and split setting
  // i.e. the transceiver is ready for use.
  Q_SLOT void sync_transceiver (bool force_signal = false, bool enforce_mode_and_split = false);

  Q_SLOT void invalidate_audio_input_device (QString error);
  Q_SLOT void invalidate_audio_output_device (QString error);

  //
  // These signals indicate a font has been selected and accepted for
  // the application text and decoded text respectively.
  //
  Q_SIGNAL void text_font_changed (QFont) const;
  Q_SIGNAL void decoded_text_font_changed (QFont) const;

  //
  // This signal is emitted when the UDP server changes
  //
  Q_SIGNAL void udp_server_changed (QString& udp_server_name, QStringList const& network_interfaces) const;
  Q_SIGNAL void udp_server_port_changed (port_type server_port) const;
  Q_SIGNAL void udp_TTL_changed (int TTL) const;
  Q_SIGNAL void accept_udp_requests_changed (bool checked) const;

  // signal updates to decode highlighting
  Q_SIGNAL void decode_highlighting_changed (DecodeHighlightingModel const&) const;

  //
  // These signals are emitted and reflect transceiver state changes
  //

  // signals a change in one of the TransceiverState members
  Q_SIGNAL void transceiver_update (Transceiver::TransceiverState const&) const;
  Q_SIGNAL void transceiver_TCIframesWritten (qint64) const;
  Q_SIGNAL void transceiver_TCImodActive (bool) const;
  Q_SIGNAL void leavingSettings (bool) const;

  // Signals a failure of a control rig CAT or PTT connection.
  //
  // A failed rig CAT or PTT connection is fatal and the underlying
  // connections are closed automatically. The connections can be
  // re-established with a call to transceiver_online(true) assuming
  // the fault condition has been rectified or is transient.
  Q_SIGNAL void transceiver_failure (QString const& reason) const;

  // signal announces audio devices are being enumerated
  //
  // As this can take some time, particularly on Linux, consumers
  // might like to notify the user.
  Q_SIGNAL void enumerating_audio_devices ();

public slots:
  Q_SLOT void transceiver_volume (double = 0);
  Q_SLOT void transceiver_txvolume (double = 0);

private:
  class impl;
  pimpl<impl> m_;
};

ENUM_QDATASTREAM_OPS_DECL (Configuration, DataMode);
ENUM_QDATASTREAM_OPS_DECL (Configuration, Type2MsgGen);

ENUM_CONVERSION_OPS_DECL (Configuration, DataMode);
ENUM_CONVERSION_OPS_DECL (Configuration, Type2MsgGen);

#endif
