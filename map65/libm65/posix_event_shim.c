// posix_event_shim.c
#include <pthread.h>
#include <stdlib.h>

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    int             signaled;   // 0 = non-signaled, 1 = signaled
} posix_event_t;

void* win_create_event(void)
{
    posix_event_t* ev = malloc(sizeof(posix_event_t));
    if (!ev) return NULL;

    pthread_mutex_init(&ev->mutex, NULL);
    pthread_cond_init(&ev->cond, NULL);
    ev->signaled = 0;

    return ev;
}

int win_set_event(void* h)
{
    posix_event_t* ev = (posix_event_t*)h;
    pthread_mutex_lock(&ev->mutex);

    ev->signaled = 1;
    pthread_cond_signal(&ev->cond);   // auto-reset: wake ONE waiter

    pthread_mutex_unlock(&ev->mutex);
    return 1;
}

int win_reset_event(void* h)
{
    posix_event_t* ev = (posix_event_t*)h;
    pthread_mutex_lock(&ev->mutex);
    ev->signaled = 0;
    pthread_mutex_unlock(&ev->mutex);
    return 1;
}

int win_wait_for_single_object(void* h)
{
    posix_event_t* ev = (posix_event_t*)h;
    pthread_mutex_lock(&ev->mutex);

    while (!ev->signaled)
        pthread_cond_wait(&ev->cond, &ev->mutex);

    // Auto-reset: clear signaled state immediately
    ev->signaled = 0;

    pthread_mutex_unlock(&ev->mutex);
    return 0;
}

void win_close_event(void* h)
{
    posix_event_t* ev = (posix_event_t*)h;
    pthread_mutex_destroy(&ev->mutex);
    pthread_cond_destroy(&ev->cond);
    free(ev);
}
