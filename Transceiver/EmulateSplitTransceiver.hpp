#ifndef EMULATE_SPLIT_TRANSCEIVER_HPP__
#define EMULATE_SPLIT_TRANSCEIVER_HPP__

#include <memory>

#include "Transceiver.hpp"

//
// Emulate Split Transceiver
//
// Helper decorator class that encapsulates  the emulation of split TX
// operation.
//
// Responsibilities
//
//  Delegates  all  but setting  of  other  (split) frequency  to  the
//  wrapped Transceiver instance. Also routes failure signals from the
//  wrapped Transceiver instance to this instances failure signal.
//
//  Intercepts status  updates from  the wrapped  Transceiver instance
//  and re-signals it with the emulated status.
//
//  Generates a status update signal if the other (split) frequency is
//  changed, this is necessary  since the wrapped transceiver instance
//  never receives other frequency changes.
//
class EmulateSplitTransceiver final
  : public Transceiver
{
  Q_OBJECT

public:
  // takes ownership of wrapped Transceiver
  explicit EmulateSplitTransceiver (logger_type *,
                                    std::unique_ptr<Transceiver> wrapped,
                                    QObject * parent = nullptr);

  void set (TransceiverState const&,
            unsigned sequence_number) noexcept override;

  // forward everything else to wrapped Transceiver
  void start (unsigned sequence_number) noexcept override {wrapped_->start (sequence_number);}
  void stop () noexcept override {wrapped_->stop ();}
  void receive_discontinuity (JttyReceiveReason reason) override
  { wrapped_->receive_discontinuity (reason); }
  void receive_stop_reason (JttyReceiveReason reason) override
  { wrapped_->receive_stop_reason (reason); }
  void requestContinuousReceiveDrain (quint64 requestId) override
  { wrapped_->requestContinuousReceiveDrain (requestId); }

private:
  void handle_update (TransceiverState const&, unsigned seqeunce_number);
  void handle_frequency_result (RigFrequency::Result const&);
  void handle_frequency_observation (RigFrequency::Evidence const&);

  std::unique_ptr<Transceiver> wrapped_;
  Frequency rx_frequency_;        // requested Rx frequency
  Frequency tx_frequency_;        // requested Tx frequency
  bool split_; // requested split state
  RigFrequency::Request emulated_tx_;
  RigFrequency::Request logical_rx_;
  RigFrequency::Request logical_tx_;
  RigFrequency::Request physical_request_;
  RigFrequency::Path physical_path_ = RigFrequency::Path::Rx;
};

#endif
