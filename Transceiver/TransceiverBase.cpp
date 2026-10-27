#include "TransceiverBase.hpp"

#include <exception>

#include <QString>
#include <QTimer>
#include <QThread>
#include <QDebug>

#include "moc_TransceiverBase.cpp"

namespace
{
  auto const unexpected = TransceiverBase::tr ("Unexpected rig error");
}

void TransceiverBase::start (unsigned sequence_number) noexcept
{
  CAT_TRACE ("#: " << sequence_number);
  failure_reported_ = false;

  QString message;
  try
    {
      last_sequence_number_ = sequence_number;
      may_update u {this, true};
      shutdown ();
      startup ();
    }
  catch (std::exception const& e)
    {
      CAT_TRACE ("#: " << sequence_number << " what: " << e.what ());
      message = e.what ();
    }
  catch (...)
    {
      CAT_TRACE ("#: " << sequence_number);
      message = unexpected;
    }
  if (!message.isEmpty ())
    {
      offline (message);
    }
}

void TransceiverBase::set (TransceiverState const& s,
                           unsigned sequence_number) noexcept
{
  CAT_TRACE ("#: " << s);

  try {
        do_txvolume (s.txvolume());
  } catch(...) {printf("Setting tx attenuation failed");}

  try {
      do_volume (s.volume());
  } catch(...) {printf("Setting rx attenuation failed");}

  QString message;
  try
    {
      last_sequence_number_ = sequence_number;
      may_update u {this, true};
      bool was_online {requested_.online ()};
      if (!s.online () && was_online)
        {
          shutdown ();
        }
      else if (s.online () && !was_online)
        {
          shutdown ();
          startup ();
        }
      if (requested_.online ())
        {
          bool audio_cmd {false};
          if (requested_.receive_policy () != s.receive_policy ()) {
            do_receive_policy (s.receive_policy ());
            requested_.receive_policy (s.receive_policy ());
          }
          if (requested_.receive_context () != s.receive_context ()) {
            do_receive_context (s.receive_context ());
            requested_.receive_context (s.receive_context ());
          }
          if (requested_.blocksize() != s.blocksize()) {
            do_blocksize (s.blocksize());
            audio_cmd = true;
            requested_.blocksize (s.blocksize ());
          }
          if (!period_applied_ || applied_period_ != s.period()) {
            bool const had_applied_period {period_applied_};
            do_period (s.period());
            requested_.period (s.period ());
            applied_period_ = s.period ();
            period_applied_ = true;
            audio_cmd = audio_cmd || had_applied_period;
          }
          if (requested_.spread() != s.spread()) {
            do_spread (s.spread());
            audio_cmd = true;
            requested_.spread (s.spread ());
          }
          if (requested_.nsym() != s.nsym()) {
            do_nsym (s.nsym());
            audio_cmd = true;
            requested_.nsym (s.nsym ());
          }
          if (requested_.trfrequency() != s.trfrequency()) {
            do_trfrequency (s.trfrequency());
            audio_cmd = true;
            requested_.trfrequency (s.trfrequency ());
          }
          if (requested_.audio() != s.audio()) {
            do_audio (s.audio());
            audio_cmd = true;
            requested_.audio (s.audio ());
          }
          if (requested_.tx_audio() != s.tx_audio()) {
            if (s.tx_audio()) {
              do_modulator_start (s.tx_request ());
              requested_.tx_request (s.tx_request ());
            } else {
              do_modulator_stop(s.quick ());
              requested_.quick (s.quick ());
            }
            audio_cmd = true;

            requested_.tx_audio (s.tx_audio ());
          }

          if (requested_.tune() != s.tune()) {
            do_tune (s.tune());
            audio_cmd = true;
            requested_.tune (s.tune ());
          }
          if (!audio_cmd) {
            bool ptt_on {false};
            bool ptt_off {false};
            if (s.ptt () != requested_.ptt ())
              {
                ptt_on = s.ptt ();
                ptt_off = !s.ptt ();
              }
            if (ptt_off)
              {
                do_ptt (false);
                do_post_ptt (false);
                if (!requested_.audio()) QThread::msleep (100); // some rigs cannot process CAT
                                       // commands while switching from
                                       // Tx to Rx
              }

          if (s.frequency_request (RigFrequency::Path::Rx)
              || s.frequency_request (RigFrequency::Path::Tx))
            {
              apply_frequency (s, RigFrequency::Path::Rx, ptt_off);
              apply_frequency (s, RigFrequency::Path::Tx, ptt_on);
              if (s.mode () != UNK && s.mode () != requested_.mode ())
                {
                  do_mode (s.mode ());
                  do_post_mode (s.mode ());
                }
              requested_.frequency (actual_.frequency ());
              requested_.tx_frequency (actual_.tx_frequency ());
              requested_.split (actual_.split ());
            }
          else if (s.frequency ()    // ignore bogus zero frequencies
              && ((s.frequency () != requested_.frequency () // and QSY
                   || (s.mode () != UNK && s.mode () != requested_.mode ())))) // or mode change
            {
                requested_.frequency (s.frequency ());
                requested_.mode (s.mode ());
              do_frequency (s.frequency (), s.mode (), ptt_off);
              do_post_frequency (s.frequency (), s.mode ());

              // record what actually changed
              requested_.frequency (actual_.frequency ());
              requested_.mode (actual_.mode ());
            }
            else if (!s.tx_frequency ()
              || (s.tx_frequency () > 10000 // ignore bogus startup values
                  && s.tx_frequency () < std::numeric_limits<Frequency>::max () - 10000))
            {
              if ((s.tx_frequency () != requested_.tx_frequency () // and QSY
                   || (s.mode () != UNK && s.mode () != requested_.mode ())) // or mode change
                  // || s.split () != requested_.split ())) // or split change
                   || (s.tx_frequency () && ptt_on)) // or about to tx split
                {
                  requested_.tx_frequency (s.tx_frequency ());
                  requested_.split (s.tx_frequency () != 0);
                  do_tx_frequency (s.tx_frequency (), s.mode (), ptt_on);
                  do_post_tx_frequency (s.tx_frequency (), s.mode ());

                  // record what actually changed
                  requested_.tx_frequency (actual_.tx_frequency ());
                  requested_.split (actual_.split ());
                }
            }
          if (ptt_on)
            {
              do_ptt (true);
              do_post_ptt (true);
              QThread::msleep (100); // some rigs cannot process CAT
                                     // commands while switching from
                                     // Rx to Tx
            }

          // record what actually changed
            if (ptt_on || ptt_off) requested_.ptt (actual_.ptt ());
        }
          else
            {
              for (auto path : {RigFrequency::Path::Rx, RigFrequency::Path::Tx})
                {
                  auto const request = s.frequency_request (path);
                  auto& result = frequency_results_[path == RigFrequency::Path::Tx];
                  if (request && !(RigFrequency::same_operation (request, result.request)
                                   && result.outcome == RigFrequency::Outcome::Established))
                    {
                      result = {request, RigFrequency::Outcome::Deferred, RigFrequency::Basis::Retained,
                        path == RigFrequency::Path::Tx ? actual_.tx_frequency () : actual_.frequency ()};
                      Q_EMIT frequency_result (result);
                    }
                }
            }
    }
	}
  catch (std::exception const& e)
    {
      CAT_TRACE ("#: " << sequence_number << " what: " << e.what ());
      message = e.what ();
    }
  catch (...)
    {
      CAT_TRACE ("#: " << sequence_number << " " << sequence_number);
      message = unexpected;
    }
  if (!message.isEmpty ())
    {
      for (auto path : {RigFrequency::Path::Rx, RigFrequency::Path::Tx})
        {
          auto const request = s.frequency_request (path);
          auto& result = frequency_results_[path == RigFrequency::Path::Tx];
          if (request && (!RigFrequency::same_operation (request, result.request)
                          || result.outcome == RigFrequency::Outcome::Pending))
            {
              result = {request, RigFrequency::Outcome::Failed, RigFrequency::Basis::Retained,
                path == RigFrequency::Path::Tx ? actual_.tx_frequency () : actual_.frequency ()};
              Q_EMIT frequency_result (result);
            }
        }
      offline (message);
    }
}

void TransceiverBase::startup ()
{
  CAT_TRACE ("startup\n");
  failure_reported_ = false;
  for (auto& basis : frequency_basis_) basis = RigFrequency::Basis::Retained;
  for (auto& result : frequency_results_) result = RigFrequency::Result {};
  QString message;
  try
    {
      actual_.online (true);
      requested_.online (true);
      auto res = do_start ();
      frequency_resolution_ = RigFrequency::resolution_hz (res);
      do_post_start ();
      Q_EMIT resolution (res);
    }
  catch (std::exception const& e)
    {
      CAT_TRACE ("startup" << " what: " << e.what () << '\n');
      message = e.what ();
    }
  catch (...)
    {
      CAT_TRACE ("startup error: unexpected\n");
      message = unexpected;
    }
  if (!message.isEmpty ())
    {
      offline (message);
    }
}

void TransceiverBase::shutdown ()
{
  CAT_TRACE ("shutdown\n");
  may_update u {this};
  if (requested_.online ())
    {
      try
        {
          // try and ensure PTT isn't left set
          do_ptt (false);
          do_post_ptt (false);
          if (requested_.split ())
            {
              // try and reset split mode
              do_tx_frequency (0, UNK, true);
              do_post_tx_frequency (0, UNK);
              update_split(false); //w3sz tci
            }
        }
      catch (...)
        {
          CAT_TRACE ("shutdown\n");
          // don't care about exceptions
        }
    }
  do_stop();
  do_post_stop ();
  actual_ = TransceiverState {};
  requested_ = TransceiverState {};
  period_applied_ = false;
  applied_period_ = 0.0;
}

void TransceiverBase::stop () noexcept
{
  CAT_TRACE ("stop");
  QString message;
  try
    {
      shutdown ();
    }
  catch (std::exception const& e)
    {
      CAT_TRACE ("stop" << " what: " << e.what ());
      message = e.what ();
    }
  catch (...)
    {
      CAT_TRACE ("stop");
      message = unexpected;
    }
  if (!message.isEmpty ())
    {
      offline (message);
    }
  else
    {
      Q_EMIT finished ();
    }
}

void TransceiverBase::apply_frequency (TransceiverState const& s, RigFrequency::Path path, bool force)
{
  using namespace RigFrequency;
  auto const index = path == Path::Tx;
  auto const request = s.frequency_request (path);
  if (!request) return;
  auto const cached = frequency_results_[index];
  if (request.generation < frequency_generation_) return;
  auto const value = index ? s.tx_frequency () : s.frequency ();
  auto const current = index ? actual_.tx_frequency () : actual_.frequency ();
  bool const arrangement_matches = (!index || actual_.split () == (value != 0))
    && (s.mode () == UNK || s.mode () == actual_.mode ());
  if (same_operation (request, cached.request)
      && cached.outcome != Outcome::Deferred
      && (cached.outcome != Outcome::Established || (current == value && arrangement_matches && !force)))
    {
      Q_EMIT frequency_result (cached);
      return;
    }
  frequency_generation_ = request.generation;
  if (cached.request && cached.outcome == Outcome::Pending
      && cached.request.path != request.path && !same_operation (cached.request, request))
    Q_EMIT frequency_result (Result {cached.request, Outcome::Deferred, Basis::Retained, current});
  Result result {request, Outcome::Pending, Basis::Retained, current};
  frequency_results_[index] = result;
  frequency_polls_remaining_[index] = polls_to_stabilize;
  if (request.target != value || (!index && !value))
    {
      result.outcome = Outcome::Rejected;
      frequency_results_[index] = result;
      Q_EMIT frequency_result (result);
      return;
    }
  if (current == value && frequency_basis_[index] != Basis::Retained && arrangement_matches && !force
      && (!frequency_readback_expected (path, value) || frequency_basis_[index] == Basis::Readback))
    {
      result = {request, Outcome::Established, frequency_basis_[index], current};
      frequency_results_[index] = result;
      Q_EMIT frequency_result (result);
      return;
    }
  Q_EMIT frequency_result (result);
  auto const previous = active_frequency_[index];
  auto const previous_result = active_frequency_result_[index];
  active_frequency_[index] = request;
  active_frequency_result_[index] = &result;
  try
    {
      if (index)
        {
          do_tx_frequency (value, s.mode (), force);
          if (result.outcome != Outcome::Deferred) do_post_tx_frequency (value, s.mode ());
        }
      else
        {
          do_frequency (value, s.mode (), force);
          if (result.outcome != Outcome::Deferred) do_post_frequency (value, s.mode ());
        }
    }
  catch (...)
    {
      active_frequency_[index] = previous;
      active_frequency_result_[index] = previous_result;
      result = {request, Outcome::Failed, Basis::Retained, current};
      if (same_operation (frequency_results_[index].request, request)) frequency_results_[index] = result;
      Q_EMIT frequency_result (result);
      throw;
    }
  active_frequency_[index] = previous;
  active_frequency_result_[index] = previous_result;
  if (same_operation (frequency_results_[index].request, request)) frequency_results_[index] = result;
  Q_EMIT frequency_result (result);
}

void TransceiverBase::defer_frequency (RigFrequency::Path path)
{
  auto result = active_frequency_result_[path == RigFrequency::Path::Tx];
  if (result) result->outcome = RigFrequency::Outcome::Deferred;
}

void TransceiverBase::fail_pending_frequencies ()
{
  for (auto& result : frequency_results_)
    if (result.request && result.outcome == RigFrequency::Outcome::Pending)
      {
        result.outcome = RigFrequency::Outcome::Failed;
        Q_EMIT frequency_result (result);
      }
}

void TransceiverBase::frequency_poll_complete ()
{
  using namespace RigFrequency;
  for (auto path : {Path::Rx, Path::Tx})
    {
      auto const index = path == Path::Tx;
      auto& result = frequency_results_[index];
      if (result.request && result.outcome == Outcome::Pending
          && frequency_readback_expected (path, result.request.target)
          && frequency_polls_remaining_[index] && !--frequency_polls_remaining_[index])
        {
          result.outcome = Outcome::Failed;
          Q_EMIT frequency_result (result);
        }
    }
}

void TransceiverBase::frequency_evidence (RigFrequency::Path path, Frequency value,
                                         RigFrequency::Basis basis)
{
  using namespace RigFrequency;
  auto const index = path == Path::Tx;
  auto const operation = active_frequency_[index];
  if (basis == Basis::Retained) basis = operation ? Basis::BackendOperation : Basis::Readback;
  frequency_basis_[index] = basis;
  if (operation && basis == Basis::BackendOperation
      && frequency_readback_expected (path, operation.target)) return;
  auto& result = active_frequency_result_[index] ? *active_frequency_result_[index] : frequency_results_[index];
  auto const pending = operation ? operation :
    (result.outcome == Outcome::Pending ? result.request : Request {});
  Q_EMIT frequency_observation (Evidence {pending ? pending.generation : frequency_generation_, ++frequency_serial_,
    pending ? pending.path : path, value, basis, pending});
  if (pending && same_operation (result.request, pending)
      && within_resolution (value, pending.target, frequency_resolution_))
    {
      result = {pending, Outcome::Established, basis, value};
      if (!operation) Q_EMIT frequency_result (result);
    }
}

void TransceiverBase::update_rx_frequency (Frequency rx, RigFrequency::Basis basis)
{
  CAT_TRACE ("frequency: " << rx);
  if (rx)
    {
      actual_.frequency (rx);
      if (!active_frequency_[0]) requested_.frequency (rx);
      frequency_evidence (RigFrequency::Path::Rx, rx, basis);
    }
}

void TransceiverBase::update_other_frequency (Frequency tx, RigFrequency::Basis basis)
{
  CAT_TRACE ("frequency: " << tx);
  actual_.tx_frequency (tx);
  frequency_evidence (RigFrequency::Path::Tx, tx, basis);
}

void TransceiverBase::update_split (bool state)
{
  CAT_TRACE ("state: " << state);
  actual_.split (state);
}

void TransceiverBase::update_mode (MODE m)
{
  CAT_TRACE ("mode: " << m);
  actual_.mode (m);
  requested_.mode (m);    // track rig changes
}

void TransceiverBase::update_PTT (bool state)
{
  CAT_TRACE ("state: " << state);
  actual_.ptt (state);
}

void TransceiverBase::update_level (int l)
{
    CAT_TRACE ("level: " << l);
    actual_.level (l);
    //  requested_.level (l);    // track rig changes
}

void TransceiverBase::update_power (unsigned int p)
{
  CAT_TRACE ("power: " << p);
  actual_.power (p);
}

void TransceiverBase::update_swr (unsigned int p)
{
  CAT_TRACE ("swr: " << p);
  actual_.swr (p);
}

void TransceiverBase::update_complete (bool force_signal)
{
  CAT_TRACE ("force signal: " << force_signal) << '\n';
  if ((do_pre_update () && actual_ != last_) || force_signal)
    {
      Q_EMIT update (actual_, last_sequence_number_);
      last_ = actual_;
    }
}

void TransceiverBase::offline (QString const& reason)
{
  CAT_TRACE ("reason: " << reason << '\n');
  if (failure_reported_) return;
  failure_reported_ = true;
  Q_EMIT failure (reason);
  try
    {
      shutdown ();
    }
  catch (...)
    {
      CAT_TRACE ("reason: " << reason);
      // don't care
    }
}
