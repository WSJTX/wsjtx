#ifndef TX_OUTPUT_FAULT_POLICY_HPP_
#define TX_OUTPUT_FAULT_POLICY_HPP_

#include "Audio/TxPlaybackEvidence.hpp"

namespace TxOutputFaultPolicy
{
  enum class Action {Ignore, Report, Stop};

  inline Action decide (TxEvidence::TxOutputError const& fault,
                        TxEvidence::TxPlaybackContext currentActiveContext,
                        bool jttyActiveOrPending) noexcept
  {
    if (!currentActiveContext.isValid () || !fault.context.isValid ()
        || fault.context != currentActiveContext)
      return Action::Ignore;

    switch (fault.error)
      {
      case TxEvidence::TxBackendError::None:
        return Action::Ignore;
      case TxEvidence::TxBackendError::Open:
      case TxEvidence::TxBackendError::Io:
      case TxEvidence::TxBackendError::Fatal:
        return Action::Stop;
      case TxEvidence::TxBackendError::Underrun:
        return jttyActiveOrPending ? Action::Stop : Action::Report;
      case TxEvidence::TxBackendError::Unknown:
        return Action::Report;
      }
    return Action::Report;
  }
}

#endif
