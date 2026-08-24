#include <tuple>

#include <QtTest>

#include "Transceiver/HamlibVfoRoleState.hpp"

class TestHamlibVfoRoleState : public QObject
{
  Q_OBJECT

private:
  Q_SLOT void receivingOnVfoAUsesNormalRoles ()
  {
    HamlibVfoRoleState state;
    state.observe_active_vfo (RIG_VFO_A, false);

    auto const vfos = state.resolve (RIG_VFO_A, RIG_VFO_B);
    QCOMPARE (std::get<0> (vfos), RIG_VFO_A);
    QCOMPARE (std::get<1> (vfos), RIG_VFO_B);
  }

  Q_SLOT void receivingOnVfoBReversesRoles ()
  {
    HamlibVfoRoleState state;
    state.observe_active_vfo (RIG_VFO_B, false);

    auto const vfos = state.resolve (RIG_VFO_A, RIG_VFO_B);
    QCOMPARE (std::get<0> (vfos), RIG_VFO_B);
    QCOMPARE (std::get<1> (vfos), RIG_VFO_A);
  }

  Q_SLOT void splitPttPreservesNormalRoles ()
  {
    HamlibVfoRoleState state;
    state.observe_active_vfo (RIG_VFO_A, false);
    state.observe_active_vfo (RIG_VFO_B, true);

    auto const vfos = state.resolve (RIG_VFO_A, RIG_VFO_B);
    QCOMPARE (std::get<0> (vfos), RIG_VFO_A);
    QCOMPARE (std::get<1> (vfos), RIG_VFO_B);
  }

  Q_SLOT void splitPttPreservesReversedRoles ()
  {
    HamlibVfoRoleState state;
    state.observe_active_vfo (RIG_VFO_B, false);
    state.observe_active_vfo (RIG_VFO_A, true);

    auto const vfos = state.resolve (RIG_VFO_A, RIG_VFO_B);
    QCOMPARE (std::get<0> (vfos), RIG_VFO_B);
    QCOMPARE (std::get<1> (vfos), RIG_VFO_A);
  }

  Q_SLOT void observationResumesAfterSplitPtt ()
  {
    HamlibVfoRoleState state;
    state.observe_active_vfo (RIG_VFO_A, false);
    state.observe_active_vfo (RIG_VFO_B, true);
    state.observe_active_vfo (RIG_VFO_B, false);

    auto const vfos = state.resolve (RIG_VFO_A, RIG_VFO_B);
    QCOMPARE (std::get<0> (vfos), RIG_VFO_B);
    QCOMPARE (std::get<1> (vfos), RIG_VFO_A);
  }

  Q_SLOT void resetRestoresNormalRoles ()
  {
    HamlibVfoRoleState state;
    auto vfos = state.resolve (RIG_VFO_A, RIG_VFO_B);
    QCOMPARE (std::get<0> (vfos), RIG_VFO_A);
    QCOMPARE (std::get<1> (vfos), RIG_VFO_B);

    state.observe_active_vfo (RIG_VFO_B, false);
    state.reset ();

    vfos = state.resolve (RIG_VFO_A, RIG_VFO_B);
    QCOMPARE (std::get<0> (vfos), RIG_VFO_A);
    QCOMPARE (std::get<1> (vfos), RIG_VFO_B);
  }
};

QTEST_APPLESS_MAIN (TestHamlibVfoRoleState)
#include "test_hamlib_vfo_role_state.moc"
