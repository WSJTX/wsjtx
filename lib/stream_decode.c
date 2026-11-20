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
 * The output queue: once spectrum, ref_spectrum or prepared_audio is on,
 * every stdout line (lib/streaming_emit.f90) is queued, under the writer
 * lock, for a writer thread, the only thread that then writes stdout, so no
 * other thread waits on a reader. Past STREAM_ROWS_CAP droppable row
 * entries the oldest is discarded and its lines counted for the writer's
 * next rows_dropped line;
 * past STREAM_ROWS_OTHER_CAP bytes of other lines the queuing thread waits.
 * The other entries keep their order among themselves and go ahead of every
 * droppable one. While a held group is open (the writer lock held more than
 * once when one of its entries was queued: a reply, a held pair), the writer
 * writes no droppable entry, so none lands inside the group.
 *
 * Lock order: the queue lock, then the stdin reader's. Nothing holds the
 * queue lock across a decode or a stdout write. The writer lock is recursive:
 * a reply's lines take it once for the whole reply and again for each line.
 * The output queue's lock is taken under the writer lock or with no lock
 * held, and nothing is taken under it. Besides it, the writer thread takes
 * only libgfortran's lock on unit 6, which no other thread takes while the
 * writer runs but on error paths that write unit 6 directly: the decoders
 * flush through streaming_emit_flush.
 * Called via bind(C) from Fortran.
 */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <time.h>
#endif
#ifdef __linux__
#include <link.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

/* Queued bytes, operations and their audio, past which the frame loop waits;
 * one operation is always admitted. */
#define STREAM_DECODE_CAP ((int64_t) 64 << 20)
/* The decode thread's stack, besides the static TLS glibc carves out of it. */
#define STREAM_DECODE_STACK ((size_t) 16 << 20)
/* Droppable row entries queued at most (about 5.8 s of a period mode's), and
 * the row writer's stack besides the static TLS. */
#define STREAM_ROWS_CAP 20
#define STREAM_ROWS_OTHER_CAP ((int64_t) 16 << 20)
#define STREAM_ROWS_STACK ((size_t) 256 << 10)

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
/* WSJT_CODEC_TEST_DECODE_NICE: added to the decode thread's nice value. */
static int decode_nice;
static void (*body) (void);
static pthread_t thread;
/* WSJT_CODEC_TEST_PLAN_GATE: the frame loop has said planning. */
static pthread_cond_t planned = PTHREAD_COND_INITIALIZER;
static int gate_open;

static pthread_mutex_t writer;
static int writer_ready;
/* How many times the writer lock's holder holds it (only the holder reads
 * or changes it). */
static int writer_depth;
/* A held group is open: changed only by the writer lock's holder, under the
 * queue lock. */
static int rows_group;
static pthread_mutex_t pool = PTHREAD_MUTEX_INITIALIZER;

struct stream_row {
  struct stream_row *next;
  int64_t size;
  int lines, droppable;
  char text[];
};

static pthread_mutex_t rows = PTHREAD_MUTEX_INITIALIZER;
/* An entry is queued, or the session ends; an entry was written. */
static pthread_cond_t rows_ready = PTHREAD_COND_INITIALIZER;
static pthread_cond_t rows_room = PTHREAD_COND_INITIALIZER;
static struct stream_row *rows_head, *rows_tail;
/* The last entry queued that is not droppable (NULL: none). */
static struct stream_row *rows_last_other;
/* Droppable entries queued; the writer is writing one; the session is
 * ending; lines discarded since the last rows_dropped line; bytes of the
 * other entries queued. */
static int rows_droppable, rows_writing, rows_ending;
static int64_t rows_dropped, rows_other;
static pthread_t rows_thread;
static void (*rows_write) (char const *text, int64_t size, int64_t dropped);

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
#ifdef __linux__
  if (decode_nice)
    {
      /* Linux keeps a nice value per thread; the OpenMP threads this one
       * starts inherit it. */
      id_t const tid = (id_t) syscall (SYS_gettid);
      errno = 0;
      int const nice = getpriority (PRIO_PROCESS, tid);
      if (errno == 0) setpriority (PRIO_PROCESS, tid, nice + decode_nice);
    }
#endif
  body ();
  return NULL;
}

/* Before stream_decode_start, a test's nice for the decode thread; 1 where
 * it can apply it (Linux). */
int stream_decode_set_nice (int nice)
{
  decode_nice = nice;
#ifdef __linux__
  return 1;
#else
  return 0;
#endif
}

/* The writer lock, made once by the frame loop's thread. 0 when it could
 * not be made. */
static int writer_init (void)
{
  pthread_mutexattr_t recursive;
  if (writer_ready) return 1;
  if (pthread_mutexattr_init (&recursive) != 0) return 0;
  writer_ready = pthread_mutexattr_settype (&recursive, PTHREAD_MUTEX_RECURSIVE) == 0
    && pthread_mutex_init (&writer, &recursive) == 0;
  pthread_mutexattr_destroy (&recursive);
  return writer_ready;
}

/* Start the decode thread on decode; take_ms holds it before it takes each
 * operation (a test hook). 0 when no thread could be started. */
int stream_decode_start (void (*decode) (void), int take_ms)
{
  pthread_attr_t attributes;
  size_t stack = STREAM_DECODE_STACK;
#ifdef __linux__
  dl_iterate_phdr (add_tls, &stack);
#endif
  int ok = writer_init ();
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
  ++writer_depth;
}

/* 1 when the writer lock was free and is now held. */
int stream_writer_trylock (void)
{
  if (pthread_mutex_trylock (&writer) != 0) return 0;
  ++writer_depth;
  return 1;
}

void stream_writer_unlock (void)
{
  if (--writer_depth == 0 && rows_group)
    {
      pthread_mutex_lock (&rows);
      rows_group = 0;
      pthread_cond_signal (&rows_ready);
      pthread_mutex_unlock (&rows);
    }
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

static void *rows_main (void *unused)
{
  (void) unused;
  for (;;)
    {
      pthread_mutex_lock (&rows);
      while (!rows_ending && !(rows_head && (!rows_head->droppable || !rows_group)))
        pthread_cond_wait (&rows_ready, &rows);
      struct stream_row *row = rows_head;
      int64_t dropped = 0;
      if (row)
        {
          rows_head = row->next;
          if (!rows_head) rows_tail = NULL;
          if (rows_last_other == row) rows_last_other = NULL;
          rows_droppable -= row->droppable;
          /* The count goes before a droppable row, never inside a held group
           * of lines: the droppable row whose arrival discarded others is
           * still queued. */
          if (row->droppable)
            {
              dropped = rows_dropped;
              rows_dropped = 0;
            }
          rows_writing = 1;
        }
      pthread_mutex_unlock (&rows);
      if (!row) return NULL;
      rows_write (row->text, row->size, dropped);
      pthread_mutex_lock (&rows);
      rows_writing = 0;
      if (!row->droppable) rows_other -= row->size;
      pthread_cond_broadcast (&rows_room);
      pthread_mutex_unlock (&rows);
      free (row);
    }
}

/* Start the writer thread on write, which writes an entry, after a
 * rows_dropped line when dropped is above 0. 0 when the writer lock or the
 * thread could not be made. */
int stream_rows_start (void (*write) (char const *text, int64_t size, int64_t dropped))
{
  pthread_attr_t attributes;
  size_t stack = STREAM_ROWS_STACK;
#ifdef __linux__
  dl_iterate_phdr (add_tls, &stack);
#endif
  if (!writer_init () || pthread_attr_init (&attributes) != 0) return 0;
  rows_write = write;
  int ok = pthread_attr_setstacksize (&attributes, stack) == 0
    && pthread_create (&rows_thread, &attributes, rows_main, NULL) == 0;
  pthread_attr_destroy (&attributes);
  return ok;
}

/* An entry of size bytes and lines lines, its lines ended by newlines but
 * the last, queued under the writer lock: a droppable one last, another
 * after the other entries queued. A droppable entry arriving at
 * STREAM_ROWS_CAP queued discards the oldest droppable one, as one there is
 * no memory for is discarded; another waits while STREAM_ROWS_OTHER_CAP
 * bytes are queued. 0: no memory for another entry. */
int stream_rows_put (char const *text, int64_t size, int lines, int droppable)
{
  int const grouped = writer_depth > 1;
  struct stream_row *row = malloc (sizeof *row + (size_t) size);
  if (row)
    {
      memcpy (row->text, text, (size_t) size);
      row->size = size;
      row->lines = lines;
      row->droppable = droppable;
      row->next = NULL;
    }
  pthread_mutex_lock (&rows);
  if (!row)
    {
      if (droppable) rows_dropped += lines;
      pthread_mutex_unlock (&rows);
      return droppable;
    }
  while (!droppable && rows_other > 0 && rows_other + size > STREAM_ROWS_OTHER_CAP)
    pthread_cond_wait (&rows_room, &rows);
  if (droppable && rows_droppable == STREAM_ROWS_CAP)
    {
      /* Of several droppable entries the oldest is never the last. */
      struct stream_row **at = &rows_head, *oldest;
      while (!(*at)->droppable) at = &(*at)->next;
      oldest = *at;
      *at = oldest->next;
      rows_dropped += oldest->lines;
      rows_droppable -= 1;
      free (oldest);
    }
  if (droppable || !rows_tail || rows_last_other == rows_tail)
    {
      if (rows_tail) rows_tail->next = row; else rows_head = row;
      rows_tail = row;
    }
  else if (rows_last_other)
    {
      row->next = rows_last_other->next;
      rows_last_other->next = row;
    }
  else
    {
      row->next = rows_head;
      rows_head = row;
    }
  if (!droppable) rows_last_other = row;
  if (grouped) rows_group = 1;
  rows_droppable += droppable;
  if (!droppable) rows_other += size;
  pthread_cond_signal (&rows_ready);
  pthread_mutex_unlock (&rows);
  return 1;
}

/* Returns once every entry queued has been written. Called inside an open
 * held group, it discards and counts the droppable entries instead, which
 * could not be written until the group closes. */
void stream_rows_quiet (void)
{
  int inside = 0;
  /* The writer lock is recursive: it is free to the caller, and held at
   * depth above 0, only when the caller already holds it. */
  if (pthread_mutex_trylock (&writer) == 0)
    {
      inside = writer_depth > 0;
      pthread_mutex_unlock (&writer);
    }
  pthread_mutex_lock (&rows);
  if (inside && rows_group)
    {
      struct stream_row **at = &rows_head;
      rows_tail = NULL;
      while (*at)
        {
          struct stream_row *row = *at;
          if (row->droppable)
            {
              *at = row->next;
              rows_dropped += row->lines;
              rows_droppable -= 1;
              free (row);
            }
          else
            {
              rows_tail = row;
              at = &row->next;
            }
        }
      /* Another thread's quiet may wait for what was discarded. */
      pthread_cond_broadcast (&rows_room);
    }
  while (rows_head || rows_writing) pthread_cond_wait (&rows_room, &rows);
  pthread_mutex_unlock (&rows);
}

/* The session ends: the writer writes what is queued and stops. */
void stream_rows_end (void)
{
  pthread_mutex_lock (&rows);
  rows_ending = 1;
  pthread_cond_signal (&rows_ready);
  pthread_mutex_unlock (&rows);
  pthread_join (rows_thread, NULL);
}
