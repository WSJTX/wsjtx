#include "RoundRobinSelection.hpp"

#include <QComboBox>
#include <QSignalBlocker>

namespace
{
  int constexpr minimumPresetSlotCount {2};
  int constexpr maximumPresetSlotCount {6};
}

namespace RoundRobinSelection
{
  void initialize (QComboBox& combo, QString const& randomLabel)
  {
    QSignalBlocker const blocker {&combo};
    combo.clear ();
    combo.addItem (randomLabel, QStringLiteral ("random"));
    // Keep the standard 2–6 presets while allowing larger custom schedules.
    for (int count = minimumPresetSlotCount; count <= maximumPresetSlotCount; ++count)
      {
        for (int slot = 0; slot < count; ++slot)
          {
            auto const canonical = QString::fromStdString (
              BeaconTx::formatRoundRobinPolicy (BeaconTx::RoundRobinPolicy::fixed (slot, count)));
            combo.addItem (canonical, canonical);
          }
      }
  }

  BeaconTx::RoundRobinPolicy policy (QComboBox const& combo)
  {
    auto const index = combo.currentIndex ();
    auto const text = combo.currentText ();
    // Editing a preset leaves its index selected until the edit is committed.
    auto const value = index >= 0 && text == combo.itemText (index)
      && combo.itemData (index).isValid () ? combo.itemData (index).toString () : text;
    return BeaconTx::parseRoundRobinPolicy (value.toStdString ());
  }

  void setPolicy (QComboBox& combo, BeaconTx::RoundRobinPolicy const& policy)
  {
    auto const canonical = QString::fromStdString (BeaconTx::formatRoundRobinPolicy (policy));
    auto const index = combo.findData (canonical);
    if (index >= 0) combo.setCurrentIndex (index);
    else combo.setEditText (canonical);
  }
}
