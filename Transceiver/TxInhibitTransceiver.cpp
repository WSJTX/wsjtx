#include "TxInhibitTransceiver.hpp"

#include <algorithm>
#include <limits>

#include <QSet>
#include <QStringList>
#include <QTimer>

#include "moc_TxInhibitTransceiver.cpp"

namespace
{
  QString const overflow_hold_key;

  QString sanitize_holder (QString const& value)
  {
    QString result;
    result.reserve (std::min (value.size (), 64));
    for (auto const character : value.left (64))
      {
        if (character.isPrint ()) result.append (character);
      }
    return result.trimmed ();
  }
}

TxInhibitTransceiver::TxInhibitTransceiver (logger_type * logger,
                                            std::unique_ptr<Transceiver> wrapped)
  : Transceiver {logger, nullptr}
  , wrapped_ {std::move (wrapped)}
  , expiry_timer_ {new QTimer {this}}
{
  Q_ASSERT (wrapped_);

  expiry_timer_->setSingleShot (true);
  connect (expiry_timer_, &QTimer::timeout, this, &TxInhibitTransceiver::expire_holds);

  connect (wrapped_.get (), &Transceiver::resolution, this, &Transceiver::resolution);
  connect (wrapped_.get (), &Transceiver::tciframeswritten, this, &Transceiver::tciframeswritten);
  connect (wrapped_.get (), &Transceiver::receiveAudio, this, &Transceiver::receiveAudio);
  connect (wrapped_.get (), &Transceiver::tci_mod_active, this, &Transceiver::tci_mod_active);
  connect (wrapped_.get (), &Transceiver::txSourceCommitted, this, &Transceiver::txSourceCommitted);
  connect (wrapped_.get (), &Transceiver::rawTxPlayoutSnapshot, this, &Transceiver::rawTxPlayoutSnapshot);
  connect (wrapped_.get (), &Transceiver::jtty_drained, this, &Transceiver::jtty_drained);
  connect (wrapped_.get (), &Transceiver::jtty_enqueue_accepted, this, &Transceiver::jtty_enqueue_accepted);
  connect (wrapped_.get (), &Transceiver::jtty_enqueue_failed, this, &Transceiver::jtty_enqueue_failed);
  connect (wrapped_.get (), &Transceiver::update, this, &Transceiver::update);
  connect (wrapped_.get (), &Transceiver::finished, this, &Transceiver::finished);
  connect (wrapped_.get (), &Transceiver::failure, this, &Transceiver::failure);
}

void TxInhibitTransceiver::set (TransceiverState const& state,
                                unsigned sequence_number) noexcept
{
  requested_ = state;
  sequence_number_ = sequence_number;
  have_requested_state_ = true;
  apply_requested_state ();
}

void TxInhibitTransceiver::start (unsigned sequence_number) noexcept
{
  sequence_number_ = sequence_number;
  have_effective_state_ = false;
  monotonic_clock_.start ();
  started_ = true;
  emit_status ();
  wrapped_->start (sequence_number);
}

void TxInhibitTransceiver::stop () noexcept
{
  if (!started_) return;

  expiry_timer_->stop ();
  holds_.clear ();
  started_ = false;
  emit_status ();
  wrapped_->stop ();
  have_requested_state_ = false;
  have_effective_state_ = false;
}

void TxInhibitTransceiver::enqueue_jtty_pcm (QByteArray const& samples,
                                             TxAudioQueueEpoch epoch,
                                             qint64 enqueue_id) noexcept
{
  wrapped_->enqueue_jtty_pcm (samples, epoch, enqueue_id);
}

void TxInhibitTransceiver::clear_jtty_pcm (TxAudioQueueEpoch epoch) noexcept
{
  wrapped_->clear_jtty_pcm (epoch);
}

void TxInhibitTransceiver::apply_requested_state () noexcept
{
  if (!have_requested_state_ || !started_) return;

  auto effective = requested_;
  if (!holds_.isEmpty ())
    {
      effective.ptt (false);
      effective.tune (false);
    }

  auto const previous_ptt = have_effective_state_ && last_effective_.ptt ();
  auto final_sent = false;
  if (effective.ptt () != previous_ptt)
    {
      if (effective.ptt ())
        {
          auto prepare = effective;
          prepare.ptt (false);
          if (!have_effective_state_ || prepare != last_effective_)
            {
              wrapped_->set (prepare, sequence_number_);
            }
        }
      else
        {
          auto unkey = last_effective_;
          unkey.ptt (false);
          wrapped_->set (unkey, sequence_number_);
          final_sent = unkey == effective;
        }
    }
  if (!final_sent) wrapped_->set (effective, sequence_number_);
  last_effective_ = effective;
  have_effective_state_ = true;
}

void TxInhibitTransceiver::emit_status ()
{
  Q_EMIT statusChanged (started_, started_ && !holds_.isEmpty (),
                        holder_summary (), hold_rx_, release_rx_, expiries_, invalid_);
}

void TxInhibitTransceiver::tx_inhibit_invalid (quint64 count)
{
  if (!started_ || !count) return;
  invalid_ += static_cast<quint32> (count);
  emit_status ();
}

void TxInhibitTransceiver::expire_holds ()
{
  auto const was_inhibited = !holds_.isEmpty ();
  auto const previous_holder = holder_summary ();
  auto expired = false;
  auto const now = monotonic_clock_.elapsed ();
  for (auto it = holds_.begin (); it != holds_.end ();)
    {
      if (it->expires_at <= now)
        {
          it = holds_.erase (it);
          ++expiries_;
          expired = true;
        }
      else
        {
          ++it;
        }
    }

  if (was_inhibited != !holds_.isEmpty ()) apply_requested_state ();
  if (expired || was_inhibited != !holds_.isEmpty ()
      || previous_holder != holder_summary ())
    {
      emit_status ();
    }
  schedule_expiry ();
}

QString TxInhibitTransceiver::holder_summary () const
{
  QStringList holders;
  QSet<QString> seen;
  for (auto it = holds_.cbegin (); it != holds_.cend (); ++it)
    {
      if (!it->holder.isEmpty () && !seen.contains (it->holder))
        {
          seen.insert (it->holder);
          holders.append (it->holder);
        }
    }
  holders.sort (Qt::CaseInsensitive);
  return holders.join (QStringLiteral (", "));
}

void TxInhibitTransceiver::tx_inhibit_command (QString controller, quint32 ttl_ms, QString station)
{
  if (!started_) return;
  auto const was_inhibited = !holds_.isEmpty ();
  if (!ttl_ms)
    {
      ++release_rx_;
      holds_.remove (controller);
    }
  else
    {
      ++hold_rx_;
      auto const expires_at = monotonic_clock_.elapsed () + ttl_ms;
      auto const holder = sanitize_holder (station.isEmpty () ? controller : station);
      auto existing = holds_.find (controller);
      if (existing != holds_.end ())
        {
          *existing = Hold {expires_at, holder};
        }
      else
        {
          auto const tracked_count = holds_.size ()
            - (holds_.contains (overflow_hold_key) ? 1 : 0);
          if (tracked_count < maximum_tracked_holds)
            {
              holds_.insert (controller, Hold {expires_at, holder});
            }
          else
            {
              auto overflow = holds_.find (overflow_hold_key);
              if (overflow == holds_.end ())
                {
                  holds_.insert (overflow_hold_key, Hold {expires_at, {}});
                }
              else
                {
                  overflow->expires_at = std::max (overflow->expires_at, expires_at);
                }
            }
        }
    }

  if (was_inhibited != !holds_.isEmpty ()) apply_requested_state ();
  emit_status ();
  schedule_expiry ();
}

void TxInhibitTransceiver::schedule_expiry ()
{
  if (holds_.isEmpty ())
    {
      expiry_timer_->stop ();
      return;
    }

  auto next_expiry = std::numeric_limits<qint64>::max ();
  for (auto it = holds_.cbegin (); it != holds_.cend (); ++it)
    {
      next_expiry = std::min (next_expiry, it->expires_at);
    }
  auto const delay = std::max<qint64> (1, next_expiry - monotonic_clock_.elapsed ());
  expiry_timer_->start (static_cast<int> (std::min<qint64> (delay,
                                                            std::numeric_limits<int>::max ())));
}
