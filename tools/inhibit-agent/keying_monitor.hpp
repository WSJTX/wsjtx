// KEYing monitor — same hang policy as inhibit-test (docs/TX_INHIBIT.md §3).
// Break-in CW: hang = 1.5 × word gap (10.5 × dit), clamp 315–1260 ms.
// Continuous KEY (SSB / non-break-in): hang = 0. No software PTT debounce.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef INHIBIT_AGENT_KEYING_MONITOR_HPP_
#define INHIBIT_AGENT_KEYING_MONITOR_HPP_

#include <QtGlobal>

#include <algorithm>
#include <cmath>

enum class KeyClass
{
  Unknown,
  BreakInCw,
  Continuous
};

class KeyingMonitor
{
public:
  static int const kHangMinMs = 315;          // ~40 WPM
  static int const kHangMaxMs = 1260;         // ~10 WPM
  static int const kContinuousMarkMs = 500;
  static double constexpr kHangWordGapMult = 1.5;
  static int const kWordGapDits = 7;
  static double constexpr kMaxIntraTxGapDits = 5.0;

  void reset_transmission ()
  {
    key_class_ = KeyClass::Unknown;
    dit_ms_ = 0.0;
    mark_open_ = false;
    mark_start_ms_ = -1;
    gap_start_ms_ = -1;
  }

  void on_key_assert (qint64 now_ms)
  {
    if (gap_start_ms_ >= 0)
      {
        int gap = static_cast<int> (now_ms - gap_start_ms_);
        on_gap_closed (gap);
        gap_start_ms_ = -1;
      }
    mark_open_ = true;
    mark_start_ms_ = now_ms;
  }

  // KEY open — hang_ms to wait before EOT (0 = release now).
  int on_key_open (qint64 now_ms)
  {
    int mark_ms = 0;
    if (mark_open_ && mark_start_ms_ >= 0)
      {
        mark_ms = static_cast<int> (now_ms - mark_start_ms_);
        note_mark (mark_ms);
      }
    mark_open_ = false;
    mark_start_ms_ = -1;
    gap_start_ms_ = now_ms;

    if (key_class_ == KeyClass::Continuous
        || (key_class_ == KeyClass::Unknown && mark_ms >= kContinuousMarkMs))
      {
        key_class_ = KeyClass::Continuous;
        return 0;
      }
    if (key_class_ == KeyClass::BreakInCw || dit_ms_ > 0.0)
      {
        key_class_ = KeyClass::BreakInCw;
        return hang_ms_break_in ();
      }
    if (mark_ms > 0 && mark_ms < kContinuousMarkMs)
      {
        note_dit_sample (static_cast<double> (mark_ms));
        return hang_ms_break_in ();
      }
    return 0;
  }

  KeyClass key_class () const { return key_class_; }
  double dit_ms () const { return dit_ms_; }

  int hang_ms_break_in () const
  {
    if (dit_ms_ <= 0.0)
      {
        return kHangMinMs;
      }
    double hang = kHangWordGapMult * static_cast<double> (kWordGapDits) * dit_ms_;
    int h = static_cast<int> (std::lround (hang));
    return std::max (kHangMinMs, std::min (kHangMaxMs, h));
  }

  char const * class_name () const
  {
    switch (key_class_)
      {
      case KeyClass::BreakInCw: return "break-in CW";
      case KeyClass::Continuous: return "continuous (non-break-in/SSB)";
      default: return "unknown";
      }
  }

private:
  void note_mark (int mark_ms)
  {
    if (mark_ms <= 0)
      {
        return;
      }
    if (mark_ms >= kContinuousMarkMs && key_class_ != KeyClass::BreakInCw)
      {
        key_class_ = KeyClass::Continuous;
        return;
      }
    if (dit_ms_ <= 0.0)
      {
        if (mark_ms < kContinuousMarkMs)
          {
            note_dit_sample (static_cast<double> (mark_ms));
          }
        return;
      }
    if (mark_ms < 2.0 * dit_ms_)
      {
        note_dit_sample (static_cast<double> (mark_ms));
      }
  }

  void note_dit_sample (double sample_ms)
  {
    if (sample_ms <= 0.0)
      {
        return;
      }
    if (dit_ms_ <= 0.0)
      {
        dit_ms_ = sample_ms;
      }
    else
      {
        dit_ms_ = 0.35 * sample_ms + 0.65 * dit_ms_;
      }
  }

  void on_gap_closed (int gap_ms)
  {
    if (gap_ms <= 0)
      {
        return;
      }
    double max_gap = (dit_ms_ > 0.0)
      ? kMaxIntraTxGapDits * dit_ms_
      : static_cast<double> (kContinuousMarkMs);
    if (gap_ms <= max_gap)
      {
        key_class_ = KeyClass::BreakInCw;
      }
  }

  KeyClass key_class_ {KeyClass::Unknown};
  double dit_ms_ {0.0};
  bool mark_open_ {false};
  qint64 mark_start_ms_ {-1};
  qint64 gap_start_ms_ {-1};
};

#endif
