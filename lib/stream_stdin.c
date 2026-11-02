/* stream_stdin.c — portable read-fully helper for the framed stdin
 * protocol (lib/streaming_io.f90).
 *
 * Fortran unit I/O cannot read a redirected stdin portably: native
 * Windows has no /dev/stdin, and re-OPENing the preconnected unit 5 with
 * ACCESS='STREAM' is non-conforming (F2018 12.5.6.1 — ACCESS is not a
 * changeable mode of a connected unit), so libgfortran rejects it on
 * every platform. This helper reads file descriptor 0 directly instead,
 * on every platform; only the read call and the clock differ on Windows.
 *
 * A reader thread drains fd 0 into a FIFO while the caller decodes, so a
 * producer's writes do not wait for a decode; past STREAM_STDIN_CAP bytes
 * it stops reading, as a full pipe would. stream_stdin_read_fully blocks
 * until the full request is delivered or the stream truly ends (read() ==
 * 0, or a hard error), and returns short only after every buffered byte:
 * a pipe short-read can never be mistaken for EOF. It returns -1 once the
 * FIFO could not hold what was read (out of memory). Without a thread it
 * reads fd 0 itself, as a single-threaded caller would.
 *
 * With the stream's decode thread (lib/stream_decode.c), a caller about to
 * block marks itself blocked under that thread's queue lock and wakes it
 * (lock order: the queue lock, then this one), and the time it waits counts
 * toward stream_stdin_waited only while the decode thread is idle.
 *
 * The first stream_stdin_read_fully starts the thread. Windows: binary
 * mode must be forced on fd 0 before that (stream_set_stdin_binary in
 * lib/stream_setmode.c, called from jt9_stream's setup), or the CRT stops
 * at 0x1A and rewrites CRLF, and _setmode would wait behind the reader's
 * _read. Nothing here is freed or destroyed: at exit the reader may still
 * be parked in read(). Called via bind(C) from Fortran.
 */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <errno.h>
#include <time.h>
#include <unistd.h>
#endif

/* About 11.6 minutes of 12 kHz int16 audio, counting chunk headers. */
#define STREAM_STDIN_CAP ((int64_t) 16 << 20)
#define STREAM_STDIN_BLOCK 65536

struct stream_chunk {
  struct stream_chunk *next;
  double arrival;
  int64_t size, used;
  char data[];
};

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t readable = PTHREAD_COND_INITIALIZER;
static pthread_cond_t writable = PTHREAD_COND_INITIALIZER;
static struct stream_chunk *head, *tail;
/* Bytes held: unread data and the headers of the chunks holding it. */
static int64_t buffered;
static int ended, failed, threaded;
/* Seconds stream_stdin_read_fully spent waiting for input since stream_stdin_waited last asked. */
static double waited;
static double consumed_arrival = -1.0;
/* The decode thread's queue lock and its wakeup, while it runs. */
static void (*decode_lock) (void), (*decode_unlock) (void), (*decode_wake) (void);
/* The caller waits for input in stream_stdin_read_fully (since wait_start);
 * the decode thread is working, or idle since idle_since; idle_waited: the
 * caller's idle wait before the decode thread last started working. */
static int blocked, working;
static double wait_start, idle_since = -HUGE_VAL, idle_waited;

/* The caller's wait, from wait_start to t, while the decode thread has been idle. */
static double idle_wait (double t)
{
  if (working) return 0;
  return t - (wait_start > idle_since ? wait_start : idle_since);
}

double stream_stdin_now(void)
{
#ifdef _WIN32
  LARGE_INTEGER count, frequency;
  QueryPerformanceCounter(&count);
  QueryPerformanceFrequency(&frequency);
  return (double) count.QuadPart / (double) frequency.QuadPart;
#else
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (double) now.tv_sec + 1e-9 * (double) now.tv_nsec;
#endif
}

/* One read() of fd 0: the bytes read, 0 at end of input, negative on an error. */
static int64_t read_stdin(char *buf, int64_t n)
{
  if (n > STREAM_STDIN_BLOCK) n = STREAM_STDIN_BLOCK;
#ifdef _WIN32
  return _read(0, buf, (unsigned int) n);
#else
  ssize_t r;
  do r = read(0, buf, (size_t) n); while (r < 0 && errno == EINTR);
  return r;
#endif
}

static void *stream_stdin_reader(void *unused)
{
  static char block[STREAM_STDIN_BLOCK];
  (void) unused;
  for (;;) {
    pthread_mutex_lock(&lock);
    while (buffered >= STREAM_STDIN_CAP) pthread_cond_wait(&writable, &lock);
    pthread_mutex_unlock(&lock);
    int64_t r = read_stdin(block, sizeof block);
    struct stream_chunk *chunk = NULL;
    if (r > 0) chunk = malloc(sizeof *chunk + (size_t) r);
    if (chunk) {
      chunk->next = NULL;
      chunk->size = r;
      chunk->used = 0;
      memcpy(chunk->data, block, (size_t) r);
    }
    pthread_mutex_lock(&lock);
    if (chunk) {
      /* Stamped under the lock, so buffered_before(t) sees every chunk stamped before t. */
      chunk->arrival = stream_stdin_now();
      if (tail) tail->next = chunk; else head = chunk;
      tail = chunk;
      buffered += (int64_t) sizeof *chunk + chunk->size;
    } else {
      ended = 1;
      failed = r > 0;
    }
    pthread_cond_signal(&readable);
    pthread_mutex_unlock(&lock);
    if (!chunk) return NULL;
  }
}

int64_t stream_stdin_read_fully(void *buf, int64_t n)
{
  static int started;
  char *p = (char *) buf;
  int64_t total = 0;
  if (!started) {
    pthread_t reader;
    started = 1;
    threaded = pthread_create(&reader, NULL, stream_stdin_reader, NULL) == 0;
    if (threaded) pthread_detach(reader);
  }
  if (!threaded) {
    while (total < n) {
      double start = stream_stdin_now();
      int64_t r = read_stdin(p + total, n - total);
      double end = stream_stdin_now();
      if (r <= 0) break;
      total += r;
      pthread_mutex_lock(&lock);
      consumed_arrival = end;
      waited += end - start;
      pthread_mutex_unlock(&lock);
    }
    return total;
  }
  pthread_mutex_lock(&lock);
  while (total < n) {
    if (!head && !ended && decode_lock) {
      pthread_mutex_unlock(&lock);
      decode_lock();
      pthread_mutex_lock(&lock);
      if (!head && !ended) {
        blocked = 1;
        decode_wake();
      }
      decode_unlock();
    }
    if (!head && !ended) {
      blocked = 1;
      wait_start = stream_stdin_now();
      idle_waited = 0;
      while (!head && !ended) pthread_cond_wait(&readable, &lock);
      blocked = 0;
      waited += idle_waited + idle_wait(stream_stdin_now());
    }
    if (!head) {
      if (failed) total = -1;
      break;
    }
    int64_t take = head->size - head->used;
    if (take > n - total) take = n - total;
    memcpy(p + total, head->data + head->used, (size_t) take);
    head->used += take;
    total += take;
    buffered -= take;
    consumed_arrival = head->arrival;
    if (head->used == head->size) {
      struct stream_chunk *done = head;
      head = head->next;
      if (!head) tail = NULL;
      buffered -= (int64_t) sizeof *done;
      free(done);
    }
    pthread_cond_signal(&writable);
  }
  pthread_mutex_unlock(&lock);
  return total;
}

/* Arrival time (stream_stdin_now) of the last byte consumed; negative before any. */
double stream_stdin_arrival(void)
{
  pthread_mutex_lock(&lock);
  double arrival = consumed_arrival;
  pthread_mutex_unlock(&lock);
  return arrival;
}

/* Seconds stream_stdin_read_fully spent waiting for input since the last call. */
double stream_stdin_waited(void)
{
  pthread_mutex_lock(&lock);
  double w = waited;
  waited = 0;
  pthread_mutex_unlock(&lock);
  return w;
}

/* Bytes received and not yet consumed that arrived before t. */
int64_t stream_stdin_buffered_before(double t)
{
  int64_t n = 0;
  pthread_mutex_lock(&lock);
  for (struct stream_chunk *chunk = head; chunk && chunk->arrival < t; chunk = chunk->next)
    n += chunk->size - chunk->used;
  pthread_mutex_unlock(&lock);
  return n;
}

/* Whether a reader thread drains fd 0. */
int stream_stdin_threaded(void)
{
  return threaded;
}

void stream_stdin_set_decode(void (*lock_queue)(void), void (*unlock_queue)(void), void (*wake)(void))
{
  decode_lock = lock_queue;
  decode_unlock = unlock_queue;
  decode_wake = wake;
}

/* The decode thread starts working (an operation queued, or a drain) or goes idle. */
void stream_stdin_set_working(int now_working)
{
  pthread_mutex_lock(&lock);
  double t = stream_stdin_now();
  if (now_working && blocked) idle_waited += idle_wait(t);
  if (!now_working && working) idle_since = t;
  working = now_working;
  pthread_mutex_unlock(&lock);
}

/* Whether the caller is blocked for input with nothing buffered that arrived before t. */
int stream_stdin_blocked_before(double t)
{
  pthread_mutex_lock(&lock);
  int none = blocked && !(head && head->arrival < t);
  pthread_mutex_unlock(&lock);
  return none;
}
