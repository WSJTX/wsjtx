#ifndef START_DX_TX_HPP__
#define START_DX_TX_HPP__

#include <QtGlobal>
#include <QString>

//
// StartDxTx - logic for the "Start DX Tx" UDP request (message type
//             18, see NetworkMessage.hpp)
//
// The request asks WSJT-X to atomically select the initial standard
// message (Tx1) to the current DX station and enable Tx. It is only
// honoured if the request still matches the current station, DX and
// mode settings, otherwise it is rejected with a reason code.
//
namespace StartDxTx
{
  // reason codes sent in the "Start DX Tx Result" message
  namespace Reason
  {
    extern char const * const malformed_request;
    extern char const * const invalid_request;
    extern char const * const already_transmitting;
    extern char const * const tx_already_enabled;
    extern char const * const tuning;
    extern char const * const watchdog_timeout;
    extern char const * const unsupported_special_operation;
    extern char const * const de_call_mismatch;
    extern char const * const dial_frequency_mismatch;
    extern char const * const dx_call_mismatch;
    extern char const * const dx_grid_mismatch;
    extern char const * const mode_mismatch;
    extern char const * const submode_mismatch;
    extern char const * const tr_period_mismatch;
    extern char const * const tx1_unavailable;
    extern char const * const tx1_address_mismatch;
    extern char const * const enable_tx_failed;
    extern char const * const state_changed_after_enable;
  }

  struct Request
  {
    quint32 request_id {0};
    QString de_call;
    quint64 dial_frequency {0};
    QString dx_call;
    QString dx_grid;
    QString mode;
    QString submode;
    quint32 tr_period {0};
  };

  // the client state as reported in the Status message plus the
  // transmit state needed to decide whether a start is safe
  struct Snapshot
  {
    QString de_call;
    quint64 dial_frequency {0};
    QString dx_call;
    QString dx_grid;
    QString mode;
    QString submode;
    quint32 tr_period {0};
    bool transmitting {false};
    bool tx_enabled {false};
    bool tuning {false};
    bool watchdog_timeout {false};
    bool special_operation_supported {true};
  };

  struct Result
  {
    bool accepted {false};
    QString reason;
    QString tx_message;
  };

  // invalidates transmit message selections that were deferred to the
  // event loop (e.g. by the "Generate Std Msgs" button) so that they
  // cannot override a selection made later
  class DeferredSelectionGuard
  {
  public:
    quint64 token () const {return generation_;}
    bool is_current (quint64 token) const {return token == generation_;}
    void invalidate () {++generation_;}

  private:
    quint64 generation_ {0};
  };

  // abstraction of the WSJT-X main window so that the logic can be
  // tested with a mock
  class Target
  {
  public:
    virtual ~Target () {}

    virtual Snapshot snapshot () const = 0;

    // prevent any pending deferred transmit message selection from
    // taking effect
    virtual void cancel_deferred_selections () = 0;

    // synchronously generate the standard messages for the current DX
    // call and select Tx1, returns false if Tx1 cannot be selected,
    // on success tx_message receives the selected message text
    virtual bool select_initial_message (QString * tx_message) = 0;

    // the currently selected transmit message number (1-6) and text
    virtual int selected_message (QString * tx_message) const = 0;

    // synchronously enable Tx, returns true if Tx is now enabled
    virtual bool enable_tx () = 0;

    // synchronously disable Tx
    virtual void disable_tx () = 0;
  };

  // returns an empty string if the request may proceed, otherwise a
  // reason code
  QString validate (Request const&, Snapshot const&);

  // as validate but only checks that the station, DX and mode still
  // match, not the transmit state
  QString validate_identity (Request const&, Snapshot const&);

  // true if the message is addressed to the DX call from the DE call,
  // either may be the base call, hashed calls (<...>) are accepted
  bool addresses (QString const& message, QString const& dx_call, QString const& de_call);

  Result execute (Request const&, Target&);
}

#endif
