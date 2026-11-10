// SPDX-License-Identifier: GPL-3.0-or-later
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

int stream_decode_put (void *op, int64_t bytes, int work, int pass);
void stream_decode_publish (double position);
int stream_decode_next (int want, int idle, double busy_until, void **op);
void stream_decode_done (int held);

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static int checked_input, completed, next_step;

void stream_stdin_set_decode (void (*lock_queue) (void), void (*unlock_queue) (void), void (*wake) (void))
{
  (void) lock_queue;
  (void) unlock_queue;
  (void) wake;
}

void stream_stdin_set_working (int working)
{
  if (working) return;
  // The decoder holds its queue lock until it enters the condition wait.
  pthread_mutex_lock (&mutex);
  checked_input = 1;
  pthread_cond_signal (&changed);
  pthread_mutex_unlock (&mutex);
}

int stream_stdin_blocked_before (double t)
{
  (void) t;
  return 0;
}

double stream_stdin_now (void)
{
  return 0;
}

static int wait_for (int *condition)
{
  struct timespec deadline;
  if (clock_gettime (CLOCK_REALTIME, &deadline) != 0) return 0;
  deadline.tv_sec += 10;
  pthread_mutex_lock (&mutex);
  while (!*condition)
    if (pthread_cond_timedwait (&changed, &mutex, &deadline) != 0) break;
  int result = *condition;
  pthread_mutex_unlock (&mutex);
  return result;
}

static void *wait_for_drain (void *unused)
{
  (void) unused;
  void *op = NULL;
  int step = stream_decode_next (1, 0, 100, &op);
  stream_decode_done (0);
  pthread_mutex_lock (&mutex);
  next_step = step;
  completed = 1;
  pthread_cond_signal (&changed);
  pthread_mutex_unlock (&mutex);
  return NULL;
}

int main (void)
{
  int before = 1, at = 2;
  void *op = NULL;
  stream_decode_publish (99);
  if (!stream_decode_put (&before, 0, 1, 1)) return 1;
  stream_decode_publish (100);
  if (!stream_decode_put (&at, 0, 1, 1)) return 1;
  if (stream_decode_next (1, 0, 100, &op) != 1 || op != &before) {
    fprintf (stderr, "operation before the decoder became free was not taken first\n");
    return 1;
  }
  stream_decode_done (1);
  if (stream_decode_next (1, 0, 100, &op) != 0) {
    fprintf (stderr, "waiting final was not drained before the operation at the boundary\n");
    return 1;
  }
  stream_decode_done (0);
  if (stream_decode_next (0, 0, 100, &op) != 1 || op != &at) {
    fprintf (stderr, "operation at the boundary was not retained for after the drain\n");
    return 1;
  }
  stream_decode_done (0);

  pthread_t thread;
  if (pthread_create (&thread, NULL, wait_for_drain, NULL) != 0) return 1;
  if (!wait_for (&checked_input)) {
    fprintf (stderr, "decode thread did not wait for input\n");
    return 1;
  }
  stream_decode_publish (100);
  if (!wait_for (&completed)) {
    fprintf (stderr, "input reaching the boundary did not wake the waiting decoder\n");
    return 1;
  }
  if (pthread_join (thread, NULL) != 0 || next_step != 2) {
    fprintf (stderr, "decoder did not drain after input reached the boundary\n");
    return 1;
  }
  return 0;
}
