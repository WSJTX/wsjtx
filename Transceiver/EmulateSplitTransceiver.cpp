#include "EmulateSplitTransceiver.hpp"

#include "moc_EmulateSplitTransceiver.cpp"

EmulateSplitTransceiver::EmulateSplitTransceiver (logger_type * logger,
                                                  std::unique_ptr<Transceiver> wrapped,
                                                  QObject * parent)
  : Transceiver {logger, parent}
  , wrapped_ {std::move (wrapped)}
  , rx_frequency_ {0}
  , tx_frequency_ {0}
  , split_ {false}
{
  // Connect update signal of wrapped Transceiver object instance to ours.
  connect (wrapped_.get (), &Transceiver::update, this, &EmulateSplitTransceiver::handle_update);
  connect (wrapped_.get (), &Transceiver::frequency_result,
           this, &EmulateSplitTransceiver::handle_frequency_result);
  connect (wrapped_.get (), &Transceiver::frequency_observation,
           this, &EmulateSplitTransceiver::handle_frequency_observation);
  connect (wrapped_.get (), &Transceiver::mode_status, this, &Transceiver::mode_status);

  // Connect other signals of wrapped Transceiver object to our
  // parent matching signals.
  connect (wrapped_.get (), &Transceiver::resolution, this, [this] (int code) {
    frequency_resolution_ = RigFrequency::resolution_hz (code);
    Q_EMIT resolution (code);
  });
  connect (wrapped_.get (), &Transceiver::tciframeswritten, this, &Transceiver::tciframeswritten);
  connect (wrapped_.get (), &Transceiver::receiveAudio, this, &Transceiver::receiveAudio);
  connect (wrapped_.get (), &Transceiver::continuousAudioAvailable,
           this, &Transceiver::continuousAudioAvailable);
  connect (wrapped_.get (), &Transceiver::continuousReceptionStopped,
           this, &Transceiver::continuousReceptionStopped);
  connect (wrapped_.get (), &Transceiver::continuousReceiveDrained,
           this, &Transceiver::continuousReceiveDrained);
  connect (wrapped_.get (), &Transceiver::tci_mod_active, this, &Transceiver::tci_mod_active);
  connect (wrapped_.get (), &Transceiver::txSourceCommitted, this, &Transceiver::txSourceCommitted);
  connect (wrapped_.get (), &Transceiver::rawTxPlayoutSnapshot, this, &Transceiver::rawTxPlayoutSnapshot);
  connect (wrapped_.get (), &Transceiver::finished, this, &Transceiver::finished);
  connect (wrapped_.get (), &Transceiver::failure, this, &Transceiver::failure);
}

void EmulateSplitTransceiver::set (TransceiverState const& s, unsigned sequence_number) noexcept
{
#if WSJT_TRACE_CAT
  qDebug () << "EmulateSplitTransceiver::set: state:" << s << "#:" << sequence_number;
#endif
  // save for use in updates
  rx_frequency_ = s.frequency ();
  tx_frequency_ = s.tx_frequency ();
  split_ = s.split ();
  tx_requested_ = s.ptt () && split_;

  TransceiverState emulated_state {s};
  auto const rx = s.frequency_request (RigFrequency::Path::Rx);
  auto const tx = s.frequency_request (RigFrequency::Path::Tx);
  if (rx) logical_rx_ = rx;
  else if (logical_rx_.target != s.frequency ()) logical_rx_ = {};
  if (tx) logical_tx_ = tx;
  else if (logical_tx_.target != s.tx_frequency ()) logical_tx_ = {};
  emulated_state.frequency_request (RigFrequency::Path::Rx, logical_rx_);
  if (tx_requested_)
    {
      emulated_state.frequency (s.tx_frequency ());
      emulated_state.frequency_request (RigFrequency::Path::Rx, logical_tx_);
      if (rx)
        Q_EMIT frequency_result ({rx, RigFrequency::Outcome::Deferred,
                                 RigFrequency::Basis::Retained, 0});
    }
  else if (tx && !RigFrequency::same_operation (tx, emulated_tx_))
    {
      emulated_tx_ = tx;
      Q_EMIT frequency_result ({tx, RigFrequency::Outcome::Established,
                               RigFrequency::Basis::Emulated, tx.target});
    }
  emulated_state.frequency_request (RigFrequency::Path::Tx, {});
  physical_request_ = emulated_state.frequency_request (RigFrequency::Path::Rx);
  emulated_state.split (false);
  emulated_state.tx_frequency (0);
  wrapped_->set (emulated_state, sequence_number);
}

void EmulateSplitTransceiver::handle_frequency_result (RigFrequency::Result const& result)
{
  if (result.outcome == RigFrequency::Outcome::Established
      && result.basis != RigFrequency::Basis::Emulated
      && RigFrequency::same_operation (result.request, physical_request_))
    physical_path_ = result.request.path;
  Q_EMIT frequency_result (result);
}

void EmulateSplitTransceiver::handle_frequency_observation (RigFrequency::Evidence const& report)
{
  auto logical = report;
  if (!logical.operation && logical.path == RigFrequency::Path::Rx)
    {
      auto const matches_rx = RigFrequency::within_resolution (
        report.value, rx_frequency_, frequency_resolution_);
      // A manual QSY can discard the logical RX request. Its legacy restore
      // then has no result, so wait for RX evidence before changing paths.
      if (!tx_requested_ && !physical_request_ && rx_frequency_ && matches_rx
          && report.basis != RigFrequency::Basis::Retained)
        physical_path_ = RigFrequency::Path::Rx;
      logical.path = physical_path_;
    }
  Q_EMIT frequency_observation (logical);
}

void EmulateSplitTransceiver::handle_update (TransceiverState const& state,
                                             unsigned sequence_number)
{
#if WSJT_TRACE_CAT
  qDebug () << "EmulateSplitTransceiver::handle_update: from wrapped:" << state;
#endif

  if (state.split ())
    {
      Q_EMIT failure (tr ("Emulated split mode requires rig to be in simplex mode"));
    }
  else
    {
      TransceiverState new_state {state};
      // Fake split QSYs the rig to the TX dial before the PTT echo may
      // arrive. Keep that transient from being adopted as the RX dial.
      if (state.ptt () || (split_ && tx_frequency_
                           && state.frequency () == tx_frequency_
                           && state.frequency () != rx_frequency_))
        {
          new_state.frequency (rx_frequency_);
        }

      // These are always what was requested in prior set state operation
      new_state.tx_frequency (tx_frequency_);
      new_state.split (split_);

#if WSJT_TRACE_CAT
      qDebug () << "EmulateSplitTransceiver::handle_update: signalling:" << state;
#endif

      // signal emulated state
      Q_EMIT update (new_state, sequence_number);
    }
}
