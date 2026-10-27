// SPDX-License-Identifier: GPL-3.0-or-later
// lib/stream_stdin.c reading a pipe on fd 0: the bytes it delivers, how long
// it reports waiting, where it stops reading, and the arrival stamps the
// stream's busy model reads.
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

int64_t stream_stdin_read_fully (void *buf, int64_t n);
double stream_stdin_now (void);
double stream_stdin_arrival (void);
double stream_stdin_waited (void);
int64_t stream_stdin_buffered_before (double t);

// lib/stream_stdin.c's cap and largest read.
#define CAP ((int64_t) 16 << 20)
#define BLOCK 65536
#define PACED_WRITES 300
#define PACED_SIZE 997
#define BIG ((int64_t) 20 << 20)

static int wfd;
static atomic_int step;
static atomic_llong written;
static int failures;

static unsigned char pattern (int64_t i)
{
  uint64_t x = (uint64_t) (i + 1) * 0x9E3779B97F4A7C15ull;
  return (unsigned char) ((x ^ (x >> 29)) >> 24);
}

static void check (char const *what, int ok)
{
  printf ("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++failures;
}

static void pause_ms (long ms)
{
  struct timespec d = {ms / 1000, (ms % 1000) * 1000000L};
  nanosleep (&d, NULL);
}

static void put (int64_t n)
{
  static unsigned char b[BLOCK];
  while (n > 0)
    {
      int64_t m = n < BLOCK ? n : BLOCK;
      int64_t base = atomic_load (&written);
      for (int64_t i = 0; i < m; ++i) b[i] = pattern (base + i);
      for (int64_t done = 0; done < m;)
        {
          ssize_t w = write (wfd, b + done, (size_t) (m - done));
          if (w < 0 && errno == EINTR) continue;
          if (w < 0) { perror ("write"); exit (2); }
          done += w;
        }
      atomic_fetch_add (&written, m);
      n -= m;
    }
}

static void wait_step (int s)
{
  while (atomic_load (&step) < s) pause_ms (1);
}

// A reader that delivers nothing fails the test rather than hanging it.
static void timed_out (int signal)
{
  static char const message[] = "FAIL  timed out: the reader delivered nothing more\n";
  (void) signal;
  if (write (1, message, sizeof message - 1) < 0) _exit (1);
  _exit (1);
}

static void *producer (void *unused)
{
  (void) unused;
  wait_step (1);
  pause_ms (100);
  put (4);
  pause_ms (50);
  put (4);
  wait_step (2);
  put (100);
  wait_step (3);
  for (int i = 0; i < PACED_WRITES; ++i)
    {
      put (PACED_SIZE);
      pause_ms (1);
    }
  wait_step (4);
  put (BIG);
  wait_step (5);
  put (10);
  close (wfd);
  return NULL;
}

static int64_t consumed;
static int exact = 1, monotonic = 1;
static double last_arrival = -1.0;

// Reads n bytes in requests of at most a megabyte; the count delivered.
static int64_t consume (int64_t n)
{
  static unsigned char b[1 << 20];
  int64_t total = 0;
  while (total < n)
    {
      int64_t want = n - total < (int64_t) sizeof b ? n - total : (int64_t) sizeof b;
      int64_t got = stream_stdin_read_fully (b, want);
      for (int64_t i = 0; i < got; ++i) exact &= b[i] == pattern (consumed + i);
      consumed += got > 0 ? got : 0;
      total += got > 0 ? got : 0;
      if (got > 0)
        {
          monotonic &= stream_stdin_arrival () >= last_arrival;
          last_arrival = stream_stdin_arrival ();
        }
      if (got < want) break;
    }
  return total;
}

int main (void)
{
  int fds[2];
  if (pipe (fds) != 0 || dup2 (fds[0], 0) < 0) { perror ("pipe"); return 2; }
  close (fds[0]);
  wfd = fds[1];
  signal (SIGALRM, timed_out);
  alarm (30);
  pthread_t thread;
  if (pthread_create (&thread, NULL, producer, NULL) != 0) { perror ("thread"); return 2; }

  atomic_store (&step, 1);
  check ("a read whose bytes arrive later, in two writes, delivers them all", consume (8) == 8);
  double const wait = stream_stdin_waited ();
  check ("and reports how long it waited", wait >= 0.1 && wait < 1.0);

  atomic_store (&step, 2);
  double const deadline = stream_stdin_now () + 10;
  while (stream_stdin_buffered_before (stream_stdin_now () + 1) < 100 && stream_stdin_now () < deadline)
    pause_ms (1);
  check ("a read of buffered bytes delivers them", consume (100) == 100);
  check ("and reports that it did not wait", stream_stdin_waited () == 0.0);

  // Everything counted as stamped before t is read; the next byte read was stamped at t or later.
  atomic_store (&step, 3);
  int64_t const paced_end = consumed + PACED_WRITES * PACED_SIZE;
  int counted = 1, full = 1;
  while (consumed < paced_end)
    {
      double const t = stream_stdin_now ();
      int64_t n = stream_stdin_buffered_before (t);
      if (n > paced_end - consumed) n = paced_end - consumed;
      full &= consume (n) == n;
      if (consumed == paced_end) break;
      full &= consume (1) == 1;
      counted &= stream_stdin_arrival () >= t;
    }
  check ("buffered_before(t) counts every byte stamped before t", counted);

  // The producer writes 20 MiB while nothing is read.
  atomic_store (&step, 4);
  pause_ms (1000);
  int64_t const held = stream_stdin_buffered_before (stream_stdin_now () + 1);
  check ("the reader stops reading at the cap", held > CAP / 2 && held <= CAP + BLOCK);
  check ("and the producer is held back", atomic_load (&written) < paced_end + BIG);
  full &= consume (BIG) == BIG;
  check ("reading resumes and every byte arrives", full && consumed == paced_end + BIG);

  atomic_store (&step, 5);
  check ("a read past the end of input returns only what was left", consume (100) == 10);
  check ("then nothing", consume (1) == 0);
  check ("every byte is the one written", exact);
  check ("arrival stamps never go back", monotonic);
  pthread_join (thread, NULL);
  if (failures) printf ("%d CHECK(S) FAILED\n", failures);
  return failures != 0;
}
