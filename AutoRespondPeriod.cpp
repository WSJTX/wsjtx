#include "AutoRespondPeriod.hpp"

#include <QRegularExpression>
#include <QStringList>

#include "Decoder/decodedtext.h"
#include "Radio.hpp"

namespace
{
  QRegularExpression const continuingExchange {R"(^(?:R(?:[-+]\d+)?|RRR|RR73|73)$)"};
}

bool isDirectAutoRespondCandidate(DecodedText const& message, QString const& myCall)
{
  if (!message.isStandardMessage() || myCall.isEmpty()) return false;

  auto const words = message.messageWords();
  if (words.size() < 5) return false;

  auto const recipient = words.at(2);
  auto const sender = words.at(3);
  auto const exchange = words.at(4);
  auto const myBaseCall = Radio::base_callsign(myCall);
  bool const addressedToUs = myCall == myBaseCall ? recipient == myBaseCall : recipient == myCall;

  return addressedToUs
    && Radio::is_callsign(sender)
    && Radio::base_callsign(sender) != myBaseCall
    && !exchange.isEmpty()
    && !exchange.contains(continuingExchange);
}

bool AutoRespondPeriodState::setEnableTx(bool enabled)
{
  if (enabled == m_enableTx) return false;

  m_enableTx = enabled;
  if (!enabled) disarm();
  return true;
}

bool AutoRespondPeriodState::observePeriod(QDateTime const& periodStart, bool receivePeriod,
                                           bool pendingCq, AutoRespondPolicy policy)
{
  if (!periodStart.isValid() || periodStart == m_lastObservedPeriod) return false;

  m_lastObservedPeriod = periodStart;
  m_currentPeriodIsReceive = receivePeriod;
  if (!receivePeriod) return false;

  m_receivePeriod = periodStart;
  m_firstClaimed = false;
  m_policy = AutoRespondPolicy::None;
  m_open = m_enableTx
    && pendingCq
    && AutoRespondPolicy::None != policy;
  if (m_open) m_policy = policy;
  return m_open;
}

bool AutoRespondPeriodState::armCurrentReceivePeriod(bool pendingCq,
                                                     AutoRespondPolicy policy)
{
  if (m_open || !m_enableTx || !m_currentPeriodIsReceive || !pendingCq
      || AutoRespondPolicy::None == policy) return false;

  m_open = true;
  m_policy = policy;
  m_firstClaimed = false;
  return true;
}

void AutoRespondPeriodState::disarm()
{
  m_open = false;
  m_policy = AutoRespondPolicy::None;
  m_firstClaimed = false;
}

void AutoRespondPeriodState::close()
{
  disarm();
}

bool AutoRespondPeriodState::accepts(QDateTime const& decodePeriodStart) const
{
  return m_open && decodePeriodStart.isValid() && decodePeriodStart == m_receivePeriod;
}

bool AutoRespondPeriodState::claimFirst(QDateTime const& decodePeriodStart)
{
  if (!accepts(decodePeriodStart) || m_firstClaimed) return false;
  m_firstClaimed = true;
  return true;
}

AutoRespondPolicy AutoRespondPeriodState::policy() const
{
  return m_policy;
}
