#include "OperatingFrequency.hpp"

bool OperatingFrequency::requestNominal (Frequency value, Delta correction,
                                         Request const& request)
{
  if (!value || !request (value + correction)) return false;
  rx_ = tx_ = value;
  return true;
}

OperatingFrequency::Transition OperatingFrequency::enableMonitor (
  Context const& context, Request const& request)
{
  Transition result {snapshot (), snapshot (), std::nullopt};
  if (!context.monitorAllowed) return result;
  result.monitor = true;
  result.restorationAccepted = !context.monitoring && context.restore && !context.echo
    && requestNominal (remembered_, context.rxCorrection, request);
  result.after = snapshot ();
  return result;
}

OperatingFrequency::Transition OperatingFrequency::reconcile (
  Observation const& previous, Observation const& current,
  Context const& context, Request const& request)
{
  Transition result {snapshot (), snapshot (), std::nullopt};
  auto const target = remembered_;
  bool const startup = !previous.online && current.online && context.monitorAllowed;
  bool const monitoring = startup ? context.monitorAtStartup && !context.echo : context.monitoring;
  bool const restoring = startup && monitoring && !context.monitoring
    && context.restore && !context.echo;

  if (current.online && current.rx)
    {
      if (!rx_ && current.transmitting) rx_ = current.rx;
      if (!rx_ || current.rx != previous.rx || current.split != previous.split
          || current.transmitting != previous.transmitting
          || (current.transmitting && current.tx != previous.tx))
        {
          observe (current, monitoring && !(startup && context.restore && !context.echo),
                   context.rxCorrection, context.txCorrection);
        }
    }
  if (startup)
    {
      result.monitor = monitoring;
      if (restoring)
        {
          // The observation that initiates restoration cannot replace its target.
          remembered_ = target;
          result.restorationAccepted = enableMonitor (context, request).restorationAccepted;
        }
    }
  result.after = snapshot ();
  return result;
}

void OperatingFrequency::observe (Observation const& report, bool monitoring,
                                  Delta rxCorrection, Delta txCorrection)
{
  if (!report.transmitting)
    {
      auto const previousNominal = rx_;
      rx_ = report.rx - rxCorrection;
      if (previousNominal != rx_) tx_ = rx_;
      if (monitoring) remembered_ = rx_;
    }
  else
    {
      tx_ = report.split ? report.tx - txCorrection : report.rx;
    }
}
