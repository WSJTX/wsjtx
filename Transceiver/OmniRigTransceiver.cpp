#include "OmniRigTransceiver.hpp"

#include <QDebug>
#include <objbase.h>
#include <QThread>
#include <QEventLoop>

#include "qt_helpers.hpp"

#include "moc_OmniRigTransceiver.cpp"

namespace
{
  auto constexpr OmniRig_transceiver_one_name = "OmniRig Rig 1";
  auto constexpr OmniRig_transceiver_two_name = "OmniRig Rig 2";
}

auto OmniRigTransceiver::map_mode (OmniRig::RigParamX param) -> MODE
{
  if (param & OmniRig::PM_CW_U)
    {
      return CW_R;
    }
  else if (param & OmniRig::PM_CW_L)
    {
      return CW;
    }
  else if (param & OmniRig::PM_SSB_U)
    {
      return USB;
    }
  else if (param & OmniRig::PM_SSB_L)
    {
      return LSB;
    }
  else if (param & OmniRig::PM_DIG_U)
    {
      return DIG_U;
    }
  else if (param & OmniRig::PM_DIG_L)
    {
      return DIG_L;
    }
  else if (param & OmniRig::PM_AM)
    {
      return AM;
    }
  else if (param & OmniRig::PM_FM)
    {
      return FM;
    }
  CAT_ERROR ("unrecognized mode");
  throw_qstring (tr ("OmniRig: unrecognized mode"));
  return UNK;
}

OmniRig::RigParamX OmniRigTransceiver::map_mode (MODE mode)
{
  switch (mode)
    {
    case AM: return OmniRig::PM_AM;
    case CW: return OmniRig::PM_CW_L;
    case CW_R: return OmniRig::PM_CW_U;
    case USB: return OmniRig::PM_SSB_U;
    case LSB: return OmniRig::PM_SSB_L;
    case FSK: return OmniRig::PM_DIG_L;
    case FSK_R: return OmniRig::PM_DIG_U;
    case DIG_L: return OmniRig::PM_DIG_L;
    case DIG_U: return OmniRig::PM_DIG_U;
    case FM: return OmniRig::PM_FM;
    case DIG_FM: return OmniRig::PM_FM;
    default: break;
    }
  return OmniRig::PM_SSB_U; // quieten compiler grumble
}

void OmniRigTransceiver::register_transceivers (logger_type *,
                                                TransceiverFactory::Transceivers * registry,
                                                unsigned id1, unsigned id2)
{
  (*registry)[OmniRig_transceiver_one_name] = TransceiverFactory::Capabilities {
    id1
    , TransceiverFactory::Capabilities::none // COM isn't serial or network
    , true             // does PTT
    , false            // doesn't select mic/data (use OmniRig config file)
    , true             // can remote control RTS nd DTR
    , true             // asynchronous interface
  };
  (*registry)[OmniRig_transceiver_two_name] = TransceiverFactory::Capabilities {
    id2
    , TransceiverFactory::Capabilities::none // COM isn't serial or network
    , true             // does PTT
    , false            // doesn't select mic/data (use OmniRig config file)
    , true             // can remote control RTS nd DTR
    , true             // asynchronous interface
  };
}

OmniRigTransceiver::OmniRigTransceiver (logger_type * the_logger,
                                        std::unique_ptr<TransceiverBase> wrapped,
                                        RigNumber n, TransceiverFactory::PTTMethod ptt_type,
                                        QString const& ptt_port, QObject * parent)
  : TransceiverBase {the_logger, parent}
  , wrapped_ {std::move (wrapped)}
  , use_for_ptt_ {TransceiverFactory::PTT_method_CAT == ptt_type || ("CAT" == ptt_port && (TransceiverFactory::PTT_method_RTS == ptt_type || TransceiverFactory::PTT_method_DTR == ptt_type))}
  , ptt_type_ {ptt_type}
  , rig_number_ {n}
  , readable_params_ {0}
  , writable_params_ {0}
  , send_update_signal_ {false}
  , reversed_ {false}
{
  CoInitializeEx (nullptr, 0 /*COINIT_APARTMENTTHREADED*/); // required because Qt only does this for GUI thread
  CAT_TRACE ("constructed");
}

OmniRigTransceiver::~OmniRigTransceiver ()
{
  CAT_TRACE ("destroying");
  CoUninitialize ();
}

int OmniRigTransceiver::do_start ()
{
  CAT_TRACE ("starting");
  try
    {
      if (wrapped_) wrapped_->start (0);

      omni_rig_.reset (new OmniRig::OmniRigX {this});
      if (omni_rig_->isNull ())
        {
          CAT_ERROR ("failed to start COM server");
          throw_qstring (tr ("Failed to start OmniRig COM server"));
        }

      // COM/OLE exceptions get signaled
      connect (&*omni_rig_, SIGNAL (exception (int, QString, QString, QString)), this, SLOT (handle_COM_exception (int, QString, QString, QString)));

      // IOmniRigXEvent interface signals
      connect (&*omni_rig_, SIGNAL (VisibleChange ()), this, SLOT (handle_visible_change ()));
      connect (&*omni_rig_, SIGNAL (RigTypeChange (int)), this, SLOT (handle_rig_type_change (int)));
      connect (&*omni_rig_, SIGNAL (StatusChange (int)), this, SLOT (handle_status_change (int)));
      connect (&*omni_rig_, SIGNAL (ParamsChange (int, int)), this, SLOT (handle_params_change (int, int)));
      connect (&*omni_rig_
               , SIGNAL (CustomReply (int, QVariant const&, QVariant const&))
               , this, SLOT (handle_custom_reply (int, QVariant const&, QVariant const&)));

      CAT_INFO ("OmniRig s/w version: " << static_cast<quint16> (omni_rig_->SoftwareVersion () >> 16)
                << '.' << static_cast<quint16> (omni_rig_->SoftwareVersion () & 0xffff)
                << " i/f version: " << static_cast<int> (omni_rig_->InterfaceVersion () >> 8 & 0xff)
                << '.' << static_cast<int> (omni_rig_->InterfaceVersion () && 0xff));

      // fetch the interface of the RigX CoClass and instantiate a proxy object
      switch (rig_number_)
        {
        case One: rig_.reset (new OmniRig::RigX (omni_rig_->Rig1 ())); break;
        case Two: rig_.reset (new OmniRig::RigX (omni_rig_->Rig2 ())); break;
        }

      Q_ASSERT (rig_);
      Q_ASSERT (!rig_->isNull ());

      // COM/OLE exceptions get signaled
      connect (&*rig_, SIGNAL (exception (int, QString, QString, QString)), this, SLOT (handle_COM_exception (int, QString, QString, QString)));

      offline_timer_.reset (new QTimer); // instantiate here as constructor runs in wrong thread
      offline_timer_->setSingleShot (true);
      connect (offline_timer_.data (), &QTimer::timeout, [this] () {offline ("Rig went offline");});

      for (int i = 0; i < 5; ++i)
        {
          // leave some time for Omni-Rig to do its first poll
          QThread::msleep (250);
          if (OmniRig::ST_ONLINE == rig_->Status ())
            {
              break;
            }
        }

      if (OmniRig::ST_ONLINE != rig_->Status ())
        {
          CAT_ERROR ("rig not online");
          throw_qstring ("OmniRig: " + rig_->StatusStr ());
        }

      if (use_for_ptt_ && (TransceiverFactory::PTT_method_DTR == ptt_type_ || TransceiverFactory::PTT_method_RTS == ptt_type_))
        {
          // fetch the interface for the serial port if we need it for PTT
          port_.reset (new OmniRig::PortBits (rig_->PortBits ()));

          Q_ASSERT (port_);
          Q_ASSERT (!port_->isNull ());

          // COM/OLE exceptions get signaled
          connect (&*port_, SIGNAL (exception (int, QString, QString, QString)), this, SLOT (handle_COM_exception (int, QString, QString, QString)));

          CAT_TRACE ("OmniRig RTS state: " << port_->Rts ());

          // remove locking because it doesn't seem to work properly
          // if (!port_->Lock ()) // try to take exclusive use of the OmniRig serial port for PTT
          //   {
          //     CAT_WARNING ("Failed to get exclusive use of serial port for PTT from OmniRig");
          //   }

          // start off so we don't accidentally key the radio
          if (TransceiverFactory::PTT_method_DTR == ptt_type_)
            {
              port_->SetDtr (false);
            }
          else      // RTS
            {
              port_->SetRts (false);
            }
        }

      rig_type_ = rig_->RigType ();
      readable_params_ = rig_->ReadableParams ();
      writable_params_ = rig_->WriteableParams ();

      CAT_INFO (QString {"OmniRig initial rig type: %1 readable params=0x%2 writable params=0x%3 for rig %4"}
         .arg (rig_type_)
         .arg (readable_params_, 8, 16, QChar ('0'))
         .arg (writable_params_, 8, 16, QChar ('0'))
         .arg (rig_number_));
      update_rx_frequency (rig_->GetRxFrequency ());
      int resolution {0};
      if (OmniRig::PM_UNKNOWN == rig_->Vfo ()
          && (writable_params_ & (OmniRig::PM_VFOA | OmniRig::PM_VFOB))
          == (OmniRig::PM_VFOA | OmniRig::PM_VFOB))
        {
          // start with VFO A (probably MAIN) on rigs that we
          // can't query VFO but can set explicitly
          rig_->SetVfo (OmniRig::PM_VFOA);
        }
      auto f = state ().frequency ();
      if (f % 10) return resolution; // 1Hz resolution
      auto test_frequency = f - f % 100 + 55;
      if (OmniRig::PM_FREQ & writable_params_)
        {
          rig_->SetFreq (test_frequency);
        }
      else if (reversed_ && (OmniRig::PM_FREQB & writable_params_))
        {
          rig_->SetFreqB (test_frequency);
        }
      else if (!reversed_ && (OmniRig::PM_FREQA & writable_params_))
        {
          rig_->SetFreqA (test_frequency);
        }
      else
        {
          throw_qstring (tr ("OmniRig: don't know how to set rig frequency"));
        }
      switch (rig_->GetRxFrequency () - test_frequency)
        {
        case -5: resolution = -1; break;  // 10Hz truncated
        case 5: resolution = 1; break;    // 10Hz rounded
        case -15: resolution = -2; break; // 20Hz truncated
        case -55: resolution = -2; break; // 100Hz truncated
        case 45: resolution = 2; break;   // 100Hz rounded
        }
      if (1 == resolution)  // may be 20Hz rounded
        {
          test_frequency = f - f % 100 + 51;
          if (OmniRig::PM_FREQ & writable_params_)
            {
              rig_->SetFreq (test_frequency);
            }
          else if (reversed_ && (OmniRig::PM_FREQB & writable_params_))
            {
              rig_->SetFreqB (test_frequency);
            }
          else if (!reversed_ && (OmniRig::PM_FREQA & writable_params_))
            {
              rig_->SetFreqA (test_frequency);
            }
          if (9 == rig_->GetRxFrequency () - test_frequency)
            {
              resolution = 2;   // 20Hz rounded
            }
        }

      // For OmniRig v1.19 or later we need a delay between GetRxFrequency () and SetFreq (f),
      // otherwise rig QRG stays at f+55 Hz. 200 ms should do job for all modern transceivers.
      // However, with very slow rigs, QRG may still stay at f+55 Hz. Such rigs should use v1.18.
      // Due to the asynchronous nature of Omnirig commands, a better solution would be to implement
      // an event handler for OmniRig's OnParamChange event and read the frequency inside that handler.

      if (OmniRig::PM_FREQ & writable_params_)
        {
          QTimer::singleShot (200, [=] {
              rig_->SetFreq (f);
              });
        }
      else if (reversed_ && (OmniRig::PM_FREQB & writable_params_))
        {
          QTimer::singleShot (200, [=] {
              rig_->SetFreqB (f);
              });
        }
      else if (!reversed_ && (OmniRig::PM_FREQA & writable_params_))
        {
          QTimer::singleShot (200, [=] {
              rig_->SetFreqA (f);
              });
        }
      update_rx_frequency (f);
      CAT_TRACE ("started");

      return resolution;
    }
  catch (...)
    {
      CAT_TRACE ("start threw exception");
      throw;
    }
}

void OmniRigTransceiver::do_stop ()
{
  CAT_TRACE ("stopping");
  QThread::msleep (200);        // leave some time for pending
                                // commands at the server end

  offline_timer_.reset ();      // destroy here rather than in
                                // destructor as destructor runs in
                                // wrong thread

  if (port_ && !port_->isNull ())
    {
      // port_->Unlock ();   // release serial port
      port_->clear ();
      port_.reset ();
    }
  if (omni_rig_ && !omni_rig_->isNull ())
    {
      if (rig_ && !rig_->isNull ())
        {
          rig_->clear ();
          rig_.reset ();
          CAT_TRACE ("rig_ reset");
        }
      omni_rig_->clear ();
      omni_rig_.reset ();
    }
  
  if (wrapped_) wrapped_->stop ();

  CAT_TRACE ("stopped");
}

void OmniRigTransceiver::handle_COM_exception (int code, QString source, QString desc, QString help)
{
  CAT_ERROR ((QString::number (code) + " at " + source + ": " + desc + " (" + help + ')'));
  throw_qstring (tr ("OmniRig COM/OLE error: %1 at %2: %3 (%4)").arg (QString::number (code)).arg (source). arg (desc). arg (help));
}

void OmniRigTransceiver::handle_visible_change ()
{
  if (!omni_rig_ || omni_rig_->isNull ()) return;
  CAT_TRACE ("visibility change: visibility =" << omni_rig_->DialogVisible ());
}

void OmniRigTransceiver::handle_rig_type_change (int rig_number)
{
  CAT_TRACE ("rig type change: rig =" << rig_number);
  if (rig_number_ == rig_number)
    {
      if (!rig_ || rig_->isNull ()) return;
      readable_params_ = rig_->ReadableParams ();
      writable_params_ = rig_->WriteableParams ();
      CAT_INFO (QString {"rig type change to: %1 readable params = 0x%2 writable params = 0x%3 for rig %4"}
        .arg (rig_->RigType ())
        .arg (readable_params_, 8, 16, QChar ('0'))
        .arg (writable_params_, 8, 16, QChar ('0'))
        .arg (rig_number));
    }
}

void OmniRigTransceiver::handle_status_change (int rig_number)
{
  CAT_TRACE (QString {"status change for rig %1"}.arg (rig_number));
  if (rig_number_ == rig_number)
    {
      if (!rig_ || rig_->isNull ()) return;
      auto const& status = rig_->StatusStr ();
      CAT_TRACE ("OmniRig status change: new status = " << status);
      if (OmniRig::ST_ONLINE != rig_->Status ())
        {
          if (!offline_timer_->isActive ())
            {
              // Omni-Rig is prone to reporting the rig offline and
              // then recovering autonomously, so we will give it a
              // few seconds to make its mind up
              offline_timer_->start (10000);
            }
        }
      else
        {
          offline_timer_->stop (); // good to go again
        }
      // else
      //   {
      //     update_rx_frequency (rig_->GetRxFrequency ());
      //     update_complete ();
      //     CAT_TRACE ("frequency:" << state ().frequency ());
      //   }
    }
}

void OmniRigTransceiver::handle_params_change (int rig_number, int params)
{
  CAT_TRACE (QString {"params change: params=0x%1 for rig %2"}
        .arg (params, 8, 16, QChar ('0'))
        .arg (rig_number)
        << "state before:" << state ());
  if (rig_number_ == rig_number)
    {
      if (!rig_ || rig_->isNull ()) return;
      //      starting_ = false;
      TransceiverState old_state {state ()};
      auto need_frequency = false;

      if (params & OmniRig::PM_VFOAA)
        {
          CAT_TRACE ("VFOAA");
          update_split (false);
          reversed_ = false;
          update_rx_frequency (rig_->FreqA ());
          update_other_frequency (rig_->FreqB ());
        }
      if (params & OmniRig::PM_VFOAB)
        {
          CAT_TRACE ("VFOAB");
          update_split (true);
          reversed_ = false;
          update_rx_frequency (rig_->FreqA ());
          update_other_frequency (rig_->FreqB ());
        }
      if (params & OmniRig::PM_VFOBA)
        {
          CAT_TRACE ("VFOBA");
          update_split (true);
          reversed_ = true;
          update_other_frequency (rig_->FreqA ());
          update_rx_frequency (rig_->FreqB ());
        }
      if (params & OmniRig::PM_VFOBB)
        {
          CAT_TRACE ("VFOBB");
          update_split (false);
          reversed_ = true;
          update_other_frequency (rig_->FreqA ());
          update_rx_frequency (rig_->FreqB ());
        }
      if (params & OmniRig::PM_VFOA)
        {
          CAT_TRACE ("VFOA");
          reversed_ = false;
          need_frequency = true;
        }
      if (params & OmniRig::PM_VFOB)
        {
          CAT_TRACE ("VFOB");
          reversed_ = true;
          need_frequency = true;
        }

      if (params & OmniRig::PM_FREQ)
        {
          need_frequency = true;
        }
      if (params & OmniRig::PM_FREQA)
        {
          auto f = rig_->FreqA ();
          CAT_TRACE ("FREQA = " << f);
          if (reversed_)
            {
              update_other_frequency (f);
            }
          else
            {
              update_rx_frequency (f);
            }
        }
      if (params & OmniRig::PM_FREQB)
        {
          auto f = rig_->FreqB ();
          CAT_TRACE ("FREQB = " << f);
          if (reversed_)
            {
              update_rx_frequency (f);
            }
          else
            {
              update_other_frequency (f);
            }
        }
      if (need_frequency)
        {
          if (readable_params_ & OmniRig::PM_FREQA)
            {
              auto f = rig_->FreqA ();
              if (f)
                {
                  CAT_TRACE ("FREQA = " << f);
                  if (reversed_)
                    {
                      update_other_frequency (f);
                    }
                  else
                    {
                      update_rx_frequency (f);
                    }
                }
            }
          if (readable_params_ & OmniRig::PM_FREQB)
            {
              auto f = rig_->FreqB ();
              if (f)
                {
                  CAT_TRACE ("FREQB = " << f);
                  if (reversed_)
                    {
                      update_rx_frequency (f);
                    }
                  else
                    {
                      update_other_frequency (f);
                    }
                }
            }
          if (readable_params_ & OmniRig::PM_FREQ && !state ().ptt ())
            {
              auto f = rig_->Freq ();
              if (f)
                {
                  CAT_TRACE ("FREQ = " << f);
                  update_rx_frequency (f);
                }
            }
        }
      if (params & OmniRig::PM_PITCH)
        {
          CAT_TRACE ("PITCH");
        }
      if (params & OmniRig::PM_RITOFFSET)
        {
          CAT_TRACE ("RITOFFSET");
        }
      if (params & OmniRig::PM_RIT0)
        {
          CAT_TRACE ("RIT0");
        }
      if (params & OmniRig::PM_VFOEQUAL)
        {
          auto f = readable_params_ & OmniRig::PM_FREQA ? rig_->FreqA () : rig_->Freq ();
          auto m = map_mode (rig_->Mode ());
          CAT_TRACE (QString {"VFOEQUAL f=%1 m=%2"}.arg (f).arg (m));
          update_rx_frequency (f);
          update_other_frequency (f);
          update_mode (m);
        }
      if (params & OmniRig::PM_VFOSWAP)
        {
          CAT_TRACE ("VFOSWAP");
          auto f = state ().tx_frequency ();
          update_other_frequency (state ().frequency ());
          update_rx_frequency (f);
          update_mode (map_mode (rig_->Mode ()));
        }
      if (params & OmniRig::PM_SPLITON)
        {
          CAT_TRACE ("SPLITON");
          update_split (true);
        }
      if (params & OmniRig::PM_SPLITOFF)
        {
          CAT_TRACE ("SPLITOFF");
          update_split (false);
        }
      if (params & OmniRig::PM_RITON)
        {
          CAT_TRACE ("RITON");
        }
      if (params & OmniRig::PM_RITOFF)
        {
          CAT_TRACE ("RITOFF");
        }
      if (params & OmniRig::PM_XITON)
        {
          CAT_TRACE ("XITON");
        }
      if (params & OmniRig::PM_XITOFF)
        {
          CAT_TRACE ("XITOFF");
        }
      if (params & OmniRig::PM_RX)
        {
          CAT_TRACE ("RX");
          update_PTT (false);
        }
      if (params & OmniRig::PM_TX)
        {
          CAT_TRACE ("TX");
          update_PTT ();
        }
      if (params & OmniRig::PM_CW_U)
        {
          CAT_TRACE ("CW-R");
          update_mode (CW_R);
        }
      if (params & OmniRig::PM_CW_L)
        {
          CAT_TRACE ("CW");
          update_mode (CW);
        }
      if (params & OmniRig::PM_SSB_U)
        {
          CAT_TRACE ("USB");
          update_mode (USB);
        }
      if (params & OmniRig::PM_SSB_L)
        {
          CAT_TRACE ("LSB");
          update_mode (LSB);
        }
      if (params & OmniRig::PM_DIG_U)
        {
          CAT_TRACE ("DATA-U");
          update_mode (DIG_U);
        }
      if (params & OmniRig::PM_DIG_L)
        {
          CAT_TRACE ("DATA-L");
          update_mode (DIG_L);
        }
      if (params & OmniRig::PM_AM)
        {
          CAT_TRACE ("AM");
          update_mode (AM);
        }
      if (params & OmniRig::PM_FM)
        {
          CAT_TRACE ("FM");
          update_mode (FM);
        }

      if (old_state != state () || send_update_signal_)
        {
          update_complete ();
          send_update_signal_ = false;
        }
      CAT_TRACE ("OmniRig params change: state after:" << state ());
    }
}

void OmniRigTransceiver::handle_custom_reply (int rig_number, QVariant const& command, QVariant const& reply)
{
  (void)command;
  (void)reply;

  if (rig_number_ == rig_number)
    {
      if (!rig_ || rig_->isNull ()) return;
      CAT_TRACE ("custom command" << command.toString ()
                 << "with reply" << reply.toString ()
                 << QString ("for rig %1").arg (rig_number));
      CAT_TRACE ("rig number:" << rig_number_ << ':' << state ());
    }
}

void OmniRigTransceiver::do_ptt (bool on)
{
  CAT_TRACE (on << state ());
  if (use_for_ptt_ && TransceiverFactory::PTT_method_CAT == ptt_type_)
    {
      CAT_TRACE ("set PTT");
      if (rig_ && !rig_->isNull ())
        {
          rig_->SetTx (on ? OmniRig::PM_TX : OmniRig::PM_RX);
        }
    }
  else
    {
      if (port_ && !port_->isNull ())
        {
          if (TransceiverFactory::PTT_method_RTS == ptt_type_)
            {
              CAT_TRACE ("set RTS");
              port_->SetRts (on);
            }
          else      // "DTR"
            {
              CAT_TRACE ("set DTR");
              port_->SetDtr (on);
            }
        }
      else if (wrapped_)
        {
          CAT_TRACE ("set PTT using basic transceiver");
          TransceiverState new_state {wrapped_->state ()};
          new_state.ptt (on);
          wrapped_->set (new_state, 0);
        }
    }
  update_PTT (on);
}

void OmniRigTransceiver::do_frequency (Frequency f, MODE m, bool /*no_ignore*/)
{
  CAT_TRACE (f << ' ' << state ());
  if (!rig_ || rig_->isNull ()) return;
  if (UNK != m)
    {
      do_mode (m);
    }
  if (OmniRig::PM_FREQ & writable_params_)
    {
      rig_->SetFreq (f);
      update_rx_frequency (f);
    }
  else if (reversed_ && (OmniRig::PM_FREQB & writable_params_))
    {
      rig_->SetFreqB (f);
      update_rx_frequency (f);
    }
  else if (!reversed_ && (OmniRig::PM_FREQA & writable_params_))
    {
      rig_->SetFreqA (f);
      update_rx_frequency (f);
    }
  else
    {
      throw_qstring (tr ("OmniRig: don't know how to set rig frequency"));
    }
}

void OmniRigTransceiver::do_tx_frequency (Frequency tx, MODE m, bool /*no_ignore*/)
{
  CAT_TRACE (tx << ' ' << state ());
  if (!rig_ || rig_->isNull ()) return;
  bool split {tx != 0};
  if (split)
    {
      if (UNK != m)
        {
          do_mode (m);
          if (OmniRig::PM_UNKNOWN == rig_->Vfo ())
            {
              if (writable_params_ & OmniRig::PM_VFOEQUAL)
                {
                  // nothing to do here because OmniRig will use VFO
                  // equalize to set the mode of the Tx VFO for us
                }
              else if ((writable_params_ & (OmniRig::PM_VFOA | OmniRig::PM_VFOB))
                   == (OmniRig::PM_VFOA | OmniRig::PM_VFOB))
                {
                  rig_->SetVfo (OmniRig::PM_VFOB);
                  do_mode (m);
                  rig_->SetVfo (OmniRig::PM_VFOA);
                }
              else if (writable_params_ & OmniRig::PM_VFOSWAP)
                {
                  rig_->SetVfo (OmniRig::PM_VFOSWAP);
                  do_mode (m);
                  rig_->SetVfo (OmniRig::PM_VFOSWAP);
                }
            }
        }
      CAT_TRACE ("set SPLIT mode on");
      rig_->SetSplitMode (state ().frequency (), tx);
      update_other_frequency (tx);
      update_split (true);
    }
  else
    {
      CAT_TRACE ("set SPLIT mode off");
      rig_->SetSimplexMode (state ().frequency ());
      update_split (false);
    }
  bool notify {false};
  if (readable_params_ & OmniRig::PM_FREQ || !(readable_params_ & (OmniRig::PM_FREQA | OmniRig::PM_FREQB)))
    {
      update_other_frequency (tx); // async updates won't return this
      // so just store it and hope
      // operator doesn't change the
      // "back" VFO on rig
      notify = true;
    }
  if (!((OmniRig::PM_VFOAB | OmniRig::PM_VFOBA | OmniRig::PM_SPLITON) & readable_params_))
    {
      CAT_TRACE ("setting SPLIT manually");
      update_split (split); // we can't read it so just set and
      // hope op doesn't change it
      notify = true;
    }
  if (notify)
    {
      update_complete ();
    }
}

void OmniRigTransceiver::do_mode (MODE mode)
{
  CAT_TRACE (mode << ' ' << state ());
  if (!rig_ || rig_->isNull ()) return;
  // TODO: G4WJS OmniRig doesn't seem to have any capability of tracking/setting VFO B mode
  auto mapped = map_mode (mode);
  if (mapped & writable_params_)
    {
      rig_->SetMode (mapped);
      update_mode (mode);
    }
  else
    {
      offline ("OmniRig invalid mode");
    }
}
