#ifndef REFERENCE_SPECTRUM_HPP
#define REFERENCE_SPECTRUM_HPP

#include <algorithm>

// Visit each newly committed sample exactly once, independently of the mode's
// notification step. The DSP accepts chunks of at most 3456 samples; a zero
// count resets stream history without discarding accumulated measurements.
class ReferenceSpectrumInput
{
public:
  template<typename Process>
  void consume (short * samples, int end, int mode, Process process)
  {
    bool const newPeriod = end < end_;
    if (newPeriod) end_ = 0;
    if (newPeriod || mode != mode_) process (samples, 0);
    if (mode)
      while (end_ < end)
        {
          int const count = std::min (3456, end - end_);
          process (samples + end_, count);
          end_ += count;
        }
    end_ = end;
    mode_ = mode;
  }
  void reset () { end_ = 0; mode_ = -1; }
private:
  int end_ = 0;
  int mode_ = -1;
};
#endif
