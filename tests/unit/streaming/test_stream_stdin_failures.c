// SPDX-License-Identifier: GPL-3.0-or-later
// lib/stream_stdin.c with its allocations or its thread start failing, and its
// accounting of what it holds. Run as "memory", "thread" or "drained".
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int fail_after = -1, fail_thread, allocations;

static void *failing_malloc (size_t n)
{
  return fail_after >= 0 && ++allocations > fail_after ? NULL : malloc (n);
}

static int failing_create (pthread_t *t, pthread_attr_t const *a, void *(*f) (void *), void *arg)
{
  return fail_thread ? EAGAIN : pthread_create (t, a, f, arg);
}

#define malloc failing_malloc
#define pthread_create failing_create
#include "stream_stdin.c"
#undef malloc
#undef pthread_create

#define PIECE 1000

static int wfd, pieces, close_after;
static long pause_us;
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

static void pause_for (long us)
{
  struct timespec d = {us / 1000000, (us % 1000000) * 1000L};
  nanosleep (&d, NULL);
}

static void timed_out (int signal)
{
  static char const message[] = "FAIL  timed out: the reader delivered nothing more\n";
  (void) signal;
  if (write (1, message, sizeof message - 1) < 0) _exit (1);
  _exit (1);
}

// One write per piece, spaced so that the reader holds each as one chunk.
static void *producer (void *unused)
{
  static unsigned char b[PIECE];
  (void) unused;
  for (int p = 0; p < pieces; ++p)
    {
      for (int i = 0; i < PIECE; ++i) b[i] = pattern ((int64_t) p * PIECE + i);
      for (int done = 0; done < PIECE;)
        {
          ssize_t w = write (wfd, b + done, (size_t) (PIECE - done));
          if (w < 0 && errno == EINTR) continue;
          if (w < 0) return NULL;
          done += (int) w;
        }
      pause_for (pause_us);
    }
  if (close_after) close (wfd);
  return NULL;
}

// Reads in requests of `request` bytes until one returns short; the bytes
// delivered, the last return, and whether every byte was the one written.
static int64_t consume (int64_t request, int64_t *last, int *exact)
{
  static unsigned char b[4096];
  int64_t total = 0;
  *exact = 1;
  for (;;)
    {
      int64_t got = stream_stdin_read_fully (b, request);
      *last = got;
      for (int64_t i = 0; i < got; ++i) *exact &= b[i] == pattern (total + i);
      if (got > 0) total += got;
      if (got < request) return total;
    }
}

static int64_t held (void)
{
  pthread_mutex_lock (&lock);
  int64_t n = buffered;
  pthread_mutex_unlock (&lock);
  return n;
}

int main (int argc, char **argv)
{
  char const *mode = argc > 1 ? argv[1] : "";
  int fds[2];
  if (pipe (fds) != 0 || dup2 (fds[0], 0) < 0) { perror ("pipe"); return 2; }
  close (fds[0]);
  wfd = fds[1];
  signal (SIGALRM, timed_out);
  alarm (30);
  int64_t last = 0;
  int exact = 0;
  pthread_t thread;
  if (!strcmp (mode, "memory"))
    {
      // The reader's fourth chunk cannot be held.
      fail_after = 3;
      pieces = 5;
      pause_us = 20000;
      pthread_create (&thread, NULL, producer, NULL);
      int64_t const got = consume (500, &last, &exact);
      // Each piece is one chunk unless the reader falls behind and joins two.
      check ("out of memory: the bytes held before it are delivered",
             got >= 3 * PIECE && got < (int64_t) pieces * PIECE && exact);
      check ("then the read fails rather than ending", last == -1);
    }
  else if (!strcmp (mode, "thread"))
    {
      fail_thread = 1;
      pieces = 100;
      pause_us = 1000;
      close_after = 1;
      pthread_create (&thread, NULL, producer, NULL);
      int64_t const got = consume (4096, &last, &exact);
      check ("without a reader thread every byte arrives", got == (int64_t) pieces * PIECE && exact);
      check ("with arrival times", stream_stdin_arrival () > 0);
      check ("and the time the reads waited", stream_stdin_waited () > 0);
    }
  else if (!strcmp (mode, "drained"))
    {
      pieces = 10;
      pause_us = 20000;
      close_after = 1;
      pthread_create (&thread, NULL, producer, NULL);
      unsigned char b[1];
      stream_stdin_read_fully (b, 1);
      double const deadline = stream_stdin_now () + 10;
      while (stream_stdin_buffered_before (stream_stdin_now () + 1) < pieces * PIECE - 1
             && stream_stdin_now () < deadline)
        pause_for (1000);
      check ("what is held counts the chunks' headers", held () > pieces * PIECE - 1);
      int64_t const got = consume (4096, &last, &exact);
      check ("every byte arrives", got == pieces * PIECE - 1);
      check ("and once the input is drained nothing is held", held () == 0);
    }
  else
    {
      fprintf (stderr, "usage: %s memory|thread|drained\n", argv[0]);
      return 2;
    }
  pthread_join (thread, NULL);
  if (failures) printf ("%d CHECK(S) FAILED\n", failures);
  return failures != 0;
}
