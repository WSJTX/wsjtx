#ifndef WAITFEATUREPOLICY_HPP
#define WAITFEATUREPOLICY_HPP

#include <QString>

#include "SpecialOperatingActivity.hpp"

struct WaitFeatureContext
{
  WaitFeatureContext (QString const& mode,
                      SpecialOperatingActivity specialOperation,
                      bool waitFeaturesEnabled,
                      bool autoSequenceEnabled,
                      bool hasDxCall,
                      bool ncccSprint)
    : mode {mode}
    , specialOperation {specialOperation}
    , waitFeaturesEnabled {waitFeaturesEnabled}
    , autoSequenceEnabled {autoSequenceEnabled}
    , hasDxCall {hasDxCall}
    , ncccSprint {ncccSprint}
  {
  }

  QString mode;
  SpecialOperatingActivity specialOperation;
  bool waitFeaturesEnabled;
  bool autoSequenceEnabled;
  bool hasDxCall;
  bool ncccSprint;
};

enum class EnableTxWarning
{
  NONE,
  WAIT_AND_REPLY,
  NCCC_SPRINT,
  HOUND_AUTO_REPLY
};

inline bool slow_wait_feature_mode_supported (QString const& mode)
{
  return mode == "FT8" || mode == "FT4" || mode == "Q65" || mode == "FST4"
    || mode == "JT65" || mode == "JT9" || mode == "JT4";
}

inline bool wait_and_reply_mode_supported (QString const& mode)
{
  return slow_wait_feature_mode_supported (mode) || mode == "MSK144";
}

inline bool nccc_sprint_auto_reply (WaitFeatureContext const& context)
{
  return context.mode == "FT4"
    && context.specialOperation == SpecialOperatingActivity::NA_VHF
    && context.ncccSprint;
}

inline EnableTxWarning enable_tx_warning (WaitFeatureContext const& context)
{
  if (!context.hasDxCall || context.specialOperation == SpecialOperatingActivity::FOX)
    {
      return EnableTxWarning::NONE;
    }
  if (context.specialOperation == SpecialOperatingActivity::HOUND)
    {
      return context.mode == "FT8"
        ? EnableTxWarning::HOUND_AUTO_REPLY
        : EnableTxWarning::NONE;
    }
  if (nccc_sprint_auto_reply (context))
    {
      return EnableTxWarning::NCCC_SPRINT;
    }
  if (context.waitFeaturesEnabled && wait_and_reply_mode_supported (context.mode))
    {
      return EnableTxWarning::WAIT_AND_REPLY;
    }
  return EnableTxWarning::NONE;
}

inline bool wait_and_call_mode_supported (QString const& mode)
{
  return mode == "FT8" || mode == "FT4" || mode == "Q65" || mode == "FST4"
    || mode == "MSK144";
}

inline bool wait_and_call_arming_eligible (WaitFeatureContext const& context)
{
  auto const ordinaryWithCall = context.specialOperation == SpecialOperatingActivity::NONE
    && context.hasDxCall;
  auto const ft8Hound = context.mode == "FT8"
    && context.specialOperation == SpecialOperatingActivity::HOUND;
  return context.waitFeaturesEnabled
    && context.autoSequenceEnabled
    && wait_and_call_mode_supported (context.mode)
    && (ordinaryWithCall || ft8Hound);
}

inline bool wait_and_call_warning_eligible (WaitFeatureContext const& context)
{
  return context.hasDxCall && wait_and_call_arming_eligible (context);
}

#endif // WAITFEATUREPOLICY_HPP
