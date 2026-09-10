#ifndef AUTORESPONDPOLICY_HPP
#define AUTORESPONDPOLICY_HPP

// Values match the historical RespondCQ combo indices stored in QSettings.
enum class AutoRespondPolicy
{
  None = 0,
  First = 1,
  MaxDistance = 2,
  MaxSignal = 3,
  MinSignal = 4
};

constexpr bool isScoringAutoRespondPolicy (AutoRespondPolicy policy)
{
  return policy == AutoRespondPolicy::MaxDistance
    || policy == AutoRespondPolicy::MaxSignal
    || policy == AutoRespondPolicy::MinSignal;
}

#endif
