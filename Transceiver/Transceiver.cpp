#include "Transceiver.hpp"

#include <ostream>

#include "moc_Transceiver.cpp"

Transceiver::Transceiver (logger_type * logger, QObject * parent)
  : QObject {parent}
  , logger_ {logger}
{
}

#if !defined (QT_NO_DEBUG_STREAM)
QDebug operator << (QDebug d, Transceiver::TransceiverState const& s)
{
  d.nospace ()
    << "Transceiver::TransceiverState(online: " << (s.online_ ? "yes" : "no")
    << " Frequency {" << s.rx_frequency_ << "Hz, " << s.tx_frequency_ << "Hz} " << s.mode_
    << "; SPLIT: " << (Transceiver::TransceiverState::Split::on == s.split_ ? "on" : Transceiver::TransceiverState::Split::off == s.split_ ? "off" : "unknown")
    << "; PTT: " << (s.ptt_ ? "on" : "off")
    << "; AUDIO: " << (s.audio_ ? "on" : "off")
    << "; TX_AUDIO: " << (s.tx_audio_ ? "on" : "off")
    << "; TUNE: " << (s.tune_ ? "on" : "off")
    << "; QUICK: " << (s.quick_ ? "on" : "off")
    << "; PERIOD: " << s.period_ << "sec."
    << "; BLOCKSIZE: " << s.blocksize_
    << "; SYMBOLSLENGTH: " << s.tx_request_.symbols_length
    << "; FRAMESPERSYMBOL: " << s.tx_request_.frames_per_symbol
    << "; TRFREQUENCY: " << s.tx_request_.frequency_hz << "Hz"
    << "; TONESPACING: " << s.tx_request_.tone_spacing
    << "; SYNCHRONIZE: " << (s.tx_request_.synchronize ? "on" : "off")
    << "; DBSNR: " << s.tx_request_.snr_db
    << "; TRPERIOD: " << s.tx_request_.tr_period_s << "sec."
    << "; SPREAD: " << s.spread_
    << "; NSYM: " << s.nsym_
    << "; VOLUME: " << s.volume_
    << "; TXVOLUME: " << s.txvolume_
    << "; LEVEL: " << s.level_ << "dBm"
    << "; POWER: " << s.power_ << "mWatts"
    << "; SWR: " << s.swr_
    << "; TX_SESSION: " << s.tx_request_.session_id.value ()
    << "; TX_GENERATION: " << s.tx_request_.generation.value ()
    << ")\n";
  return d.space (); 
}
#endif

std::ostream& operator << (std::ostream& os, Transceiver::MODE m)
{
  auto const& mo = Transceiver::staticMetaObject;                             \
  return os << mo.enumerator (mo.indexOfEnumerator ("MODE")).valueToKey (static_cast<int> (m)); \
}

std::ostream& operator << (std::ostream& os, Transceiver::TransceiverState const& s)
{
  return os
    << "Transceiver::TransceiverState(online: " << (s.online_ ? "yes" : "no")
    << " Frequency {" << s.rx_frequency_ << "Hz, " << s.tx_frequency_ << "Hz} Mode: " << s.mode_
    << "; SPLIT: " << (Transceiver::TransceiverState::Split::on == s.split_ ? "on" : Transceiver::TransceiverState::Split::off == s.split_ ? "off" : "unknown")
    << "; PTT: " << (s.ptt_ ? "on" : "off")
    << "; POWER: " << s.power_ << "mWatts"
    << "; SWR: " << s.swr_
    << "; TUNE: " << s.tune_
    << "; TX_SESSION: " << s.tx_request_.session_id.value ()
    << "; TX_GENERATION: " << s.tx_request_.generation.value ()
    << ")\n";
}

ENUM_QDATASTREAM_OPS_IMPL (Transceiver, MODE);

ENUM_CONVERSION_OPS_IMPL (Transceiver, MODE);

bool operator != (Transceiver::TransceiverState const& lhs, Transceiver::TransceiverState const& rhs)
{
  return lhs.online_ != rhs.online_
    || lhs.rx_frequency_ != rhs.rx_frequency_
    || lhs.tx_frequency_ != rhs.tx_frequency_
    || lhs.mode_ != rhs.mode_
    || lhs.split_ != rhs.split_
    || lhs.ptt_ != rhs.ptt_
    || lhs.audio_ != rhs.audio_
    || lhs.tx_audio_ != rhs.tx_audio_
    || lhs.tune_ != rhs.tune_
    || lhs.quick_ != rhs.quick_
    || lhs.period_ != rhs.period_
    || lhs.blocksize_ != rhs.blocksize_
    || lhs.tx_request_ != rhs.tx_request_
    || lhs.spread_ != rhs.spread_
    || lhs.nsym_ != rhs.nsym_
    || lhs.volume_ != rhs.volume_
    || lhs.txvolume_ != rhs.txvolume_
    || lhs.level_ != rhs.level_
    || lhs.power_ != rhs.power_
    || lhs.swr_ != rhs.swr_;
}

bool operator == (Transceiver::TransceiverState const& lhs, Transceiver::TransceiverState const& rhs)
{
  return !(lhs != rhs);
}
