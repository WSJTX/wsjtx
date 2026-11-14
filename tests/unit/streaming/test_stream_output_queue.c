// SPDX-License-Identifier: GPL-3.0-or-later
// lib/stream_decode.c's output queue as lib/streaming_emit.f90 drives it:
// the writer lock, stream_rows_put, the writer thread and stream_rows_quiet,
// from threads that each run one step at a time, with a writer callback that
// can be held. Run as "order", "group", "quiet_inside", "quiet_wakes",
// "quiet_outside" or "quiet_race". Every wait is bounded, and a watchdog ends
// a scenario that hangs.
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE   // lib/stream_decode.c's <link.h>, before any system header
#endif
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int test_mutex_unlock (pthread_mutex_t *mutex);
static int test_cond_wait (pthread_cond_t *cond, pthread_mutex_t *mutex);

#define pthread_mutex_unlock test_mutex_unlock
#define pthread_cond_wait test_cond_wait
#include "stream_decode.c"
#undef pthread_mutex_unlock
#undef pthread_cond_wait

// lib/stream_stdin.c, which the output queue does not use.
void stream_stdin_set_decode (void (*lock_queue) (void), void (*unlock_queue) (void), void (*wake) (void))
{
  (void) lock_queue;
  (void) unlock_queue;
  (void) wake;
}

void stream_stdin_set_working (int working)
{
  (void) working;
}

int stream_stdin_blocked_before (double t)
{
  (void) t;
  return 0;
}

double stream_stdin_now (void)
{
  struct timespec now;
  clock_gettime (CLOCK_MONOTONIC, &now);
  return (double) now.tv_sec + 1e-9 * (double) now.tv_nsec;
}

static void sleep_ms (int ms)
{
  struct timespec d = {ms / 1000, (ms % 1000) * 1000000L};
  nanosleep (&d, NULL);
}

static int failures;

static void check (char const *what, int ok)
{
  printf ("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++failures;
}

// A failure that leaves a thread stuck: report and end now.
static void stuck (char const *what)
{
  printf ("FAIL  %s\n", what);
  fflush (stdout);
  _Exit (1);
}

static void *watchdog (void *unused)
{
  (void) unused;
  sleep_ms (30000);
  stuck ("timed out");
  return NULL;
}

// Threads waiting on rows_room (quiet's wait for an empty queue), and a hook
// run once on one thread just after it next releases the writer lock.
static atomic_int room_waiters;
static pthread_t hook_thread;
static atomic_int hook_armed;
static void (*hook) (void);

static int test_mutex_unlock (pthread_mutex_t *mutex)
{
  int const result = pthread_mutex_unlock (mutex);
  if (mutex == &writer && atomic_load (&hook_armed) && pthread_equal (pthread_self (), hook_thread))
    {
      atomic_store (&hook_armed, 0);
      hook ();
    }
  return result;
}

static int test_cond_wait (pthread_cond_t *cond, pthread_mutex_t *mutex)
{
  if (cond == &rows_room) atomic_fetch_add (&room_waiters, 1);
  int const result = pthread_cond_wait (cond, mutex);
  if (cond == &rows_room) atomic_fetch_sub (&room_waiters, 1);
  return result;
}

// --- the writer's output -------------------------------------------------------

#define MAX_OUT 64

static pthread_mutex_t out_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_changed = PTHREAD_COND_INITIALIZER;
static int gate_closed;
static char out_text[MAX_OUT][16];
static int64_t out_dropped[MAX_OUT];
static int out_count;

static void gate (int closed)
{
  pthread_mutex_lock (&out_lock);
  gate_closed = closed;
  pthread_cond_broadcast (&gate_changed);
  pthread_mutex_unlock (&out_lock);
}

static void write_entry (char const *text, int64_t size, int64_t dropped)
{
  pthread_mutex_lock (&out_lock);
  while (gate_closed) pthread_cond_wait (&gate_changed, &out_lock);
  if (out_count < MAX_OUT)
    {
      int const n = size < 15 ? (int) size : 15;
      memcpy (out_text[out_count], text, (size_t) n);
      out_text[out_count][n] = '\0';
      out_dropped[out_count] = dropped;
      ++out_count;
    }
  pthread_mutex_unlock (&out_lock);
}

static int written (void)
{
  pthread_mutex_lock (&out_lock);
  int const n = out_count;
  pthread_mutex_unlock (&out_lock);
  return n;
}

// The entries written, space-separated, a rows_dropped count as "+N" before
// its entry.
static void output (char *text, size_t size)
{
  pthread_mutex_lock (&out_lock);
  text[0] = '\0';
  for (int i = 0; i < out_count; ++i)
    {
      size_t const at = strlen (text);
      if (out_dropped[i])
        snprintf (text + at, size - at, "%s+%lld %s", i ? " " : "", (long long) out_dropped[i], out_text[i]);
      else
        snprintf (text + at, size - at, "%s%s", i ? " " : "", out_text[i]);
    }
  pthread_mutex_unlock (&out_lock);
}

static int output_is (char const *want)
{
  char got[1024];
  output (got, sizeof got);
  if (!strcmp (got, want)) return 1;
  printf ("      written \"%s\", expected \"%s\"\n", got, want);
  return 0;
}

// Whether the writer is idle with nothing it may write: the queue empty, or
// only droppable entries in it while a group is open.
static int writer_idle (void)
{
  pthread_mutex_lock (&rows);
  int const idle = !rows_writing && (!rows_head || (rows_head->droppable && rows_group));
  pthread_mutex_unlock (&rows);
  return idle;
}

// The writer has taken an entry and is writing it.
static int writer_busy (void)
{
  pthread_mutex_lock (&rows);
  int const busy = rows_writing;
  pthread_mutex_unlock (&rows);
  return busy;
}

static int group_open (void)
{
  pthread_mutex_lock (&rows);
  int const open = rows_group;
  pthread_mutex_unlock (&rows);
  return open;
}

static int wait_for (int (*done) (void), int ms)
{
  for (int waited = 0; !done (); ++waited)
    {
      if (waited >= ms) return 0;
      sleep_ms (1);
    }
  return 1;
}

// --- the emitting threads -----------------------------------------------------

// streaming_emit.f90's put_: the line under one more hold of the writer lock.
static void put (char const *text, int droppable)
{
  stream_writer_lock ();
  stream_rows_put (text, (int64_t) strlen (text), 1, droppable);
  stream_writer_unlock ();
}

enum step_kind {HOLD, UNHOLD, PUT, QUIET, ARM_HOOK};

struct step
{
  enum step_kind kind;
  char const *text;
  int droppable;
};

// A thread that runs the steps given it in order; done counts those run.
struct actor
{
  pthread_t thread;
  pthread_mutex_t lock;
  pthread_cond_t posted;
  struct step steps[16];
  int given;
  atomic_int done;
};

static void *actor_main (void *argument)
{
  struct actor *a = argument;
  for (int next = 0;; ++next)
    {
      pthread_mutex_lock (&a->lock);
      while (next == a->given) pthread_cond_wait (&a->posted, &a->lock);
      struct step const s = a->steps[next];
      pthread_mutex_unlock (&a->lock);
      switch (s.kind)
        {
        case HOLD: stream_writer_lock (); break;
        case UNHOLD: stream_writer_unlock (); break;
        case PUT: put (s.text, s.droppable); break;
        case QUIET: stream_rows_quiet (); break;
        case ARM_HOOK:
          hook_thread = pthread_self ();
          atomic_store (&hook_armed, 1);
          break;
        }
      atomic_fetch_add (&a->done, 1);
    }
  return NULL;
}

static void start (struct actor *a)
{
  memset (a, 0, sizeof *a);
  atomic_init (&a->done, 0);
  pthread_mutex_init (&a->lock, NULL);
  pthread_cond_init (&a->posted, NULL);
  if (pthread_create (&a->thread, NULL, actor_main, a) != 0) stuck ("no actor thread");
  pthread_detach (a->thread);
}

static void give (struct actor *a, enum step_kind kind, char const *text, int droppable)
{
  pthread_mutex_lock (&a->lock);
  a->steps[a->given++] = (struct step) {kind, text, droppable};
  pthread_cond_signal (&a->posted);
  pthread_mutex_unlock (&a->lock);
}

// Waits until the actor has run all its steps; 0 after ms.
static int finished (struct actor *a, int ms)
{
  for (int waited = 0; atomic_load (&a->done) < a->given; ++waited)
    {
      if (waited >= ms) return 0;
      sleep_ms (1);
    }
  return 1;
}

static void run (struct actor *a, enum step_kind kind, char const *text, int droppable, char const *what)
{
  give (a, kind, text, droppable);
  if (!finished (a, 5000)) stuck (what);
}

// The writer started and held on a first entry, so that what is put next
// waits in the queue.
static void begin (void)
{
  if (!stream_rows_start (write_entry)) stuck ("no writer thread");
  gate (1);
  put ("W", 0);
  if (!wait_for (writer_busy, 5000)) stuck ("the writer took no entry");
}

static void end (void)
{
  stream_writer_lock ();
  stream_rows_end ();
  stream_writer_unlock ();
}

// --- the scenarios --------------------------------------------------------------

// Lines that are not droppable keep their order and go ahead of every
// droppable one queued.
static void order (void)
{
  begin ();
  put ("R0", 1);
  put ("R1", 1);
  put ("N0", 0);
  put ("R2", 1);
  put ("N1", 0);
  put ("N2", 0);
  put ("R3", 1);
  gate (0);
  check ("every entry written", wait_for (writer_idle, 5000) && 8 == written ());
  check ("the others in order, ahead of the droppable ones", output_is ("W N0 N1 N2 R0 R1 R2 R3"));
  end ();
}

// While a held group is open the writer writes no droppable entry, even with
// none of the group's lines left to write; the outermost unlock lets them go.
static void group (void)
{
  begin ();
  put ("R0", 1);
  put ("R1", 1);
  stream_writer_lock ();
  put ("G0", 0);
  gate (0);
  check ("the group's first line written", wait_for (writer_idle, 5000) && output_is ("W G0"));
  sleep_ms (200);
  check ("no droppable entry while the group's lines have run out", output_is ("W G0"));
  put ("G1", 0);
  check ("the group's next line written", wait_for (writer_idle, 5000) && output_is ("W G0 G1"));
  sleep_ms (200);
  check ("still none at depth 1 after the group's lines", output_is ("W G0 G1"));
  stream_writer_unlock ();
  check ("the droppable entries after the outermost unlock",
         wait_for (writer_idle, 5000) && output_is ("W G0 G1 R0 R1"));
  end ();
}

// The group's holder quiets inside it: quiet returns, having discarded the
// droppable entries, and the next droppable entry written carries their count.
static void quiet_inside (void)
{
  struct actor holder;
  begin ();
  put ("R0", 1);
  put ("R1", 1);
  put ("R2", 1);
  start (&holder);
  run (&holder, HOLD, NULL, 0, "the holder's lock");
  run (&holder, PUT, "G0", 0, "the group's line");
  gate (0);
  if (!wait_for (writer_idle, 5000)) stuck ("the writer did not write the group's line");
  give (&holder, QUIET, NULL, 0);
  check ("quiet inside the group returns", finished (&holder, 5000));
  if (atomic_load (&holder.done) < holder.given) stuck ("quiet inside the group never returned");
  run (&holder, UNHOLD, NULL, 0, "the holder's unlock");
  put ("R3", 1);
  check ("the droppable entries discarded and counted before the next",
         wait_for (writer_idle, 5000) && output_is ("W G0 +3 R3"));
  end ();
}

// A thread that does not hold the lock waits in quiet for an empty queue;
// the group's holder then quiets and its discard empties the queue, which
// wakes the waiting thread.
static void quiet_wakes (void)
{
  struct actor holder, waiter;
  begin ();
  put ("R0", 1);
  put ("R1", 1);
  put ("R2", 1);
  start (&holder);
  start (&waiter);
  run (&holder, HOLD, NULL, 0, "the holder's lock");
  run (&holder, PUT, "S0", 0, "the group's line");
  give (&waiter, QUIET, NULL, 0);
  for (int waited = 0; atomic_load (&room_waiters) < 1; ++waited)
    if (waited > 5000) stuck ("the waiter never waited in quiet");
    else sleep_ms (1);
  gate (0);
  if (!wait_for (writer_idle, 5000)) stuck ("the writer did not write the group's line");
  check ("the waiter waits while droppable entries are queued",
         0 == atomic_load (&waiter.done) && output_is ("W S0"));
  give (&holder, QUIET, NULL, 0);
  if (!finished (&holder, 5000)) stuck ("quiet inside the group never returned");
  check ("the discard wakes the waiter's quiet", finished (&waiter, 2000));
  run (&holder, UNHOLD, NULL, 0, "the holder's unlock");
  if (!finished (&waiter, 0))
    {
      put ("LATER", 0);
      if (!finished (&waiter, 5000)) stuck ("the waiter's quiet never returned");
    }
  end ();
}

// A thread that does not hold the lock quiets while another thread's group
// is open: nothing is discarded, and the droppable entries are written once
// the group closes, after which quiet returns.
static void quiet_outside (void)
{
  struct actor holder, waiter;
  begin ();
  put ("R0", 1);
  put ("R1", 1);
  put ("R2", 1);
  start (&holder);
  start (&waiter);
  run (&holder, HOLD, NULL, 0, "the holder's lock");
  run (&holder, PUT, "S0", 0, "the group's first line");
  give (&waiter, QUIET, NULL, 0);
  for (int waited = 0; atomic_load (&room_waiters) < 1; ++waited)
    if (waited > 5000) stuck ("the waiter never waited in quiet");
    else sleep_ms (1);
  gate (0);
  if (!wait_for (writer_idle, 5000)) stuck ("the writer did not write the group's first line");
  sleep_ms (200);
  check ("quiet waits while the group is open", 0 == atomic_load (&waiter.done) && output_is ("W S0"));
  run (&holder, PUT, "S1", 0, "the group's second line");
  run (&holder, UNHOLD, NULL, 0, "the holder's unlock");
  if (!finished (&waiter, 5000)) stuck ("the waiter's quiet never returned");
  check ("the droppable entries written, none discarded, after the group",
         output_is ("W S0 S1 R0 R1 R2"));
  end ();
}

// The same, with the group opened just after quiet found the writer lock
// free and before it looks at the queue: quiet still discards nothing.
static struct actor race_holder;

static void open_group (void)
{
  give (&race_holder, HOLD, NULL, 0);
  give (&race_holder, PUT, "S0", 0);
  if (!finished (&race_holder, 5000) || !group_open ()) stuck ("the group did not open");
}

static void quiet_race (void)
{
  struct actor waiter;
  begin ();
  put ("R0", 1);
  put ("R1", 1);
  put ("R2", 1);
  start (&race_holder);
  start (&waiter);
  hook = open_group;
  run (&waiter, ARM_HOOK, NULL, 0, "arming the hook");
  give (&waiter, QUIET, NULL, 0);
  for (int waited = 0; atomic_load (&room_waiters) < 1; ++waited)
    if (waited > 5000) stuck ("the waiter never waited in quiet");
    else sleep_ms (1);
  check ("the group opened inside quiet", group_open () && !atomic_load (&hook_armed));
  gate (0);
  if (!wait_for (writer_idle, 5000)) stuck ("the writer did not write the group's line");
  run (&race_holder, UNHOLD, NULL, 0, "the holder's unlock");
  if (!finished (&waiter, 5000)) stuck ("the waiter's quiet never returned");
  check ("the droppable entries written, none discarded", output_is ("W S0 R0 R1 R2"));
  end ();
}

int main (int argc, char **argv)
{
  pthread_t dog;
  setvbuf (stdout, NULL, _IONBF, 0);
  if (argc != 2) return 2;
  if (pthread_create (&dog, NULL, watchdog, NULL) != 0) return 2;
  pthread_detach (dog);
  if (!strcmp (argv[1], "order")) order ();
  else if (!strcmp (argv[1], "group")) group ();
  else if (!strcmp (argv[1], "quiet_inside")) quiet_inside ();
  else if (!strcmp (argv[1], "quiet_wakes")) quiet_wakes ();
  else if (!strcmp (argv[1], "quiet_outside")) quiet_outside ();
  else if (!strcmp (argv[1], "quiet_race")) quiet_race ();
  else return 2;
  return failures ? 1 : 0;
}
