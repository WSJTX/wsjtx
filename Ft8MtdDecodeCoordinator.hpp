#ifndef FT8_MTD_DECODE_COORDINATOR_HPP
#define FT8_MTD_DECODE_COORDINATOR_HPP

#include <functional>
#include <memory>

#include <QtGlobal>

#include "DecodeOperatingContext.hpp"
#include "DecoderIpc.hpp"
#include "Ft8MtdDecodeScheduler.hpp"

class Ft8MtdDecodeCoordinator
{
public:
  using Stage = Ft8MtdDecodeScheduler::Stage;
  using Action = Ft8MtdDecodeScheduler::Action;
  using Decision = Ft8MtdDecodeScheduler::Decision;

  struct PendingMtdDecode
  {
    DecoderIpc::Ft8MtdPayload payload;
    DecodeOperatingContext context;
    qint64 period {0};
  };

  enum class PendingPublishResult
  {
    Published,
    Unavailable,
    Failed
  };

  enum class DrainResult
  {
    NoPending,
    Published,
    Retained,
    Obsolete,
    Failed
  };

  struct DrainOutcome
  {
    DrainResult result {DrainResult::NoPending};
    qint64 period {0};
    bool recovered {false};
  };

  using PendingValidator = std::function<bool (PendingMtdDecode const&)>;
  using PendingPublisher =
    std::function<PendingPublishResult (PendingMtdDecode const&)>;

  Decision request (Stage stage, qint64 period, int earlyStageCount,
                    bool decoderBusy);
  bool published (Stage stage, qint64 period);
  void completed (Stage stage, qint64 period);
  void publicationFailed (Stage stage, qint64 period);

  Action deferFinal (std::unique_ptr<PendingMtdDecode> pending);
  DrainOutcome drainPending (PendingValidator const& validator,
                             PendingPublisher const& publisher);
  void cancel ();

  bool hasPending () const {return bool {pending_};}
  bool degraded () const {return scheduler_.degraded ();}
  int skippedPeriods () const {return scheduler_.skippedPeriods ();}
  qint64 nextProbePeriod () const {return scheduler_.nextProbePeriod ();}

private:
  Ft8MtdDecodeScheduler scheduler_;
  std::unique_ptr<PendingMtdDecode> pending_;
};

#endif
