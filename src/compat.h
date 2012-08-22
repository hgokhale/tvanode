#pragma once

#ifdef MISSING_UV_THREADS

#include <pthread.h>
typedef pthread_t uv_thread_t;
typedef pthread_mutex_t uv_mutex_t;


int uv_mutex_init(uv_mutex_t* mutex);
void uv_mutex_destroy(uv_mutex_t* mutex);
void uv_mutex_lock(uv_mutex_t* mutex);
int uv_mutex_trylock(uv_mutex_t* mutex);
void uv_mutex_unlock(uv_mutex_t* mutex);

#endif
