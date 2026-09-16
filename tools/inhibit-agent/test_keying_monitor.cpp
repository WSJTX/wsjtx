// Self-running hang-policy checks (no serial / no UDP).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "keying_monitor.hpp"

#include <cstdio>

static int g_fails = 0;

#define CHECK(cond) do { \
  if (!(cond)) { \
    std::fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    ++g_fails; \
  } \
} while (0)

static void test_continuous_ssb_hang_zero ()
{
  KeyingMonitor m;
  m.on_key_assert (0);
  int hang = m.on_key_open (2000);   // 2 s PTT
  CHECK (hang == 0);
  CHECK (m.key_class () == KeyClass::Continuous);
}

static void test_mark_over_500_is_continuous ()
{
  KeyingMonitor m;
  m.on_key_assert (0);
  int hang = m.on_key_open (500);
  CHECK (hang == 0);
  CHECK (m.key_class () == KeyClass::Continuous);
}

static void test_breakin_short_mark_has_hang ()
{
  KeyingMonitor m;
  m.on_key_assert (0);
  int hang = m.on_key_open (60);     // 20 WPM dit
  CHECK (hang >= KeyingMonitor::kHangMinMs);
  CHECK (hang <= KeyingMonitor::kHangMaxMs);
  CHECK (m.key_class () == KeyClass::BreakInCw || m.dit_ms () > 0.0);
}

static void test_breakin_rekey_during_hang ()
{
  KeyingMonitor m;
  m.on_key_assert (0);
  int hang = m.on_key_open (60);
  CHECK (hang > 0);
  m.on_key_assert (60 + 80);         // gap shorter than hang
  hang = m.on_key_open (60 + 80 + 180);
  CHECK (hang > 0);
  CHECK (m.key_class () == KeyClass::BreakInCw);
}

static void test_hang_covers_word_gap_sizing ()
{
  KeyingMonitor m;
  m.on_key_assert (0);
  m.on_key_open (60);
  int hang = m.hang_ms_break_in ();
  // 1.5 × 7 × 60 = 630, inside 315..1260
  CHECK (hang == 630);
}

int main ()
{
  test_continuous_ssb_hang_zero ();
  test_mark_over_500_is_continuous ();
  test_breakin_short_mark_has_hang ();
  test_breakin_rekey_during_hang ();
  test_hang_covers_word_gap_sizing ();
  if (g_fails)
    {
      std::fprintf (stderr, "%d check(s) failed\n", g_fails);
      return 1;
    }
  std::printf ("test_keying_monitor: 5/5 passed\n");
  return 0;
}
