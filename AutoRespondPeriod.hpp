#ifndef AUTORESPONDPERIOD_HPP
#define AUTORESPONDPERIOD_HPP

#include <QDateTime>
#include <QString>

#include "AutoRespondPolicy.hpp"

class DecodedText;

bool isDirectAutoRespondCandidate(DecodedText const& message, QString const& myCall);

class AutoRespondPeriodState final
{
public:
  bool setEnableTx(bool enabled);
  bool observePeriod(QDateTime const& periodStart, bool receivePeriod, bool pendingCq,
                     AutoRespondPolicy policy);
  bool armCurrentReceivePeriod(bool pendingCq, AutoRespondPolicy policy);
  void disarm();
  void close();

  bool accepts(QDateTime const& decodePeriodStart) const;
  bool claimFirst(QDateTime const& decodePeriodStart);
  AutoRespondPolicy policy() const;

private:
  QDateTime m_lastObservedPeriod;
  QDateTime m_receivePeriod;
  AutoRespondPolicy m_policy {AutoRespondPolicy::None};
  bool m_enableTx {false};
  bool m_currentPeriodIsReceive {false};
  bool m_open {false};
  bool m_firstClaimed {false};
};

#endif
