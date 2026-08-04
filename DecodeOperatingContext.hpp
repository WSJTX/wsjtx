#ifndef DECODE_OPERATING_CONTEXT_HPP
#define DECODE_OPERATING_CONTEXT_HPP

#include <QDateTime>
#include <QString>

#include "Radio.hpp"
#include "SpecialOperatingActivity.hpp"

struct DecodeOperatingContext
{
  QString mode;
  SpecialOperatingActivity specOp {SpecialOperatingActivity::NONE};
  Radio::Frequency periodFrequency {0};
  QString band;
  QDateTime sequenceStart;
  double trPeriod {0.0};
  int submode {0};
  bool diskData {false};
  bool multithreadFt8 {false};
  int ft8DecoderStart {0};
  bool superFox {false};
  QString myCall;

  bool hasSameDecodeIdentity (DecodeOperatingContext const& other) const
  {
    return mode == other.mode
      && specOp == other.specOp
      && periodFrequency == other.periodFrequency
      && band == other.band
      && trPeriod == other.trPeriod
      && submode == other.submode
      && diskData == other.diskData
      && multithreadFt8 == other.multithreadFt8
      && ft8DecoderStart == other.ft8DecoderStart
      && superFox == other.superFox
      && myCall == other.myCall;
  }
};

#endif
