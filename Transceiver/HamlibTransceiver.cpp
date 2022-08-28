#include "HamlibTransceiver.hpp"

#include <cstring>
#include <cmath>
#include <tuple>
#include <QByteArray>
#include <QString>
#include <QStandardPaths>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QDebug>
#include <hamlib/rig.h>
#include "pimpl_impl.hpp"
#include "moc_HamlibTransceiver.cpp"

#if HAVE_HAMLIB_OLD_CACHING
#define HAMLIB_CACHE_ALL CACHE_ALL
#endif

namespace
{
  // Unfortunately bandwidth is conflated  with mode, this is probably
  // because Icom do  the same. So we have to  care about bandwidth if
  // we want  to set  mode otherwise we  will end up  setting unwanted
  // bandwidths every time we change mode.  The best we can do via the
  // Hamlib API is to request the  normal option for the mode and hope
  // that an appropriate filter is selected.  Also ensure that mode is
  // only set is absolutely necessary.  On Icoms (and probably others)
  // the filter is  selected by number without checking  the actual BW
  // so unless the  "normal" defaults are set on the  rig we won't get
  // desirable results.
  //
  // As  an ultimate  workaround make  sure  the user  always has  the
  // option to skip mode setting altogether.

  // callback function that receives transceiver capabilities from the
  // hamlib libraries
  int register_callback (rig_model_t rig_model, void * callback_data)
  {
    TransceiverFactory::Transceivers * rigs = reinterpret_cast<TransceiverFactory::Transceivers *> (callback_data);
    // We can't use this one because it is only for testing Hamlib and
    // would confuse users, possibly causing operating on the wrong
    // frequency!
#ifdef RIG_MODEL_DUMMY_NOVFO
    if (RIG_MODEL_DUMMY_NOVFO == rig_model)
      {
        return 1;
      }
#endif

    QString key;
    if (RIG_MODEL_DUMMY == rig_model)
      {
        key = TransceiverFactory::basic_transceiver_name_;
      }
    else
      {
        key = QString::fromLatin1 (rig_get_caps_cptr (rig_model, RIG_CAPS_MFG_NAME_CPTR)).trimmed ()
          + ' '+ QString::fromLatin1 (rig_get_caps_cptr (rig_model, RIG_CAPS_MODEL_NAME_CPTR)).trimmed ()
          // + ' '+ QString::fromLatin1 (rig_get_caps_cptr (rig_model, RIG_CAPS_VERSION)).trimmed ()
          // + " (" + QString::fromLatin1 (rig_get_caps_cptr (rig_model, RIG_CAPS_STATUS)).trimmed () + ')'
          ;
      }

    auto port_type = TransceiverFactory::Capabilities::none;
    switch(rig_get_caps_int (rig_model, RIG_CAPS_PORT_TYPE))
      {
      case RIG_PORT_SERIAL:
        port_type = TransceiverFactory::Capabilities::serial;
        break;

      case RIG_PORT_NETWORK:
        port_type = TransceiverFactory::Capabilities::network;
        break;

      case RIG_PORT_USB:
        port_type = TransceiverFactory::Capabilities::usb;
        break;

      default: break;
      }
	  auto ptt_type = rig_get_caps_int (rig_model, RIG_CAPS_PTT_TYPE);
    (*rigs)[key] = TransceiverFactory::Capabilities (rig_model
                                                     , port_type
                                                     , RIG_MODEL_DUMMY != rig_model
                                                     && (RIG_PTT_RIG == ptt_type
                                                         || RIG_PTT_RIG_MICDATA == ptt_type)
                                                     , RIG_PTT_RIG_MICDATA == ptt_type);

    return 1;			// keep them coming
  }

  int unregister_callback (rig_model_t rig_model, void *)
  {
    rig_unregister (rig_get_caps_int (rig_model, RIG_CAPS_RIG_MODEL));
    return 1;			// keep them coming
  }

  // int frequency_change_callback (RIG * /* rig */, vfo_t vfo, freq_t f, rig_ptr_t arg)
  // {
  //   (void)vfo;			// unused in release build

  //   Q_ASSERT (vfo == RIG_VFO_CURR); // G4WJS: at the time of writing only current VFO is signalled by hamlib

  //   HamlibTransceiver * transceiver (reinterpret_cast<HamlibTransceiver *> (arg));
  //   Q_EMIT transceiver->frequency_change (f, Transceiver::A);
  //   return RIG_OK;
  // }

  class hamlib_tx_vfo_fixup final
  {
  public:
    hamlib_tx_vfo_fixup (RIG * rig, vfo_t tx_vfo)
      : rig_ {rig}
    {
      original_vfo_ = rig_->state.tx_vfo;
      rig_->state.tx_vfo = tx_vfo;
    }

    ~hamlib_tx_vfo_fixup ()
    {
      rig_->state.tx_vfo = original_vfo_;
    }

  private:
    RIG * rig_;
    vfo_t original_vfo_;
  };
}

class HamlibTransceiver::impl final
{
public:
  impl (HamlibTransceiver::logger_type * logger)
    : logger_ {logger}
    , model_ {RIG_MODEL_DUMMY}
    , rig_ {rig_init (model_)}
    , ptt_only_ {true}
    , back_ptt_port_ {false}
    , one_VFO_ {false}
    , is_dummy_ {true}
    , reversed_ {false}
    , freq_query_works_ {true}
    , mode_query_works_ {true}
    , split_query_works_ {true}
    , tickle_hamlib_ {false}
    , get_vfo_works_ {true}
    , set_vfo_works_ {true}
  {
  }

  impl (HamlibTransceiver::logger_type * logger, unsigned model_number
        , TransceiverFactory::ParameterPack const& params)
    :  logger_ {logger}
    , model_ {model_number}
    , rig_ {rig_init (model_)}
    , ptt_only_ {false}
    , back_ptt_port_ {TransceiverFactory::TX_audio_source_rear == params.audio_source}
    , one_VFO_ {false}
    , is_dummy_ {RIG_MODEL_DUMMY == model_}
    , reversed_ {false}
    , freq_query_works_ {rig_ && rig_get_function_ptr (model_, RIG_FUNCTION_GET_FREQ)}
    , mode_query_works_ {rig_ && rig_get_function_ptr (model_, RIG_FUNCTION_GET_MODE)}
    , split_query_works_ {rig_ && rig_get_function_ptr (model_, RIG_FUNCTION_GET_SPLIT_VFO)}
    , tickle_hamlib_ {false}
    , get_vfo_works_ {true}
    , set_vfo_works_ {true}
  {
  }

  HamlibTransceiver::logger_type& logger () const
  {
    return *logger_;
  }

  void error_check (int ret_code, QString const& doing) const;
  void set_conf (char const * item, char const * value);
  QByteArray get_conf (char const * item);
  Transceiver::MODE map_mode (rmode_t) const;
  rmode_t map_mode (Transceiver::MODE mode) const;
  std::tuple<vfo_t, vfo_t> get_vfos (bool for_split) const;

  HamlibTransceiver::logger_type mutable * logger_;
  unsigned model_;
  struct RIGDeleter {static void cleanup (RIG *);};
  QScopedPointer<RIG, RIGDeleter> rig_;

  bool ptt_only_;               // we can use a dummy device for PTT
  bool back_ptt_port_;
  bool one_VFO_;
  bool is_dummy_;

  // these are saved on destruction so we can start new instances
  // where the last one left off
  static freq_t dummy_frequency_;
  static rmode_t dummy_mode_;

  bool mutable reversed_;

  bool freq_query_works_;
  bool mode_query_works_;
  bool split_query_works_;
  bool tickle_hamlib_;          // Hamlib requires a
                                // rig_set_split_vfo() call to
                                // establish the Tx VFO
  bool get_vfo_works_;          // Net rigctl promises what it can't deliver
  bool set_vfo_works_;          // More rigctl promises which it can't deliver

  static int debug_callback (enum rig_debug_level_e level, rig_ptr_t arg, char const * format, va_list ap);
};

freq_t HamlibTransceiver::impl::dummy_frequency_;
rmode_t HamlibTransceiver::impl::dummy_mode_ {RIG_MODE_NONE};

  // reroute Hamlib diagnostic messages to Qt
int HamlibTransceiver::impl::debug_callback (enum rig_debug_level_e level, rig_ptr_t arg, char const * format, va_list ap)
{
  auto logger = reinterpret_cast<logger_type *> (arg);
  auto message = QString::vasprintf (format, ap);
  va_end (ap);
  auto severity = boost::log::trivial::trace;
  switch (level)
    {
    case RIG_DEBUG_BUG: severity = boost::log::trivial::fatal; break;
    case RIG_DEBUG_ERR: severity = boost::log::trivial::error; break;
    case RIG_DEBUG_WARN: severity = boost::log::trivial::warning; break;
    case RIG_DEBUG_VERBOSE: severity = boost::log::trivial::debug; break;
    case RIG_DEBUG_TRACE: severity = boost::log::trivial::trace; break;
    default: break;
    };
  if (level != RIG_DEBUG_NONE) // no idea what level NONE means so
    // ignore it
    {
      BOOST_LOG_SEV (*logger, severity) << message.trimmed ().toStdString ();
    }
  return 0;
}

void HamlibTransceiver::register_transceivers (logger_type * logger,
                                               TransceiverFactory::Transceivers * registry)
{
  rig_set_debug_callback (impl::debug_callback, logger);
  rig_set_debug (RIG_DEBUG_TRACE);
  BOOST_LOG_SEV (*logger, boost::log::trivial::info) << "Hamlib version: " << rig_version ();
  rig_load_all_backends ();
  rig_list_foreach_model (register_callback, registry);
}

void HamlibTransceiver::unregister_transceivers ()
{
  rig_list_foreach_model (unregister_callback, nullptr);
}

void HamlibTransceiver::impl::RIGDeleter::cleanup (RIG * rig)
{
  if (rig)
    {
      rig_cleanup (rig);
    }
}

void HamlibTransceiver::impl::error_check (int ret_code, QString const& doing) const
{
  if (RIG_OK != ret_code)
    {
      CAT_ERROR ("error: " << rigerror (ret_code));
      throw error {tr ("Hamlib error: %1 while %2").arg (rigerror (ret_code)).arg (doing)};
    }
}

std::tuple<vfo_t, vfo_t> HamlibTransceiver::impl::get_vfos (bool for_split) const
{
  if (get_vfo_works_ && rig_get_function_ptr (model_, RIG_FUNCTION_GET_VFO))
    {
      vfo_t v;
      error_check (rig_get_vfo (rig_.data (), &v), tr ("getting current VFO")); // has side effect of establishing current VFO inside hamlib
      CAT_TRACE ("rig_get_vfo VFO=" << rig_strvfo (v));

      reversed_ = RIG_VFO_B == v;
    }
  else if (!for_split && set_vfo_works_ && rig_get_function_ptr (model_, RIG_FUNCTION_SET_VFO) && rig_get_function_ptr (model_, RIG_FUNCTION_SET_SPLIT_VFO))
    {
      // use VFO A/MAIN for main frequency and B/SUB for Tx
      // frequency if split since these type of radios can only
      // support this way around

      CAT_TRACE ("rig_set_vfo VFO=A/MAIN");
      error_check (rig_set_vfo (rig_.data (), rig_->state.vfo_list & RIG_VFO_A ? RIG_VFO_A : RIG_VFO_MAIN), tr ("setting current VFO"));
    }
  // else only toggle available but VFOs should be substitutable 

  auto rx_vfo = rig_->state.vfo_list & RIG_VFO_A ? RIG_VFO_A : RIG_VFO_MAIN;
  auto tx_vfo = (WSJT_RIG_NONE_CAN_SPLIT || !is_dummy_) && for_split
    ? (rig_->state.vfo_list & RIG_VFO_B ? RIG_VFO_B : RIG_VFO_SUB)
    : rx_vfo;
  if (reversed_)
    {
      CAT_TRACE ("reversing VFOs");
      std::swap (rx_vfo, tx_vfo);
    }

  CAT_TRACE ("RX VFO=" << rig_strvfo (rx_vfo) << " TX VFO=" << rig_strvfo (tx_vfo));
  return std::make_tuple (rx_vfo, tx_vfo);
}

void HamlibTransceiver::impl::set_conf (char const * item, char const * value)
{
  token_t token = rig_token_lookup (rig_.data (), item);
  if (RIG_CONF_END != token)	// only set if valid for rig model
    {
      error_check (rig_set_conf (rig_.data (), token, value), tr ("setting a configuration item"));
    }
}

QByteArray HamlibTransceiver::impl::get_conf (char const * item)
{
  token_t token = rig_token_lookup (rig_.data (), item);
  QByteArray value {128, '\0'};
  if (RIG_CONF_END != token)	// only get if valid for rig model
    {
      error_check (rig_get_conf (rig_.data (), token, value.data ()), tr ("getting a configuration item"));
    }
  return value;
}

auto HamlibTransceiver::impl::map_mode (rmode_t m) const -> MODE
{
  switch (m)
    {
    case RIG_MODE_AM:
    case RIG_MODE_SAM:
    case RIG_MODE_AMS:
    case RIG_MODE_DSB:
      return AM;

    case RIG_MODE_CW:
      return CW;

    case RIG_MODE_CWR:
      return CW_R;

    case RIG_MODE_USB:
    case RIG_MODE_ECSSUSB:
    case RIG_MODE_SAH:
    case RIG_MODE_FAX:
      return USB;

    case RIG_MODE_LSB:
    case RIG_MODE_ECSSLSB:
    case RIG_MODE_SAL:
      return LSB;

    case RIG_MODE_RTTY:
      return FSK;

    case RIG_MODE_RTTYR:
      return FSK_R;

    case RIG_MODE_PKTLSB:
      return DIG_L;

    case RIG_MODE_PKTUSB:
      return DIG_U;

    case RIG_MODE_FM:
    case RIG_MODE_WFM:
      return FM;

    case RIG_MODE_PKTFM:
      return DIG_FM;

    default:
      return UNK;
    }
}

rmode_t HamlibTransceiver::impl::map_mode (MODE mode) const
{
  switch (mode)
    {
    case AM: return RIG_MODE_AM;
    case CW: return RIG_MODE_CW;
    case CW_R: return RIG_MODE_CWR;
    case USB: return RIG_MODE_USB;
    case LSB: return RIG_MODE_LSB;
    case FSK: return RIG_MODE_RTTY;
    case FSK_R: return RIG_MODE_RTTYR;
    case DIG_L: return RIG_MODE_PKTLSB;
    case DIG_U: return RIG_MODE_PKTUSB;
    case FM: return RIG_MODE_FM;
    case DIG_FM: return RIG_MODE_PKTFM;
    default: break;
    }
  return RIG_MODE_USB;	// quieten compiler grumble
}

HamlibTransceiver::HamlibTransceiver (logger_type * logger,
                                      TransceiverFactory::PTTMethod ptt_type, QString const& ptt_port,
                                      QObject * parent)
  : PollingTransceiver {logger, 0, parent}
  , m_ {logger}
{
  if (!m_->rig_)
    {
      throw error {tr ("Hamlib initialisation error")};
    }
  switch (ptt_type)
    {
    case TransceiverFactory::PTT_method_VOX:
      m_->set_conf ("ptt_type", "None");
      break;

    case TransceiverFactory::PTT_method_CAT:
      // Use the default PTT_TYPE for the rig (defined in the Hamlib
      // rig back-end capabilities).
      break;

    case TransceiverFactory::PTT_method_DTR:
    case TransceiverFactory::PTT_method_RTS:
      if (!ptt_port.isEmpty ())
        {
#if defined (WIN32)
          m_->set_conf ("ptt_pathname", ("\\\\.\\" + ptt_port).toLatin1 ().data ());
#else
          m_->set_conf ("ptt_pathname", ptt_port.toLatin1 ().data ());
#endif
        }

      if (TransceiverFactory::PTT_method_DTR == ptt_type)
        {
          m_->set_conf ("ptt_type", "DTR");
        }
      else
        {
          m_->set_conf ("ptt_type", "RTS");
        }
      m_->set_conf ("ptt_share", "1");
    }

  // do this late to allow any configuration option to be overriden
  load_user_settings ();
}

HamlibTransceiver::HamlibTransceiver (logger_type * logger,
                                      unsigned model_number,
                                      TransceiverFactory::ParameterPack const& params,
                                      QObject * parent)
  : PollingTransceiver {logger, params.poll_interval, parent}
  , m_ {logger, model_number, params}
{
  if (!m_->rig_)
    {
      throw error {tr ("Hamlib initialisation error")};
    }

  // m_->rig_->state.obj = this;

  if (!m_->is_dummy_)
    {
      switch (rig_get_caps_int (m_->model_, RIG_CAPS_PORT_TYPE))
        {
        case RIG_PORT_SERIAL:
          if (!params.serial_port.isEmpty ())
            {
              m_->set_conf ("rig_pathname", params.serial_port.toLatin1 ().data ());
            }
          m_->set_conf ("serial_speed", QByteArray::number (params.baud).data ());
          if (params.data_bits != TransceiverFactory::default_data_bits)
            {
              m_->set_conf ("data_bits", TransceiverFactory::seven_data_bits == params.data_bits ? "7" : "8");
            }
          if (params.stop_bits != TransceiverFactory::default_stop_bits)
            {
              m_->set_conf ("stop_bits", TransceiverFactory::one_stop_bit == params.stop_bits ? "1" : "2");
            }

          switch (params.handshake)
            {
            case TransceiverFactory::handshake_none: m_->set_conf ("serial_handshake", "None"); break;
            case TransceiverFactory::handshake_XonXoff: m_->set_conf ("serial_handshake", "XONXOFF"); break;
            case TransceiverFactory::handshake_hardware: m_->set_conf ("serial_handshake", "Hardware"); break;
            default: break;
            }

          if (params.force_dtr)
            {
              m_->set_conf ("dtr_state", params.dtr_high ? "ON" : "OFF");
            }
          if (params.force_rts)
            {
              if (TransceiverFactory::handshake_hardware != params.handshake)
                {
                  m_->set_conf ("rts_state", params.rts_high ? "ON" : "OFF");
                }
            }
          break;

        case RIG_PORT_NETWORK:
          if (!params.network_port.isEmpty ())
            {
              m_->set_conf ("rig_pathname", params.network_port.toLatin1 ().data ());
            }
          break;

        case RIG_PORT_USB:
          if (!params.usb_port.isEmpty ())
            {
              m_->set_conf ("rig_pathname", params.usb_port.toLatin1 ().data ());
            }
          break;

        default:
          throw error {tr ("Unsupported CAT type")};
          break;
        }
    }

  switch (params.ptt_type)
    {
    case TransceiverFactory::PTT_method_VOX:
      m_->set_conf ("ptt_type", "None");
      break;

    case TransceiverFactory::PTT_method_CAT:
      // Use the default PTT_TYPE for the rig (defined in the Hamlib
      // rig back-end capabilities).
      break;

    case TransceiverFactory::PTT_method_DTR:
    case TransceiverFactory::PTT_method_RTS:
      if (params.ptt_port.size ()
          && params.ptt_port != "None"
          && (m_->is_dummy_
              || RIG_PORT_SERIAL != rig_get_caps_int (m_->model_, RIG_CAPS_PORT_TYPE)
              || params.ptt_port != params.serial_port))
        {
#if defined (WIN32)
          m_->set_conf ("ptt_pathname", ("\\\\.\\" + params.ptt_port).toLatin1 ().data ());
#else
          m_->set_conf ("ptt_pathname", params.ptt_port.toLatin1 ().data ());
#endif
        }

      if (TransceiverFactory::PTT_method_DTR == params.ptt_type)
        {
          m_->set_conf ("ptt_type", "DTR");
        }
      else
        {
          m_->set_conf ("ptt_type", "RTS");
        }
      m_->set_conf ("ptt_share", "1");
    }

  // Make Icom CAT split commands less glitchy
  m_->set_conf ("no_xchg", "1");

  // do this late to allow any configuration option to be overriden
  load_user_settings ();

  // would be nice to get events but not supported on Windows and also not on a lot of rigs
  // rig_set_freq_callback (m_->rig_.data (), &frequency_change_callback, this);
}

HamlibTransceiver::~HamlibTransceiver () = default;

void HamlibTransceiver::load_user_settings ()
{
  //
  // user defined Hamlib settings
  //
  auto settings_file_name = QStandardPaths::locate (QStandardPaths::AppConfigLocation
                                                    , "hamlib_settings.json");
  if (!settings_file_name.isEmpty ())
    {
      QFile settings_file {settings_file_name};
      qDebug () << "Using Hamlib settings file:" << settings_file_name;
      if (settings_file.open (QFile::ReadOnly))
        {
          QJsonParseError status;
          auto settings_doc = QJsonDocument::fromJson (settings_file.readAll (), &status);
          if (status.error)
            {
              throw error {tr ("Hamlib settings file error: %1 at character offset %2")
                             .arg (status.errorString ()).arg (status.offset)};
            }
          qDebug () << "Hamlib settings JSON:" << settings_doc.toJson ();
          if (!settings_doc.isObject ())
            {
              throw error {tr ("Hamlib settings file error: top level must be a JSON object")};
            }
          auto const& settings = settings_doc.object ();

          //
          // configuration settings
          //
          auto const& config = settings["config"];
          if (!config.isUndefined ())
            {
              if (!config.isObject ())
                {
                  throw error {tr ("Hamlib settings file error: config must be a JSON object")};
                }
              auto const& config_list = config.toObject ();
              for (auto item = config_list.constBegin (); item != config_list.constEnd (); ++item)
                {
                  m_->set_conf (item.key ().toLocal8Bit ().constData ()
                                , (*item).toVariant ().toString ().toLocal8Bit ().constData ());
                }
            }
        }
    }
}

int HamlibTransceiver::do_start ()
{
  CAT_TRACE ("starting: " << rig_get_caps_cptr (m_->model_, RIG_CAPS_MFG_NAME_CPTR)
             << ": " << rig_get_caps_cptr (m_->model_, RIG_CAPS_MODEL_NAME_CPTR));

  m_->error_check (rig_open (m_->rig_.data ()), tr ("opening connection to rig"));

  // reset dynamic state
  m_->one_VFO_ = false;
  m_->reversed_ = false;
  m_->freq_query_works_ = rig_get_function_ptr (m_->model_, RIG_FUNCTION_GET_FREQ);
  m_->mode_query_works_ = rig_get_function_ptr (m_->model_, RIG_FUNCTION_GET_MODE);
  m_->split_query_works_ = rig_get_function_ptr (m_->model_, RIG_FUNCTION_GET_SPLIT_VFO);
  m_->tickle_hamlib_ = false;
  m_->get_vfo_works_ = true;
  m_->set_vfo_works_ = true;

  // the Net rigctl back end promises all functions work but we must
  // test get_vfo as it determines our strategy for Icom rigs
  vfo_t vfo;
  int rc = rig_get_vfo (m_->rig_.data (), &vfo);
  if (-RIG_ENAVAIL == rc || -RIG_ENIMPL == rc)
    {
      m_->get_vfo_works_ = false;
      // determine if the rig uses single VFO addressing i.e. A/B and
      // no get_vfo function
      if (m_->rig_->state.vfo_list & RIG_VFO_B)
        {
          m_->one_VFO_ = true;
        }
    }
  else
    {
      m_->error_check (rc, "testing getting current VFO");
    }

  if ((WSJT_RIG_NONE_CAN_SPLIT || !m_->is_dummy_)
      && rig_get_function_ptr (m_->model_, RIG_FUNCTION_SET_SPLIT_VFO)) // if split is possible do some extra setup
    {
      freq_t f1;
      freq_t f2;
      rmode_t m {RIG_MODE_USB};
      rmode_t mb;
      pbwidth_t w {RIG_PASSBAND_NORMAL};
      pbwidth_t wb;
      if (m_->freq_query_works_
          && (!m_->get_vfo_works_ || !rig_get_function_ptr (m_->model_, RIG_FUNCTION_GET_VFO)))
        {
          // Icom have deficient CAT protocol with no way of reading which
          // VFO is selected or if SPLIT is selected so we have to simply
          // assume it is as when we started by setting at open time right
          // here. We also gather/set other initial state.
          m_->error_check (rig_get_freq (m_->rig_.data (), RIG_VFO_CURR, &f1), tr ("getting current frequency"));
          f1 = std::round (f1);
          CAT_TRACE ("current frequency=" << f1);

          m_->error_check (rig_get_mode (m_->rig_.data (), RIG_VFO_CURR, &m, &w), tr ("getting current mode"));
          CAT_TRACE ("current mode=" << rig_strrmode (m) << " bw=" << w);

          if (!rig_get_function_ptr (m_->model_, RIG_FUNCTION_SET_VFO))
            {
              CAT_TRACE ("rig_vfo_op TOGGLE");
              rc = rig_vfo_op (m_->rig_.data (), RIG_VFO_CURR, RIG_OP_TOGGLE);
            }
          else
            {
              CAT_TRACE ("rig_set_vfo to other VFO");
              rc = rig_set_vfo (m_->rig_.data (), m_->rig_->state.vfo_list & RIG_VFO_B ? RIG_VFO_B : RIG_VFO_SUB);
              if (-RIG_ENAVAIL == rc || -RIG_ENIMPL == rc)
                {
                  // if we are talking to netrigctl then toggle VFO op
                  // may still work
                  CAT_TRACE ("rig_vfo_op TOGGLE");
                  rc = rig_vfo_op (m_->rig_.data (), RIG_VFO_CURR, RIG_OP_TOGGLE);
                }
            }
          if (-RIG_ENAVAIL == rc || -RIG_ENIMPL == rc)
            {
              // we are probably dealing with rigctld so we do not
              // have completely accurate rig capabilities
              m_->set_vfo_works_ = false;
              m_->one_VFO_ = false; // we do not need single VFO addressing
            }
          else
            {
              m_->error_check (rc, tr ("exchanging VFOs"));
            }

          if (m_->set_vfo_works_)
            {
              // without the above we cannot proceed but we know we
              // are on VFO A and that will not change so there's no
              // need to execute this block
              m_->error_check (rig_get_freq (m_->rig_.data (), RIG_VFO_CURR, &f2), tr ("getting other VFO frequency"));
              f2 = std::round (f2);
              CAT_TRACE ("rig_get_freq other frequency=" << f2);

              m_->error_check (rig_get_mode (m_->rig_.data (), RIG_VFO_CURR, &mb, &wb), tr ("getting other VFO mode"));
              CAT_TRACE ("rig_get_mode other mode=" << rig_strrmode (mb) << " bw=" << wb);

              update_other_frequency (f2);

              if (!rig_get_function_ptr (m_->model_, RIG_FUNCTION_SET_VFO))
                {
                  CAT_TRACE ("rig_vfo_op TOGGLE");
                  m_->error_check (rig_vfo_op (m_->rig_.data (), RIG_VFO_CURR, RIG_OP_TOGGLE), tr ("exchanging VFOs"));
                }
              else
                {
                  CAT_TRACE ("rig_set_vfo A/MAIN");
                  m_->error_check (rig_set_vfo (m_->rig_.data (), m_->rig_->state.vfo_list & RIG_VFO_A ? RIG_VFO_A : RIG_VFO_MAIN), tr ("setting current VFO"));
                }

              if (f1 != f2 || m != mb || w != wb)	// we must have started with MAIN/A
                {
                  update_rx_frequency (f1);
                }
              else
                {
                  m_->error_check (rig_get_freq (m_->rig_.data (), RIG_VFO_CURR, &f1), tr ("getting frequency"));
                  f1 = std::round (f1);
                  CAT_TRACE ("rig_get_freq frequency=" << f1);

                  m_->error_check (rig_get_mode (m_->rig_.data (), RIG_VFO_CURR, &m, &w), tr ("getting mode"));
                  CAT_TRACE ("rig_get_mode mode=" << rig_strrmode (m) << " bw=" << w);

                  update_rx_frequency (f1);
                }
            }

          // TRACE_CAT ("rig_set_split_vfo split off");
          // m_->error_check (rig_set_split_vfo (m_->rig_.data (), RIG_VFO_CURR, RIG_SPLIT_OFF, RIG_VFO_CURR), tr ("setting split off"));
          // update_split (false);
        }
      else
        {
          vfo_t v {RIG_VFO_A};  // assume RX always on VFO A/MAIN

          if (m_->get_vfo_works_ && rig_get_function_ptr (m_->model_, RIG_FUNCTION_GET_VFO))
            {
              m_->error_check (rig_get_vfo (m_->rig_.data (), &v), tr ("getting current VFO")); // has side effect of establishing current VFO inside hamlib
              CAT_TRACE ("rig_get_vfo current VFO=" << rig_strvfo (v));
            }

          m_->reversed_ = RIG_VFO_B == v;

          if (m_->mode_query_works_ && !(rig_get_caps_int (m_->model_, RIG_CAPS_TARGETABLE_VFO) & (RIG_TARGETABLE_MODE | RIG_TARGETABLE_PURE)))
            {
              if (RIG_OK == rig_get_mode (m_->rig_.data (), RIG_VFO_CURR, &m, &w))
                {
                  CAT_TRACE ("rig_get_mode current mode=" << rig_strrmode (m) << " bw=" << w);
                }
              else
                {
                  m_->mode_query_works_ = false;
                  // Some rigs (HDSDR) don't have a working way of
                  // reporting MODE so we give up on mode queries -
                  // sets will still cause an error
                  CAT_TRACE ("rig_get_mode can't do on this rig");
                }
            }
        }
      update_mode (m_->map_mode (m));
    }

  m_->tickle_hamlib_ = true;

  if (m_->is_dummy_ && !m_->ptt_only_ && impl::dummy_frequency_)
    {
      // return to where last dummy instance was
      // TODO: this is going to break down if multiple dummy rigs are used
      rig_set_freq (m_->rig_.data (), RIG_VFO_CURR, impl::dummy_frequency_);
      update_rx_frequency (impl::dummy_frequency_);
      if (RIG_MODE_NONE != impl::dummy_mode_)
        {
          rig_set_mode (m_->rig_.data (), RIG_VFO_CURR, impl::dummy_mode_, RIG_PASSBAND_NOCHANGE);
          update_mode (m_->map_mode (impl::dummy_mode_));
        }
    }

#if HAVE_HAMLIB_CACHING || HAVE_HAMLIB_OLD_CACHING
  // we must disable Hamlib caching because it lies about frequency
  // for less than 1 Hz resolution rigs
  auto orig_cache_timeout = rig_get_cache_timeout_ms (m_->rig_.data (), HAMLIB_CACHE_ALL);
  rig_set_cache_timeout_ms (m_->rig_.data (), HAMLIB_CACHE_ALL, 0);
#endif

  int resolution {0};
  if (m_->freq_query_works_)
    {
      freq_t current_frequency;
      m_->error_check (rig_get_freq (m_->rig_.data (), RIG_VFO_CURR, &current_frequency), tr ("getting current VFO frequency"));
      current_frequency = std::round (current_frequency);
      Frequency f = current_frequency;
      if (f && !(f % 10))
        {
          auto test_frequency = f - f % 100 + 55;
          m_->error_check (rig_set_freq (m_->rig_.data (), RIG_VFO_CURR, test_frequency), tr ("setting frequency"));
          freq_t new_frequency;
          m_->error_check (rig_get_freq (m_->rig_.data (), RIG_VFO_CURR, &new_frequency), tr ("getting current VFO frequency"));
          new_frequency = std::round (new_frequency);
          switch (static_cast<Radio::FrequencyDelta> (new_frequency - test_frequency))
            {
            case -5: resolution = -1; break;  // 10Hz truncated
            case 5: resolution = 1; break;    // 10Hz rounded
            case -15: resolution = -2; break; // 20Hz truncated
            case -55: resolution = -3; break; // 100Hz truncated
            case 45: resolution = 3; break;   // 100Hz rounded
            }
          if (1 == resolution)      // may be 20Hz rounded
            {
              test_frequency = f - f % 100 + 51;
              m_->error_check (rig_set_freq (m_->rig_.data (), RIG_VFO_CURR, test_frequency), tr ("setting frequency"));
              m_->error_check (rig_get_freq (m_->rig_.data (), RIG_VFO_CURR, &new_frequency), tr ("getting current VFO frequency"));
              if (9 == static_cast<Radio::FrequencyDelta> (new_frequency - test_frequency))
                {
                  resolution = 2;   // 20Hz rounded
                }
            }
          m_->error_check (rig_set_freq (m_->rig_.data (), RIG_VFO_CURR, current_frequency), tr ("setting frequency"));
        }
    }
  else
    {
      resolution = -1;          // best guess
    }

#if HAVE_HAMLIB_CACHING || HAVE_HAMLIB_OLD_CACHING
  // revert Hamlib cache timeout
  rig_set_cache_timeout_ms (m_->rig_.data (), HAMLIB_CACHE_ALL, orig_cache_timeout);
#endif

  do_poll ();

  CAT_TRACE ("finished start " << state () << " reversed=" << m_->reversed_ << " resolution=" << resolution);
  return resolution;
}

void HamlibTransceiver::do_stop ()
{
  if (m_->is_dummy_ && !m_->ptt_only_)
    {
      rig_get_freq (m_->rig_.data (), RIG_VFO_CURR, &impl::dummy_frequency_);
      impl::dummy_frequency_ = std::round (impl::dummy_frequency_);
      if (m_->mode_query_works_)
        {
          pbwidth_t width;
          rig_get_mode (m_->rig_.data (), RIG_VFO_CURR, &impl::dummy_mode_, &width);
        }
    }
  if (m_->rig_)
    {
      rig_close (m_->rig_.data ());
    }

  CAT_TRACE ("state: " << state () << " reversed=" << m_->reversed_);
}

void HamlibTransceiver::do_frequency (Frequency f, MODE m, bool no_ignore)
{
  CAT_TRACE ("f: " << f << " mode: " << m << " reversed: " << m_->reversed_);

  // only change when receiving or simplex or direct VFO addressing
  // unavailable or forced
  if (!state ().ptt () || !state ().split () || !m_->one_VFO_ || no_ignore)
    {
      // for the 1st time as a band change may cause a recalled mode to be
      // set
      vfo_t target_vfo = RIG_VFO_CURR;
      if (!(m_->rig_->state.vfo_list & RIG_VFO_B))
        {
          target_vfo = RIG_VFO_MAIN; // no VFO A/B so force to Rx on MAIN
        }
      m_->error_check (rig_set_freq (m_->rig_.data (), target_vfo, f), tr ("setting frequency"));
      update_rx_frequency (f);

      if (m_->mode_query_works_ && UNK != m)
        {
          rmode_t current_mode;
          pbwidth_t current_width;
          auto new_mode = m_->map_mode (m);
          m_->error_check (rig_get_mode (m_->rig_.data (), target_vfo, &current_mode, &current_width), tr ("getting current VFO mode"));
          CAT_TRACE ("rig_get_mode mode=" << rig_strrmode (current_mode) << " bw=" << current_width);

          if (new_mode != current_mode)
            {
              CAT_TRACE ("rig_set_mode mode=" << rig_strrmode (new_mode));
              m_->error_check (rig_set_mode (m_->rig_.data (), target_vfo, new_mode, RIG_PASSBAND_NOCHANGE), tr ("setting current VFO mode"));

              // for the 2nd time because a mode change may have caused a
              // frequency change
              m_->error_check (rig_set_freq (m_->rig_.data (), RIG_VFO_CURR, f), tr ("setting frequency"));

              // for the second time because some rigs change mode according
              // to frequency such as the TS-2000 auto mode setting
              CAT_TRACE ("rig_set_mode mode=" << rig_strrmode (new_mode));
              m_->error_check (rig_set_mode (m_->rig_.data (), target_vfo, new_mode, RIG_PASSBAND_NOCHANGE), tr ("setting current VFO mode"));
              // set mode on VFOB too if we are in split
              if (state ().split()) rig_set_mode (m_->rig_.data (), RIG_VFO_B, new_mode, RIG_PASSBAND_NOCHANGE), tr ("setting VFOB mode");
            }
          update_mode (m);
        }
    }
}

void HamlibTransceiver::do_tx_frequency (Frequency tx, MODE mode, bool no_ignore)
{
  CAT_TRACE ("txf: " << tx << " reversed: " << m_->reversed_);

  if (WSJT_RIG_NONE_CAN_SPLIT || !m_->is_dummy_) // split is meaningless if you can't see it
    {
      auto split = tx ? RIG_SPLIT_ON : RIG_SPLIT_OFF;
      auto vfos = m_->get_vfos (tx);
      // auto rx_vfo = std::get<0> (vfos); // or use RIG_VFO_CURR
      auto tx_vfo = std::get<1> (vfos);

      if (tx)
        {
          // Doing set split for the 1st of two times, this one
          // ensures that the internal Hamlib state is correct
          // otherwise rig_set_split_freq() will target the wrong VFO
          // on some rigs

          if (m_->tickle_hamlib_)
            {
              // This potentially causes issues with the Elecraft K3
              // which will block setting split mode when it deems
              // cross mode split operation not possible. There's not
              // much we can do since the Hamlib Library needs this
              // call at least once to establish the Tx VFO. Best we
              // can do is only do this once per session.
              CAT_TRACE ("rig_set_split_vfo split=" << split);
              auto rc = rig_set_split_vfo (m_->rig_.data (), RIG_VFO_CURR, split, tx_vfo);
              if (tx || (-RIG_ENAVAIL != rc && -RIG_ENIMPL != rc))
                {
                  // On rigs that can't have split controlled only throw an
                  // exception when an error other than command not accepted
                  // is returned when trying to leave split mode. This allows
                  // fake split mode and non-split mode to work without error
                  // on such rigs without having to know anything about the
                  // specific rig.
                  m_->error_check (rc, tr ("setting/unsetting split mode"));
                }
              m_->tickle_hamlib_ = false;
              update_split (tx);
            }

          // just change current when transmitting with single VFO
          // addressing
          if (state ().ptt () && m_->one_VFO_)
            {
              CAT_TRACE ("rig_set_split_vfo split=" << split);
              m_->error_check (rig_set_split_vfo (m_->rig_.data (), RIG_VFO_CURR, split, tx_vfo), tr ("setting split mode"));

              m_->error_check (rig_set_freq (m_->rig_.data (), RIG_VFO_CURR, tx), tr ("setting frequency"));

              if (UNK != mode && m_->mode_query_works_)
                {
                  rmode_t current_mode;
                  pbwidth_t current_width;
                  auto new_mode = m_->map_mode (mode);
                  m_->error_check (rig_get_mode (m_->rig_.data (), RIG_VFO_CURR, &current_mode, &current_width), tr ("getting current VFO mode"));
                  CAT_TRACE ("rig_get_mode mode=" << rig_strrmode (current_mode) << " bw=" << current_width);

                  if (new_mode != current_mode)
                    {
                      CAT_TRACE ("rig_set_mode mode=" << rig_strrmode (new_mode));
                      m_->error_check (rig_set_mode (m_->rig_.data (), RIG_VFO_CURR, new_mode, RIG_PASSBAND_NOCHANGE), tr ("setting current VFO mode"));
                    }
                }
              update_other_frequency (tx);
            }
          else if (!m_->one_VFO_ || no_ignore)   // if not single VFO addressing and not forced
            {
              hamlib_tx_vfo_fixup fixup (m_->rig_.data (), tx_vfo);
              if (UNK != mode)
                {
                  auto new_mode = m_->map_mode (mode);
                  CAT_TRACE ("rig_set_split_freq_mode freq=" << tx
                             << " mode = " << rig_strrmode (new_mode));
                  m_->error_check (rig_set_split_freq_mode (m_->rig_.data (), RIG_VFO_CURR, tx, new_mode, RIG_PASSBAND_NOCHANGE), tr ("setting split TX frequency and mode"));
                }
              else
                {
                  CAT_TRACE ("rig_set_split_freq freq=" << tx);
                  m_->error_check (rig_set_split_freq (m_->rig_.data (), RIG_VFO_CURR, tx), tr ("setting split TX frequency"));
                }
              // Enable split last since some rigs (Kenwood for one) come out
              // of split when you switch RX VFO (to set split mode above for
              // example). Also the Elecraft K3 will refuse to go to split
              // with certain VFO A/B mode combinations.
              CAT_TRACE ("rig_set_split_vfo split=" << split);
              m_->error_check (rig_set_split_vfo (m_->rig_.data (), RIG_VFO_CURR, split, tx_vfo), tr ("setting split mode"));
              update_other_frequency (tx);
              update_split (tx);
            }
        }
      else
        {
          // Disable split
          CAT_TRACE ("rig_set_split_vfo split=" << split);
          auto rc = rig_set_split_vfo (m_->rig_.data (), RIG_VFO_CURR, split, tx_vfo);
          if (tx || (-RIG_ENAVAIL != rc && -RIG_ENIMPL != rc))
            {
              // On rigs that can't have split controlled only throw an
              // exception when an error other than command not accepted
              // is returned when trying to leave split mode. This allows
              // fake split mode and non-split mode to work without error
              // on such rigs without having to know anything about the
              // specific rig.
              m_->error_check (rc, tr ("setting/unsetting split mode"));
            }
          update_other_frequency (tx);
          update_split (tx);
        }
    }
}

void HamlibTransceiver::do_mode (MODE mode)
{
  CAT_TRACE (mode);

  auto vfos = m_->get_vfos (state ().split ());
  // auto rx_vfo = std::get<0> (vfos);
  auto tx_vfo = std::get<1> (vfos);

  rmode_t current_mode;
  pbwidth_t current_width;
  auto new_mode = m_->map_mode (mode);

  vfo_t target_vfo = RIG_VFO_CURR;
  if (!(m_->rig_->state.vfo_list & RIG_VFO_B))
    {
      target_vfo = RIG_VFO_MAIN; // no VFO A/B so force to Rx on MAIN
    }

  // only change when receiving or simplex if direct VFO addressing unavailable
  if (!(state ().ptt () && state ().split () && m_->one_VFO_))
    {
      m_->error_check (rig_get_mode (m_->rig_.data (), target_vfo, &current_mode, &current_width), tr ("getting current VFO mode"));
      CAT_TRACE ("rig_get_mode mode=" << rig_strrmode (current_mode) << " bw=" << current_width);

      if (new_mode != current_mode)
        {
          CAT_TRACE ("rig_set_mode mode=" << rig_strrmode (new_mode));
          m_->error_check (rig_set_mode (m_->rig_.data (), target_vfo, new_mode, RIG_PASSBAND_NOCHANGE), tr ("setting current VFO mode"));
        }
    }

  // just change current when transmitting split with one VFO mode
  if (state ().ptt () && state ().split () && m_->one_VFO_)
    {
      m_->error_check (rig_get_mode (m_->rig_.data (), RIG_VFO_CURR, &current_mode, &current_width), tr ("getting current VFO mode"));
      CAT_TRACE ("rig_get_mode mode=" << rig_strrmode (current_mode) << " bw=" << current_width);

      if (new_mode != current_mode)
        {
          CAT_TRACE ("rig_set_mode mode=" << rig_strrmode (new_mode));
          m_->error_check (rig_set_mode (m_->rig_.data (), RIG_VFO_CURR, new_mode, RIG_PASSBAND_NOCHANGE), tr ("setting current VFO mode"));
        }
    }
  else if (state ().split () && !m_->one_VFO_)
    {
      m_->error_check (rig_get_split_mode (m_->rig_.data (), RIG_VFO_CURR, &current_mode, &current_width), tr ("getting split TX VFO mode"));
      CAT_TRACE ("rig_get_split_mode mode=" << rig_strrmode (current_mode) << " bw=" << current_width);

      if (new_mode != current_mode)
        {
          CAT_TRACE ("rig_set_split_mode mode=" << rig_strrmode (new_mode));
          hamlib_tx_vfo_fixup fixup (m_->rig_.data (), tx_vfo);
          m_->error_check (rig_set_split_mode (m_->rig_.data (), RIG_VFO_CURR, new_mode, RIG_PASSBAND_NOCHANGE), tr ("setting split TX VFO mode"));
        }
    }
  update_mode (mode);
}

void HamlibTransceiver::do_poll ()
{
  freq_t f;
  rmode_t m;
  pbwidth_t w;
  split_t s;

  if (m_->get_vfo_works_ && rig_get_function_ptr (m_->model_, RIG_FUNCTION_GET_VFO))
    {
      vfo_t v;
      m_->error_check (rig_get_vfo (m_->rig_.data (), &v), tr ("getting current VFO")); // has side effect of establishing current VFO inside hamlib
      CAT_TRACE ("VFO=" << rig_strvfo (v));
      m_->reversed_ = RIG_VFO_B == v;
    }

  if ((WSJT_RIG_NONE_CAN_SPLIT || !m_->is_dummy_)
      && rig_get_function_ptr (m_->model_, RIG_FUNCTION_GET_SPLIT_VFO) && m_->split_query_works_)
    {
      vfo_t v {RIG_VFO_NONE};		// so we can tell if it doesn't get updated :(
      auto rc = rig_get_split_vfo (m_->rig_.data (), RIG_VFO_CURR, &s, &v);
      if (-RIG_OK == rc && RIG_SPLIT_ON == s)
        {
          CAT_TRACE ("rig_get_split_vfo split=" << s << " VFO=" << rig_strvfo (v));
          update_split (true);
          // if (RIG_VFO_A == v)
          // 	{
          // 	  m_->reversed_ = true;	// not sure if this helps us here
          // 	}
        }
      else if (-RIG_OK == rc)	// not split
        {
          CAT_TRACE ("rig_get_split_vfo split=" << s << " VFO=" << rig_strvfo (v));
          update_split (false);
        }
      else
        {
          // Some rigs (Icom) don't have a way of reporting SPLIT
          // mode
          CAT_TRACE ("rig_get_split_vfo can't do on this rig");
          // just report how we see it based on prior commands
          m_->split_query_works_ = false;
        }
    }

  if (m_->freq_query_works_)
    {
      // only read if possible and when receiving or simplex
      if (!state ().ptt () || !state ().split ())
        {
          m_->error_check (rig_get_freq (m_->rig_.data (), RIG_VFO_CURR, &f), tr ("getting current VFO frequency"));
          f = std::round (f);
          CAT_TRACE ("rig_get_freq frequency=" << Radio::frequency (f));
          update_rx_frequency (f);
        }

      if ((WSJT_RIG_NONE_CAN_SPLIT || !m_->is_dummy_)
          && state ().split ()
          && (rig_get_caps_int (m_->model_, RIG_CAPS_TARGETABLE_VFO) & (RIG_TARGETABLE_FREQ | RIG_TARGETABLE_PURE))
          && !m_->one_VFO_)
        {
          // only read "other" VFO if in split, this allows rigs like
          // FlexRadio to work in Kenwood TS-2000 mode despite them
          // not having a FB; command

          // we can only probe current VFO unless rig supports reading
          // the other one directly because we can't glitch the Rx
          m_->error_check (rig_get_freq (m_->rig_.data ()
                                         , m_->reversed_
                                         ? (m_->rig_->state.vfo_list & RIG_VFO_A ? RIG_VFO_A : RIG_VFO_MAIN)
                                         : (m_->rig_->state.vfo_list & RIG_VFO_B ? RIG_VFO_B : RIG_VFO_SUB)
                                         , &f), tr ("getting other VFO frequency"));
          f = std::round (f);
          CAT_TRACE ("rig_get_freq other VFO=" << f);
          update_other_frequency (f);
        }
    }

  // only read when receiving or simplex if direct VFO addressing unavailable
  if ((!state ().ptt () || !state ().split ())
      && m_->mode_query_works_)
    {
      // We have to ignore errors here because Yaesu FTdx... rigs can
      // report the wrong mode when transmitting split with different
      // modes per VFO. This is unfortunate because that is exactly
      // what you need to do to get 4kHz Rx b.w and modulation into
      // the rig through the data socket or USB. I.e.  USB for Rx and
      // DATA-USB for Tx.
      auto rc = rig_get_mode (m_->rig_.data (), RIG_VFO_CURR, &m, &w);
      if (RIG_OK == rc)
        {
          CAT_TRACE ("rig_get_mode mode=" << rig_strrmode (m) << " bw=" << w);
          update_mode (m_->map_mode (m));
        }
      else
        {
          CAT_TRACE ("rig_get_mode mode failed with rc: " << rc << " ignoring");
        }
    }

  if (RIG_PTT_NONE != m_->rig_->state.pttport.type.ptt && rig_get_function_ptr (m_->model_, RIG_FUNCTION_GET_PTT))
    {
      ptt_t p;
      auto rc = rig_get_ptt (m_->rig_.data (), RIG_VFO_CURR, &p);
      if (-RIG_ENAVAIL != rc && -RIG_ENIMPL != rc) // may fail if
        // Net rig ctl and target doesn't
        // support command
        {
          m_->error_check (rc, tr ("getting PTT state"));
          CAT_TRACE ("rig_get_ptt PTT=" << p);
          update_PTT (!(RIG_PTT_OFF == p));
        }
    }
}

void HamlibTransceiver::do_ptt (bool on)
{
  CAT_TRACE ("PTT: " << on << " " << state () << " reversed=" << m_->reversed_);
  if (on)
    {
      if (RIG_PTT_NONE != m_->rig_->state.pttport.type.ptt)
        {
          CAT_TRACE ("rig_set_ptt PTT=true");
          auto ptt_type = rig_get_caps_int (m_->model_, RIG_CAPS_PTT_TYPE);
          m_->error_check (rig_set_ptt (m_->rig_.data (), RIG_VFO_CURR
                                        , RIG_PTT_RIG_MICDATA == ptt_type && m_->back_ptt_port_
                                        ? RIG_PTT_ON_DATA : RIG_PTT_ON), tr ("setting PTT on"));
        }
    }
  else
    {
      if (RIG_PTT_NONE != m_->rig_->state.pttport.type.ptt)
        {
          CAT_TRACE ("rig_set_ptt PTT=false");
          m_->error_check (rig_set_ptt (m_->rig_.data (), RIG_VFO_CURR, RIG_PTT_OFF), tr ("setting PTT off"));
        }
    }

  update_PTT (on);
}
