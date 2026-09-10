#ifndef ROUNDROBINSELECTION_HPP
#define ROUNDROBINSELECTION_HPP

#include "BeaconTxController.hpp"

class QComboBox;
class QString;

namespace RoundRobinSelection
{
  void initialize (QComboBox& combo, QString const& randomLabel);
  BeaconTx::RoundRobinPolicy policy (QComboBox const& combo);
  void setPolicy (QComboBox& combo, BeaconTx::RoundRobinPolicy const& policy);
}

#endif
