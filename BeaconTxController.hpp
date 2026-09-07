#ifndef BEACONTXCONTROLLER_HPP
#define BEACONTXCONTROLLER_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace BeaconTx
{
  // Identifies one UTC period within a scheduling epoch so delayed outcomes from
  // an earlier mode, period, or clock timeline cannot affect the current schedule.
  struct PlanId
  {
    std::uint64_t epoch {0};
    std::int64_t periodSerial {-1};

    bool isValid () const {return epoch != 0 && periodSerial >= 0;}
  };

  inline bool operator== (PlanId const& lhs, PlanId const& rhs)
  {
    return lhs.epoch == rhs.epoch && lhs.periodSerial == rhs.periodSerial;
  }

  inline bool operator!= (PlanId const& lhs, PlanId const& rhs)
  {
    return !(lhs == rhs);
  }

  enum class Disposition {Receive, Transmit};
  enum class PlanSource {Percentage, BandHop, TxNext};
  enum class ApplicationStatus {AwaitingProposal, Ready, Applying, Applied, Failed};
  // A requested and accepted start is not yet a beacon transmission. MessageStarted
  // means the audio source committed, which is when Tx Next and history may change.
  enum class TxLifecycle {None, Decided, StartRequested, MessageStarted};
  enum class TuneKind {None, Manual, Automatic};

  struct RoundRobinPolicy
  {
    enum class Kind {Random, Fixed};

    Kind kind {Kind::Random};
    int selectedSlot {0};
    int slotCount {0};

    static RoundRobinPolicy random () {return {};}
    static RoundRobinPolicy fixed (int selected, int count)
    {
      RoundRobinPolicy result;
      result.kind = Kind::Fixed;
      result.selectedSlot = selected;
      result.slotCount = count;
      return result;
    }
  };

  struct HoppingProposal
  {
    int frequenciesIndex {-1};
    bool tuneRequired {false};
    std::string periodName;
  };

  // A provider's scheduling choice, containing no rig or UI effects. BeaconPlan
  // is the controller-owned copy tied to a target period and tracks application.
  struct ScheduleProposal
  {
    Disposition disposition {Disposition::Receive};
    PlanSource source {PlanSource::Percentage};
    bool hasHoppingProposal {false};
    HoppingProposal hopping;
  };

  struct BeaconPlan
  {
    PlanId id;
    Disposition disposition {Disposition::Receive};
    PlanSource source {PlanSource::Percentage};
    bool hasHoppingProposal {false};
    HoppingProposal hopping;
    ApplicationStatus applicationStatus {ApplicationStatus::AwaitingProposal};
  };

  enum class ActionKind
  {
    SetTransmitWindow,
    RequestScheduleProposal,
    ApplyBandChange,
    StartAutomaticTune,
    RestoreAuto,
    ClearTxNextUi,
    RecordBeaconTransmission
  };

  struct Action
  {
    ActionKind kind {ActionKind::SetTransmitWindow};
    PlanId planId;
    bool enabled {false};
    HoppingProposal hopping;

    static Action setTransmitWindow (PlanId id, bool enabled);
    static Action forPlan (ActionKind kind, PlanId id);
  };

  // Owns WSPR/FST4W period plans and Tx/Tune lifecycle state. It reads no clock and
  // performs no UI or rig I/O; callers supply events, apply actions, and report
  // outcomes by PlanId.
  class Controller final
  {
  public:
    using Actions = std::vector<Action>;

    Actions enterMode (std::int64_t periodMilliseconds,
                       RoundRobinPolicy policy = RoundRobinPolicy::random ());
    Actions exitMode ();
    Actions setPeriod (std::int64_t periodMilliseconds);
    void setAutoEnabled (bool enabled, bool explicitOperatorChange = true);
    void setRoundRobinPolicy (RoundRobinPolicy policy);
    Actions setTxNext (bool enabled);

    Actions observeUtc (std::int64_t utcMilliseconds);
    Actions receiveCompleted ();
    Actions tuneStarted (TuneKind kind, PlanId planId = {});
    Actions tuneCompleted (PlanId planId);
    Actions txStartRequested ();
    Actions txStartResult (PlanId planId, bool accepted);
    Actions messageStarted (PlanId planId);
    Actions transmitWindowEnded ();
    Actions txStopped (PlanId planId);
    Actions proposalDelivered (PlanId planId, ScheduleProposal const& proposal);
    Actions bandChangeOutcome (PlanId planId, bool succeeded);

    bool active () const {return m_active;}
    bool autoEnabled () const {return m_autoEnabled;}
    bool txNextPending () const {return m_txNextPending;}
    bool transmitWindow () const {return m_transmitWindow;}
    TuneKind tuneKind () const {return m_tuneKind;}
    TxLifecycle txLifecycle () const {return m_txLifecycle;}
    PlanSource currentPlanSource () const {return m_currentPlanSource;}
    PlanId currentPlanId () const {return m_currentPlanId;}
    PlanId txPlanId () const {return m_txPlanId;}
    PlanId tunePlanId () const {return m_tunePlanId;}
    BeaconPlan const * futurePlan () const {return m_hasFuturePlan ? &m_futurePlan : nullptr;}

  private:
    Actions resynchronize (std::int64_t periodSerial);
    void appendPreparation (Actions& actions);
    bool fixedRoundRobinTransmits (std::int64_t utcMilliseconds) const;
    void clearFuturePlan ();

    bool m_active {false};
    bool m_autoEnabled {false};
    bool m_txNextPending {false};
    bool m_initialized {false};
    bool m_transmitWindow {false};
    bool m_currentCompleted {false};
    bool m_hasFuturePlan {false};
    bool m_preparationSuppressed {false};
    bool m_restoreAuto {false};
    bool m_messageSessionActive {false};
    bool m_txStartAccepted {false};
    bool m_currentFromTxNext {false};
    std::uint64_t m_epoch {0};
    std::int64_t m_periodMilliseconds {0};
    std::int64_t m_currentPeriodSerial {-1};
    std::int64_t m_lastObservedUtc {-1};
    RoundRobinPolicy m_roundRobinPolicy;
    Disposition m_txNextUnderlyingDisposition {Disposition::Receive};
    PlanSource m_txNextUnderlyingSource {PlanSource::Percentage};
    PlanSource m_currentPlanSource {PlanSource::Percentage};
    BeaconPlan m_futurePlan;
    PlanId m_currentPlanId;
    PlanId m_txPlanId;
    TxLifecycle m_txLifecycle {TxLifecycle::None};
    TuneKind m_tuneKind {TuneKind::None};
    PlanId m_tunePlanId;
  };
}

#endif
