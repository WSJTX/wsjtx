#include "StartDxTx.hpp"

#include <QStringList>

#include "Radio.hpp"
#include "qt_helpers.hpp"

namespace StartDxTx
{
  namespace Reason
  {
    char const * const malformed_request {"malformed_request"};
    char const * const invalid_request {"invalid_request"};
    char const * const already_transmitting {"already_transmitting"};
    char const * const tx_already_enabled {"tx_already_enabled"};
    char const * const tuning {"tuning"};
    char const * const watchdog_timeout {"watchdog_timeout"};
    char const * const unsupported_special_operation {"unsupported_special_operation"};
    char const * const de_call_mismatch {"de_call_mismatch"};
    char const * const dial_frequency_mismatch {"dial_frequency_mismatch"};
    char const * const dx_call_mismatch {"dx_call_mismatch"};
    char const * const dx_grid_mismatch {"dx_grid_mismatch"};
    char const * const mode_mismatch {"mode_mismatch"};
    char const * const submode_mismatch {"submode_mismatch"};
    char const * const tr_period_mismatch {"tr_period_mismatch"};
    char const * const tx1_unavailable {"tx1_unavailable"};
    char const * const tx1_address_mismatch {"tx1_address_mismatch"};
    char const * const enable_tx_failed {"enable_tx_failed"};
    char const * const state_changed_after_enable {"state_changed_after_enable"};
  }

  namespace
  {
    bool same (QString const& lhs, QString const& rhs)
    {
      return 0 == lhs.trimmed ().compare (rhs.trimmed (), Qt::CaseInsensitive);
    }

    bool same_call (QString token, QString const& call)
    {
      if (token.size () > 2 && token.startsWith ('<') && token.endsWith ('>'))
        {
          token = token.mid (1, token.size () - 2);
        }
      auto const& trimmed_call = call.trimmed ();
      return !trimmed_call.isEmpty ()
        && (same (token, trimmed_call) || same (token, Radio::base_callsign (trimmed_call.toUpper ())));
    }
  }

  QString validate_identity (Request const& request, Snapshot const& state)
  {
    if (!same (request.de_call, state.de_call)) return Reason::de_call_mismatch;
    if (request.dial_frequency != state.dial_frequency) return Reason::dial_frequency_mismatch;
    if (!same (request.dx_call, state.dx_call)) return Reason::dx_call_mismatch;
    if (!same (request.dx_grid, state.dx_grid)) return Reason::dx_grid_mismatch;
    if (request.mode.trimmed () != state.mode.trimmed ()) return Reason::mode_mismatch;
    if (!same (request.submode, state.submode)) return Reason::submode_mismatch;
    if (request.tr_period != state.tr_period) return Reason::tr_period_mismatch;
    return {};
  }

  QString validate (Request const& request, Snapshot const& state)
  {
    if (request.de_call.trimmed ().isEmpty () || !request.dial_frequency
        || request.dx_call.trimmed ().isEmpty () || request.mode.trimmed ().isEmpty ())
      {
        return Reason::invalid_request;
      }
    if (state.transmitting) return Reason::already_transmitting;
    if (state.tx_enabled) return Reason::tx_already_enabled;
    if (state.tuning) return Reason::tuning;
    if (state.watchdog_timeout) return Reason::watchdog_timeout;
    if (!state.special_operation_supported) return Reason::unsupported_special_operation;
    return validate_identity (request, state);
  }

  bool addresses (QString const& message, QString const& dx_call, QString const& de_call)
  {
    auto const& words = message.split (' ', SkipEmptyParts);
    return words.size () >= 2 && same_call (words[0], dx_call) && same_call (words[1], de_call);
  }

  Result execute (Request const& request, Target& target)
  {
    Result result;
    result.reason = validate (request, target.snapshot ());
    if (!result.reason.isEmpty ()) return result;

    // an older deferred selection (e.g. Tx2 queued by "Generate Std
    // Msgs" while Tx1 was disabled) must not run after this one
    target.cancel_deferred_selections ();

    QString tx_message;
    if (!target.select_initial_message (&tx_message) || tx_message.trimmed ().isEmpty ())
      {
        result.reason = Reason::tx1_unavailable;
        return result;
      }
    if (!addresses (tx_message, request.dx_call, request.de_call))
      {
        result.reason = Reason::tx1_address_mismatch;
        return result;
      }

    // selecting the message must not have changed what was validated
    result.reason = validate (request, target.snapshot ());
    if (!result.reason.isEmpty ()) return result;

    if (!target.enable_tx ())
      {
        target.disable_tx ();
        result.reason = Reason::enable_tx_failed;
        return result;
      }

    // enabling Tx must not have changed the station, DX, mode or
    // selected message either
    QString selected_text;
    auto selected = target.selected_message (&selected_text);
    auto const& snapshot = target.snapshot ();
    if (!validate_identity (request, snapshot).isEmpty () || !snapshot.tx_enabled
        || 1 != selected || selected_text != tx_message)
      {
        target.disable_tx ();
        result.reason = Reason::state_changed_after_enable;
        return result;
      }

    result.accepted = true;
    result.tx_message = tx_message;
    return result;
  }
}
