#ifndef QSOLOGDATA_HPP
#define QSOLOGDATA_HPP

#include <QDateTime>
#include <QString>

#include "Radio.hpp"
#include "SpecialOperatingActivity.hpp"

struct QsoLogData
{
  QString call;
  QString grid;
  QString mode;
  QString rptSent;
  QString rptRcvd;
  QDateTime dateTimeOn;
  QDateTime dateTimeOff;
  Radio::Frequency dialFreq {0};
  bool noSuffix {false};
  QString xSent;
  QString xRcvd;
  SpecialOperatingActivity specOp {SpecialOperatingActivity::NONE};
  QString myCall;
  QString myGrid;
  QString operatorCall;
  QString contestName;
  bool individualContestName {false};
  bool superFox {false};
};

#endif
