
#include "compat.h"

#ifdef MISSING_UV_THREADS
#include <errno.h>
#include <stdlib.h>

int uv_mutex_init(uv_mutex_t* mutex) {
#ifdef NDEBUG
  if (pthread_mutex_init(mutex, NULL))
    return -1;
  else
    return 0;
#else
  pthread_mutexattr_t attr;
  int r;

  if (pthread_mutexattr_init(&attr))
    abort();

  if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK))
    abort();

  r = pthread_mutex_init(mutex, &attr);

  if (pthread_mutexattr_destroy(&attr))
    abort();

  return r ? -1 : 0;
#endif
}


void uv_mutex_destroy(uv_mutex_t* mutex) {
  if (pthread_mutex_destroy(mutex))
    abort();
}


void uv_mutex_lock(uv_mutex_t* mutex) {
  if (pthread_mutex_lock(mutex))
    abort();
}


int uv_mutex_trylock(uv_mutex_t* mutex) {
  int r;

  r = pthread_mutex_trylock(mutex);

  if (r && r != EAGAIN)
    abort();

  if (r)
    return -1;
  else
    return 0;
}


void uv_mutex_unlock(uv_mutex_t* mutex) {
  if (pthread_mutex_unlock(mutex))
    abort();
}

#endif

