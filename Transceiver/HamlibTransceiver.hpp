#ifndef HAMLIB_TRANSCEIVER_HPP_
#define HAMLIB_TRANSCEIVER_HPP_

#include <QString>

#include "TransceiverFactory.hpp"
#include "PollingTransceiver.hpp"
#include "pimpl_h.hpp"

class TxInhibitGate;

// hamlib transceiver and PTT mostly delegated directly to hamlib Rig class
class HamlibTransceiver final
  : public PollingTransceiver
{
  Q_OBJECT                      // for translation context

public:
  static void register_transceivers (logger_type *, TransceiverFactory::Transceivers *);
  static void unregister_transceivers ();

  explicit HamlibTransceiver (logger_type *, unsigned model_number, TransceiverFactory::ParameterPack const&,
                              QObject * parent = nullptr);
  // PTT-only helper used under DXLab/HRD/OmniRig wrappers (no CAT model).
  explicit HamlibTransceiver (logger_type *, TransceiverFactory::PTTMethod ptt_type, QString const& ptt_port,
                              bool enable_tx_inhibit = false, QObject * parent = nullptr);
  ~HamlibTransceiver ();

private:
  void load_user_settings ();
  int do_start () override;
  void do_stop () override;
  void do_frequency (Frequency, MODE, bool no_ignore) override;
  void do_tx_frequency (Frequency, MODE, bool no_ignore) override;
  void do_mode (MODE) override;
  void do_ptt (bool) override;
  void do_tune (bool) override;

  void do_poll () override;

  // Actual Hamlib pin/CAT PTT (used by stock path and by the inhibit gate).
  void apply_physical_ptt (bool radiate);
  void start_tx_inhibit_gate ();
  void stop_tx_inhibit_gate ();

  bool ptt_on_ = false;
  bool do_pwr_;
  bool do_pwr2_;
  bool do_swr_;
  // True when Settings "Enable TX Inhibit" is on and PTT method is RTS/DTR.
  bool use_tx_inhibit_ = false;
  TxInhibitGate * inhibit_gate_ = nullptr;

  class impl;
  pimpl<impl> m_;
};

#endif
