#ifndef TXSTARTPOLICY_HPP__
#define TXSTARTPOLICY_HPP__

#include <QString>

constexpr double TxStartLatestPeriodFraction = 0.75;

inline bool requires_standard_tx_message (QString const& mode)
{
  // These modes synthesize their Tx payload outside the standard Tx1-Tx6 slots.
  return mode != "Echo" && mode != "WSPR" && mode != "FST4W" && mode != "JTTY";
}

inline bool tx_payload_ready (QString const& mode, int standard_message_length)
{
  return !requires_standard_tx_message (mode) || standard_message_length > 0;
}

inline bool should_stop_for_missing_tx_payload (QString const& mode,
                                                int standard_message_length,
                                                bool tuning)
{
  return !tuning && !tx_payload_ready (mode, standard_message_length);
}

inline bool tx_start_window_open (QString const& mode, bool tx_time, double period_fraction)
{
  return mode == "JTTY"
    || (tx_time && period_fraction < TxStartLatestPeriodFraction);
}

inline bool can_start_transmit (QString const& mode, bool tx_time,
                                double period_fraction, int standard_message_length,
                                bool tuning)
{
  return tuning || (tx_start_window_open (mode, tx_time, period_fraction)
                    && tx_payload_ready (mode, standard_message_length));
}

inline bool should_block_generated_transmit (QString const& generated_message, bool tuning)
{
  return !tuning
    && generated_message.trimmed () == QStringLiteral ("*** bad message ***");
}

#endif
