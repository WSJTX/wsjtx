#ifndef RECEIVE_AUDIO_HPP
#define RECEIVE_AUDIO_HPP

#include "commons.h"
#include "DecDataMutex.hpp"
#include <QMetaType>
#include <QMutexLocker>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

struct ReceiveAudioSourceState
{
  quint64 epoch = 0;
  int frames = 0;
};

struct ReceiveAudioBlock
{
  quint64 epoch;
  int start;
  int periodFrames;
  std::vector<short> samples;
  std::shared_ptr<ReceiveAudioSourceState const> source;
  int end () const { return start + int (samples.size ()); }
  int sourceFrames () const { return source ? source->frames : 0; }
};
using ReceiveAudio = std::shared_ptr<ReceiveAudioBlock const>;
Q_DECLARE_METATYPE (ReceiveAudio)

inline std::atomic<quint64>& receiveAudioEpochCounter ()
{
  static std::atomic<quint64> counter {0};
  return counter;
}

// Producer methods are called under dec_data_mutex. Each receive backend owns
// independent staging storage; queued blocks retain source freshness state.
class ReceiveAudioProducer
{
public:
  ReceiveAudioProducer ()
    : data_ {new dec_data_t {}}
    , source_ {std::make_shared<ReceiveAudioSourceState> ()}
  {
  }

  dec_data_t& data () { return *data_; }
  int frames () const { return source_->frames; }
  int capturedEnd () const { return capturedEnd_; }

  void setFrames (int frames)
  {
    data_->params.kin = frames;
    source_->frames = frames;
  }

  void reset (double period)
  {
    source_->epoch = receiveAudioEpochCounter ().fetch_add (1) + 1;
    capturedEnd_ = 0;
    auto const capacity = sizeof data_->d2 / sizeof data_->d2[0];
    auto const periodFrames = static_cast<std::size_t> (
      std::ceil (period * RX_SAMPLE_RATE));
    std::fill_n (data_->d2, std::min (capacity, periodFrames), qint16 {0});
    setFrames (0);
  }

  ReceiveAudio capture (int end, double period)
  {
    static int const registered = qRegisterMetaType<ReceiveAudio> ("ReceiveAudio");
    (void) registered;
    auto block = std::make_shared<ReceiveAudioBlock> ();
    block->epoch = source_->epoch;
    block->start = capturedEnd_;
    block->periodFrames = std::min (NTMAX * RX_SAMPLE_RATE,
                                    int (period * RX_SAMPLE_RATE));
    block->samples.assign (data_->d2 + capturedEnd_, data_->d2 + end);
    block->source = source_;
    capturedEnd_ = end;
    return block;
  }

private:
  std::unique_ptr<dec_data_t> data_;
  std::shared_ptr<ReceiveAudioSourceState> source_;
  int capturedEnd_ = 0;
};

// GUI-owned assembly. Reject obsolete periods (their timestamps and settings
// need no longer describe current operation), gaps and duplicate delivery.
// DSP may mutate the accepted buffer without affecting producer storage.
class ReceiveAudioConsumer
{
public:
  bool accept (ReceiveAudio const& block, dec_data_t& target)
  {
    {
      QMutexLocker lock {&dec_data_mutex ()};
      if (!block || !block->source || block->epoch != block->source->epoch
          || block->epoch <= invalidThrough_) return false;
    }
    if (block->samples.empty () || block->start < 0 || block->end () > NTMAX * RX_SAMPLE_RATE
        || block->periodFrames < 0 || block->periodFrames > NTMAX * RX_SAMPLE_RATE)
      return false;
    if (block->epoch != epoch_)
      {
        if (block->epoch < epoch_ || block->start != 0) return false;
        auto const clearFrames = std::min (NTMAX * RX_SAMPLE_RATE,
          std::max ({periodFrames_, block->periodFrames, target.params.kin}));
        std::fill_n (target.d2, clearFrames, short {0});
        periodFrames_ = block->periodFrames;
        target.params.kin = 0;
        epoch_ = block->epoch;
      }
    if (block->start != target.params.kin) return false;
    std::copy (block->samples.begin (), block->samples.end (), target.d2 + block->start);
    target.params.kin = block->end ();
    return true;
  }
  void invalidate ()
  {
    QMutexLocker lock {&dec_data_mutex ()};
    invalidThrough_ = receiveAudioEpochCounter ().load ();
    epoch_ = 0;
  }
  quint64 epoch () const { return epoch_; }
private:
  quint64 epoch_ = 0;
  quint64 invalidThrough_ = 0;
  int periodFrames_ = 0;
};
#endif
