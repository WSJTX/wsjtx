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
  int ft8ThreadCount {0};
  int decodeDepth {0};
  int ft8Cycles {0};
  int ft8Sensitivity {0};
  int ft8RxFrequencySensitivity {0};
  int decodeLowFrequency {0};
  int decodeHighFrequency {0};
  bool ft8WideDxCallSearch {false};
  bool hideFt8DuplicateMessages {false};
  bool ft8ApEnabled {false};
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

  bool hasSameFt8PendingIdentity (DecodeOperatingContext const& other) const
  {
    return "FT8" == mode
      && "FT8" == other.mode
      && hasSameDecodeIdentity (other)
      && ft8ThreadCount == other.ft8ThreadCount
      && decodeDepth == other.decodeDepth
      && ft8Cycles == other.ft8Cycles
      && ft8Sensitivity == other.ft8Sensitivity
      && ft8RxFrequencySensitivity == other.ft8RxFrequencySensitivity
      && decodeLowFrequency == other.decodeLowFrequency
      && decodeHighFrequency == other.decodeHighFrequency
      && ft8WideDxCallSearch == other.ft8WideDxCallSearch
      && hideFt8DuplicateMessages == other.hideFt8DuplicateMessages
      && ft8ApEnabled == other.ft8ApEnabled;
  }
};

#endif
