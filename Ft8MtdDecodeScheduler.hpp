#ifndef FT8_MTD_DECODE_SCHEDULER_HPP
#define FT8_MTD_DECODE_SCHEDULER_HPP

#include <QtGlobal>

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

  Decision request (Stage stage, qint64 period, int earlyStageCount,
                    bool decoderBusy, bool finalAlreadyPending);
  bool published (Stage stage, qint64 period);
  void completed (Stage stage, qint64 period);
  void publicationFailed (Stage stage, qint64 period);
  void cancel ();

  bool degraded () const {return degraded_;}
  int skippedPeriods () const;
  qint64 nextProbePeriod () const {return nextProbePeriod_;}

private:
  static constexpr int BackoffCount {4};
  static constexpr int BackoffPeriods[BackoffCount] {1, 2, 4, 8};

  void degrade (qint64 period, bool advance);
  bool probeComplete (qint64 period) const;
  void clearProbe ();

  bool degraded_ {false};
  int backoffIndex_ {0};
  qint64 nextProbePeriod_ {0};
  qint64 probePeriod_ {-1};
  int probeEarlyStageCount_ {0};
  int completedEarlyStages_ {0};
  bool recoverOnFinalPublish_ {false};
};

#endif
