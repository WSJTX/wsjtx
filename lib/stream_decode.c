// SPDX-License-Identifier: GPL-3.0-or-later
/* stream_decode.c — the stream's decode thread (lib/streaming_decode.f90):
 * its queue of operations, the frame loop's waits on it, and the stdout
 * writer lock.
 *
 * The frame loop (lib/streaming_io.f90) puts each operation on the queue in
 * stream order, with the furthest position it has read since the operation
 * before; the decode thread takes them in that order. While a final waits or
 * an early pass's completion is due, the decode thread drains first: before
 * an operation whose position is at or after the time the last decode ended,
 * and, with the queue empty, once the frame loop has read that far or is
 * blocked with nothing buffered from before it (lib/stream_stdin.c).
 *
 * Lock order: the queue lock, then the stdin reader's. Nothing holds the
 * queue lock across a decode or a stdout write. The writer lock is recursive:
 * a reply's lines take it once for the whole reply and again for each line.
 * Called via bind(C) from Fortran.
 */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <time.h>
#endif
#ifdef __linux__
#include <link.h>
#endif

/* Queued bytes, operations and their audio, past which the frame loop waits;
 * one operation is always admitted. */
#define STREAM_DECODE_CAP ((int64_t) 64 << 20)
/* The decode thread's stack, besides the static TLS glibc carves out of it. */
#define STREAM_DECODE_STACK ((size_t) 16 << 20)

/* lib/stream_stdin.c */
void stream_stdin_set_decode (void (*lock_queue) (void), void (*unlock_queue) (void), void (*wake) (void));
void stream_stdin_set_working (int working);
int stream_stdin_blocked_before (double t);
double stream_stdin_now (void);

struct stream_op {
  struct stream_op *next;
  void *op;
  double reach;
  int64_t bytes;
  int work, pass;
};

static pthread_mutex_t queue = PTHREAD_MUTEX_INITIALIZER;
/* The decode thread's wakeups, the frame loop's back-pressure wait, and its
 * wait for an empty queue before an allocation's second try. */
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static pthread_cond_t room = PTHREAD_COND_INITIALIZER;
static pthread_cond_t drained = PTHREAD_COND_INITIALIZER;
static struct stream_op *head, *tail;
static int64_t queued_bytes;
/* Operations other than configure, queued or running; and whether the decode
 * thread holds a waiting final, a completion due, decode-again requests or a
 * busy line to end. */
static int work_ops, holds;
/* An operation or a drain runs, and whether that operation is in work_ops. */
static int running, running_work;
/* The furthest position the frame loop has read since its last operation. */
static double reach = -HUGE_VAL;
/* The decode thread sleeps, and would drain once the frame loop reads to drain_at. */
static int sleeping, drain_wanted;
static double drain_at;
static int hold_take_ms;
static void (*body) (void);
static pthread_t thread;
/* WSJT_CODEC_TEST_PLAN_GATE: the frame loop has said planning. */
static pthread_cond_t planned = PTHREAD_COND_INITIALIZER;
static int gate_open;

static pthread_mutex_t writer;
static pthread_mutex_t pool = PTHREAD_MUTEX_INITIALIZER;

/* pass_progress: the running pass's generation (0: none) and fields, and when
 * the last line went out (stream_stdin_now, in ns), for every thread that
 * reports. */
static atomic_int progress_pass, progress_mode, progress_label, progress_nzhsym;
static atomic_llong progress_last = -(1LL << 62);

static void lock_queue (void)
{
  pthread_mutex_lock (&queue);
}

static void unlock_queue (void)
{
  pthread_mutex_unlock (&queue);
}

/* Under the queue lock: the frame loop is about to block for input. */
static void reader_blocks (void)
{
  if (sleeping && drain_wanted) pthread_cond_signal (&wake);
}

void stream_decode_sleep_ms (int ms)
{
#ifdef _WIN32
  Sleep ((DWORD) ms);
#else
  struct timespec left = {ms / 1000, (long) (ms % 1000) * 1000000L};
  while (nanosleep (&left, &left) != 0 && errno == EINTR) {}
#endif
}

#ifdef __linux__
static int add_tls (struct dl_phdr_info *info, size_t size, void *data)
{
  size_t *stack = data;
  (void) size;
  for (int i = 0; i < info->dlpi_phnum; ++i)
    if (info->dlpi_phdr[i].p_type == PT_TLS)
      *stack += info->dlpi_phdr[i].p_memsz + info->dlpi_phdr[i].p_align;
  return 0;
}
#endif

static void *decode_main (void *unused)
{
  (void) unused;
  body ();
  return NULL;
}

/* Start the decode thread on decode; take_ms holds it before it takes each
 * operation (a test hook). 0 when no thread could be started. */
int stream_decode_start (void (*decode) (void), int take_ms)
{
  pthread_attr_t attributes;
  pthread_mutexattr_t recursive;
  size_t stack = STREAM_DECODE_STACK;
#ifdef __linux__
  dl_iterate_phdr (add_tls, &stack);
#endif
  if (pthread_mutexattr_init (&recursive) != 0) return 0;
  int ok = pthread_mutexattr_settype (&recursive, PTHREAD_MUTEX_RECURSIVE) == 0
    && pthread_mutex_init (&writer, &recursive) == 0;
  pthread_mutexattr_destroy (&recursive);
  if (!ok || pthread_attr_init (&attributes) != 0) return 0;
  body = decode;
  hold_take_ms = take_ms;
  ok = pthread_attr_setstacksize (&attributes, stack) == 0
    && pthread_create (&thread, &attributes, decode_main, NULL) == 0;
  pthread_attr_destroy (&attributes);
  if (ok) stream_stdin_set_decode (lock_queue, unlock_queue, reader_blocks);
  return ok;
}

void stream_decode_join (void)
{
  pthread_join (thread, NULL);
  stream_stdin_set_decode (NULL, NULL, NULL);
}

/* The frame loop's operation op, of bytes; work: not a configure; pass: a
 * decode pass. 0 when there was no memory to queue it. */
int stream_decode_put (void *op, int64_t bytes, int work, int pass)
{
  struct stream_op *node = malloc (sizeof *node);
  if (!node) return 0;
  node->next = NULL;
  node->op = op;
  node->bytes = bytes + (int64_t) sizeof *node;
  node->work = work;
  node->pass = pass;
  lock_queue ();
  node->reach = reach;
  reach = -HUGE_VAL;
  if (tail) tail->next = node; else head = node;
  tail = node;
  queued_bytes += node->bytes;
  work_ops += work;
  stream_stdin_set_working (1);
  pthread_cond_signal (&wake);
  unlock_queue ();
  return 1;
}

/* The frame loop has read to position, before it acts on what it read. */
void stream_decode_publish (double position)
{
  lock_queue ();
  if (position > reach) reach = position;
  if (sleeping && drain_wanted && reach >= drain_at) pthread_cond_signal (&wake);
  unlock_queue ();
}

/* Before the frame loop queues an operation, or takes a pass's audio. */
void stream_decode_room (void)
{
  lock_queue ();
  while (head && queued_bytes >= STREAM_DECODE_CAP) pthread_cond_wait (&room, &queue);
  unlock_queue ();
}

/* Until the queue is empty and nothing runs. */
void stream_decode_idle (void)
{
  lock_queue ();
  while (head || running) pthread_cond_wait (&drained, &queue);
  unlock_queue ();
}

int stream_decode_has_work (void)
{
  lock_queue ();
  int work = work_ops > 0 || holds;
  unlock_queue ();
  return work;
}

/* The decode thread's next step: 1, the next operation in *op; 0, drain
 * first; 2, drain with nothing more to come from before busy_until: the
 * queue empty, or an operation from after it next that is not a pass. want:
 * a final waits or a completion is due; idle: whether 2 is wanted without
 * them (a busy line to end); busy_until: when the last decode ended. */
int stream_decode_next (int want, int idle, double busy_until, void **op)
{
  int held = 0, step = 0;
  lock_queue ();
  for (;;) {
    if (head) {
      if (want && head->reach >= busy_until) break;
      if (idle && !head->pass && head->reach >= busy_until) {
        step = 2;
        break;
      }
      if (hold_take_ms > 0 && !held) {
        unlock_queue ();
        stream_decode_sleep_ms (hold_take_ms);
        lock_queue ();
        held = 1;
        continue;
      }
      struct stream_op *node = head;
      head = node->next;
      if (!head) tail = NULL;
      queued_bytes -= node->bytes;
      running = 1;
      running_work = node->work;
      *op = node->op;
      free (node);
      pthread_cond_signal (&room);
      unlock_queue ();
      return 1;
    }
    if ((want || idle) && (reach >= busy_until || stream_stdin_blocked_before (busy_until))) {
      if (want) stream_stdin_set_working (1);
      step = 2;
      break;
    }
    stream_stdin_set_working (0);
    pthread_cond_broadcast (&drained);
    sleeping = 1;
    drain_wanted = want || idle;
    drain_at = busy_until;
    pthread_cond_wait (&wake, &queue);
    sleeping = 0;
  }
  running = 1;
  running_work = 0;
  unlock_queue ();
  return step;
}

/* The step stream_decode_next gave has run; held: the decode thread now
 * holds a waiting final, a completion due, decode-again requests or a busy
 * line to end. */
void stream_decode_done (int held)
{
  lock_queue ();
  work_ops -= running_work;
  running = running_work = 0;
  holds = held;
  unlock_queue ();
}

void stream_writer_lock (void)
{
  pthread_mutex_lock (&writer);
}

/* 1 when the writer lock was free and is now held. */
int stream_writer_trylock (void)
{
  return pthread_mutex_trylock (&writer) == 0;
}

void stream_writer_unlock (void)
{
  pthread_mutex_unlock (&writer);
}

/* The decode pass now running reports progress under generation (0: none
 * runs), with its mode, period label (HHMMSS as a number) and half-symbol. */
void stream_decode_progress_pass (int generation, int mode, int label, int nzhsym)
{
  atomic_store_explicit (&progress_mode, mode, memory_order_relaxed);
  atomic_store_explicit (&progress_label, label, memory_order_relaxed);
  atomic_store_explicit (&progress_nzhsym, nzhsym, memory_order_relaxed);
  atomic_store_explicit (&progress_pass, generation, memory_order_release);
}

/* 1, with the pass's fields, when a report under generation is the
 * pass_progress line to write: the running pass's, and the first in a
 * second, however many threads report. */
int stream_decode_progress_due (int generation, int *mode, int *label, int *nzhsym)
{
  if (generation <= 0 || atomic_load_explicit (&progress_pass, memory_order_acquire) != generation) return 0;
  long long now = llround (stream_stdin_now () * 1e9);
  long long last = atomic_load_explicit (&progress_last, memory_order_relaxed);
  if (now - last < 1000000000LL) return 0;
  if (!atomic_compare_exchange_strong (&progress_last, &last, now)) return 0;
  *mode = atomic_load_explicit (&progress_mode, memory_order_relaxed);
  *label = atomic_load_explicit (&progress_label, memory_order_relaxed);
  *nzhsym = atomic_load_explicit (&progress_nzhsym, memory_order_relaxed);
  return 1;
}

/* WSJT_CODEC_TEST_PLAN_GATE: the decode thread's hold before its first pass
 * decodes, until the frame loop has said planning. */
void stream_decode_plan_wait (void)
{
  lock_queue ();
  while (!gate_open) pthread_cond_wait (&planned, &queue);
  unlock_queue ();
}

void stream_decode_plan_open (void)
{
  lock_queue ();
  gate_open = 1;
  pthread_cond_broadcast (&planned);
  unlock_queue ();
}

/* lib/streaming_decode.f90's spare snapshot buffers. */
void stream_decode_pool_lock (void)
{
  pthread_mutex_lock (&pool);
}

void stream_decode_pool_unlock (void)
{
  pthread_mutex_unlock (&pool);
}
