/* ZedSynth - the engine's threading primitives, on pthreads or on Win32
 *
 * Copyright (C) 2026 Keith Adler. GPL-2.0-or-later.
 *
 * The engine needs a non-recursive mutex with trylock, a condition variable, and one worker
 * thread it can create and join. POSIX and MinGW have pthreads; MSVC does not, and the
 * standalone application is built with MSVC because clap-wrapper's Windows shell is
 * C++/WinRT. An SRWLOCK in exclusive mode, a CONDITION_VARIABLE and _beginthreadex are the
 * same things there. trylock returns 0 when the lock was taken and EBUSY when it was not, as
 * pthread_mutex_trylock does, which is what the callers test.
 */
#ifndef Y_THREAD_H
#define Y_THREAD_H

#include <errno.h>

#if defined(_MSC_VER)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <process.h>
#include <stdlib.h>

typedef SRWLOCK            y_mutex_t;
typedef CONDITION_VARIABLE y_cond_t;
typedef HANDLE             y_thread_t;
#define Y_MUTEX_INITIALIZER SRWLOCK_INIT

static inline int y_mutex_init(y_mutex_t *m)    { InitializeSRWLock(m); return 0; }
static inline int y_mutex_destroy(y_mutex_t *m) { (void)m; return 0; }
static inline int y_mutex_lock(y_mutex_t *m)    { AcquireSRWLockExclusive(m); return 0; }
static inline int y_mutex_unlock(y_mutex_t *m)  { ReleaseSRWLockExclusive(m); return 0; }
static inline int y_mutex_trylock(y_mutex_t *m) { return TryAcquireSRWLockExclusive(m) ? 0 : EBUSY; }

static inline int y_cond_init(y_cond_t *c)    { InitializeConditionVariable(c); return 0; }
static inline int y_cond_destroy(y_cond_t *c) { (void)c; return 0; }
static inline int y_cond_signal(y_cond_t *c)  { WakeConditionVariable(c); return 0; }
static inline int y_cond_wait(y_cond_t *c, y_mutex_t *m)
{
    return SleepConditionVariableSRW(c, m, INFINITE, 0) ? 0 : EINVAL;
}

/* _beginthreadex wants unsigned __stdcall (void *); the engine's worker is void *(void *). */
struct y_thread_start { void *(*fn)(void *); void *arg; };
static unsigned __stdcall y_thread_trampoline(void *p)
{
    struct y_thread_start s = *(struct y_thread_start *)p;
    free(p);
    s.fn(s.arg);
    return 0;
}
static inline int y_thread_create(y_thread_t *t, void *(*fn)(void *), void *arg)
{
    struct y_thread_start *s = (struct y_thread_start *)malloc(sizeof *s);
    if (!s) return ENOMEM;
    s->fn = fn; s->arg = arg;
    uintptr_t h = _beginthreadex(NULL, 0, y_thread_trampoline, s, 0, NULL);
    if (!h) { free(s); return EAGAIN; }
    *t = (HANDLE)h;
    return 0;
}
static inline int y_thread_join(y_thread_t t)
{
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
    return 0;
}

#else

#include <pthread.h>

typedef pthread_mutex_t y_mutex_t;
typedef pthread_cond_t  y_cond_t;
typedef pthread_t       y_thread_t;
#define Y_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER

static inline int y_mutex_init(y_mutex_t *m)    { return pthread_mutex_init(m, NULL); }
static inline int y_mutex_destroy(y_mutex_t *m) { return pthread_mutex_destroy(m); }
static inline int y_mutex_lock(y_mutex_t *m)    { return pthread_mutex_lock(m); }
static inline int y_mutex_unlock(y_mutex_t *m)  { return pthread_mutex_unlock(m); }
static inline int y_mutex_trylock(y_mutex_t *m) { return pthread_mutex_trylock(m); }

static inline int y_cond_init(y_cond_t *c)    { return pthread_cond_init(c, NULL); }
static inline int y_cond_destroy(y_cond_t *c) { return pthread_cond_destroy(c); }
static inline int y_cond_signal(y_cond_t *c)  { return pthread_cond_signal(c); }
static inline int y_cond_wait(y_cond_t *c, y_mutex_t *m) { return pthread_cond_wait(c, m); }

static inline int y_thread_create(y_thread_t *t, void *(*fn)(void *), void *arg) { return pthread_create(t, NULL, fn, arg); }
static inline int y_thread_join(y_thread_t t) { return pthread_join(t, NULL); }

#endif

#endif /* Y_THREAD_H */
