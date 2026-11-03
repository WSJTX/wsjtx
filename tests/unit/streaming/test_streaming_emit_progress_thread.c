// SPDX-License-Identifier: GPL-3.0-or-later
#include <pthread.h>
#include <time.h>

int stream_decode_start (void (*decode) (void), int take_ms);
void stream_decode_join (void);
void emit_contended_progress (void);

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t finished = PTHREAD_COND_INITIALIZER;
static pthread_t worker;
static int done;

static void decode (void) {}

int test_writer_start (void)
{
  return stream_decode_start (decode, 0);
}

void test_writer_finish (void)
{
  stream_decode_join ();
}

static void *attempt_progress (void *unused)
{
  (void) unused;
  emit_contended_progress ();
  pthread_mutex_lock (&mutex);
  done = 1;
  pthread_cond_signal (&finished);
  pthread_mutex_unlock (&mutex);
  return NULL;
}

int test_progress_start (void)
{
  return pthread_create (&worker, NULL, attempt_progress, NULL) == 0;
}

int test_progress_wait (void)
{
  struct timespec deadline;
  if (timespec_get (&deadline, TIME_UTC) != TIME_UTC) return 0;
  deadline.tv_sec += 10;
  pthread_mutex_lock (&mutex);
  while (!done)
    if (pthread_cond_timedwait (&finished, &mutex, &deadline) != 0) break;
  int completed = done;
  pthread_mutex_unlock (&mutex);
  return completed;
}

int test_progress_join (void)
{
  return pthread_join (worker, NULL) == 0;
}
