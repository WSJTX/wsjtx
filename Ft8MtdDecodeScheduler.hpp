#ifndef FT8_MTD_DECODE_SCHEDULER_HPP
#define FT8_MTD_DECODE_SCHEDULER_HPP

#include <cstdint>

class Ft8MtdDecodeScheduler
{
public:
  static bool supportsBackpressure (bool multithreadedFt8,
                                    bool standardFinalRequired)
  {
    return multithreadedFt8 && !standardFinalRequired;
  }

  enum class Stage
  {
    None,
    EarlyOne,
    EarlyTwo,
    Final
  };

  enum class Action
  {
    Publish,
    SkipEarly,
    DeferFinal,
    ReplaceFinal
  };

  struct Decision
  {
    Action action {Action::Publish};
    bool enteredDegraded {false};
    int skippedPeriods {0};
  };

  Decision request (Stage stage, std::int64_t period, int earlyStageCount,
                    bool decoderBusy, bool finalAlreadyPending);
  bool published (Stage stage, std::int64_t period);
  void completed (Stage stage, std::int64_t period);
  void publicationFailed (Stage stage, std::int64_t period);
  void cancel ();

  bool degraded () const {return degraded_;}
  int skippedPeriods () const;
  std::int64_t nextProbePeriod () const {return nextProbePeriod_;}

private:
  static constexpr int BackoffCount {4};
  static constexpr int BackoffPeriods[BackoffCount] {1, 2, 4, 8};

  void degrade (std::int64_t period, bool advance);
  bool probeComplete (std::int64_t period) const;
  void clearProbe ();

  bool degraded_ {false};
  int backoffIndex_ {0};
  std::int64_t nextProbePeriod_ {0};
  std::int64_t probePeriod_ {-1};
  int probeEarlyStageCount_ {0};
  int completedEarlyStages_ {0};
  bool recoverOnFinalPublish_ {false};
};

#endif
