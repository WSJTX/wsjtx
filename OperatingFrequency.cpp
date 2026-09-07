#include "OperatingFrequency.hpp"

bool OperatingFrequency::requestNominal (Frequency value, Delta correction,
                                         Request const& request)
{
  if (!value || !request (value + correction)) return false;
  rx_ = tx_ = value;
  return true;
}

bool OperatingFrequency::enableMonitor (bool prior, bool restore, bool echo,
                                        Delta correction, Request const& request)
{
  return !prior && restore && !echo
    && requestNominal (remembered_, correction, request);
}

void OperatingFrequency::observe (Observation const& report, bool monitoring,
                                  Delta rxCorrection, Delta txCorrection,
                                  Frequency previousNominal)
{
  if (!report.transmitting)
    {
      rx_ = report.rx - rxCorrection;
      if (previousNominal != rx_) tx_ = rx_;
      if (monitoring) remembered_ = rx_;
    }
  else
    {
      tx_ = report.split ? report.tx - txCorrection : report.rx;
    }
}
