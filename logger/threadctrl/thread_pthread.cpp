#include "stdafx.h"
#include <stdio.h>
#include <stdlib.h>
#include "threadctrl.h"


#ifdef WIN32
#include "pthread.h"
#include <winsock2.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
#include <sys/locking.h>
#else
#include <pthread.h>
#endif



#pragma comment(lib,"pthreadVC2.lib")

static pthread_mutexattr_t attr_recursive;

static void *
threadctl_posix_lock_alloc(unsigned locktype)
{
	pthread_mutexattr_t *attr = NULL;
	pthread_mutex_t *lock = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	if (!lock)
		return NULL;
	if (locktype & THREADCTL_LOCKTYPE_RECURSIVE)
		attr = &attr_recursive;
	if (pthread_mutex_init(lock, attr)) {
		free(lock);
		return NULL;
	}
	return lock;
}

static void
threadctl_posix_lock_free(void *lock_)
{
	pthread_mutex_t *lock = (pthread_mutex_t*)lock_;
	if(lock==NULL)
		return ;

	pthread_mutex_destroy(lock);
	free(lock);
}

static int
threadctl_posix_lock(unsigned mode, void *lock_)
{
	pthread_mutex_t *lock = (pthread_mutex_t*)lock_;
	if(lock==NULL)
		return -1 ;
	if (mode & THREADCTL_TRY)
		return pthread_mutex_trylock(lock);
	else
		return pthread_mutex_lock(lock);
}

static int
threadctl_posix_unlock(unsigned mode, void *lock_)
{
	pthread_mutex_t *lock = (pthread_mutex_t*)lock_;
	if(lock==NULL)
		return -1 ;
	return pthread_mutex_unlock(lock);
}

static unsigned long
threadctl_posix_get_id(void)
{
	union {
		pthread_t thr;
#if EVENT__SIZEOF_PTHREAD_T > EVENT__SIZEOF_LONG
		ev_uint64_t id;
#else
		unsigned long id;
#endif
	} r;
#if EVENT__SIZEOF_PTHREAD_T < EVENT__SIZEOF_LONG
	memset(&r, 0, sizeof(r));
#endif
	r.thr = pthread_self();
	return (unsigned long)r.id;
}

static void *
threadctl_posix_cond_alloc(unsigned condflags)
{
	pthread_cond_t *cond = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
	if (!cond)
		return NULL;
	if (pthread_cond_init(cond, NULL)) {
		free(cond);
		return NULL;
	}
	return cond;
}

static void
threadctl_posix_cond_free(void *cond_)
{
	pthread_cond_t *cond = (pthread_cond_t *)cond_;
	if(cond==NULL)
		return ; 
	pthread_cond_destroy(cond);
	free(cond);
}

static int
threadctl_posix_cond_signal(void *cond_, int broadcast)
{
	pthread_cond_t *cond = (pthread_cond_t *)cond_;
	if(cond==NULL)
		return -1 ;
	int r;
	if (broadcast)
		r = pthread_cond_broadcast(cond);
	else
		r = pthread_cond_signal(cond);
	return r ? -1 : 0;
}

#if defined(__GNUC__) && __GNUC__ >= 3         /* gcc 3.0 or later */
#define THREADCTL_UNLIKELY(p) __builtin_expect(!!(p),0)
#else
#define THREADCTL_UNLIKELY(p) (p)
#endif



#ifdef  WIN32

int threadctl_gettimeofday(struct timeval *tv, struct timezone *tz)
{
#ifdef _MSC_VER
#define U64_LITERAL(n) n##ui64
#else
#define U64_LITERAL(n) n##llu
#endif

	/* Conversion logic taken from Tor, which in turn took it
	* from Perl.  GetSystemTimeAsFileTime returns its value as
	* an unaligned (!) 64-bit value containing the number of
	* 100-nanosecond intervals since 1 January 1601 UTC. */
#define EPOCH_BIAS U64_LITERAL(116444736000000000)
#define UNITS_PER_SEC U64_LITERAL(10000000)
#define USEC_PER_SEC U64_LITERAL(1000000)
#define UNITS_PER_USEC U64_LITERAL(10)
	union {
		FILETIME ft_ft;
		unsigned __int64 ft_64;
	} ft;

	if (tv == NULL)
		return -1;

	GetSystemTimeAsFileTime(&ft.ft_ft);

	if (THREADCTL_UNLIKELY(ft.ft_64 < EPOCH_BIAS)) {
		/* Time before the unix epoch. */
		return -1;
	}
	ft.ft_64 -= EPOCH_BIAS;
	tv->tv_sec = (long) (ft.ft_64 / UNITS_PER_SEC);
	tv->tv_usec = (long) ((ft.ft_64 / UNITS_PER_USEC) % USEC_PER_SEC);
	return 0;
}
#else
#define threadctl_gettimeofday(tv, tz) gettimeofday((tv), (tz))
#endif


#define threadctl_timeradd(tvp, uvp, vvp)					\
	do {								\
	(vvp)->tv_sec = (tvp)->tv_sec + (uvp)->tv_sec;		\
	(vvp)->tv_usec = (tvp)->tv_usec + (uvp)->tv_usec;       \
	if ((vvp)->tv_usec >= 1000000) {			\
	(vvp)->tv_sec++;				\
	(vvp)->tv_usec -= 1000000;			\
	}							\
	} while (0)


static int
threadctl_posix_cond_wait(void *cond_, void *lock_, const struct timeval *tv)
{
	int r;
	pthread_cond_t *cond = (pthread_cond_t *)cond_;
	pthread_mutex_t *lock = (pthread_mutex_t*)lock_;
	if((!cond) || (!lock))
		return -1 ; 

	if (tv) {
		struct timeval now, abstime;
		struct timespec ts;
		threadctl_gettimeofday(&now, NULL);
		threadctl_timeradd(&now, tv, &abstime);
		ts.tv_sec = abstime.tv_sec;
		ts.tv_nsec = abstime.tv_usec*1000;
		r = pthread_cond_timedwait(cond, lock, &ts);
		if (r == ETIMEDOUT)
			return 1;
		else if (r)
			return -1;
		else
			return 0;
	} else {
		r = pthread_cond_wait(cond, lock);
		return r ? -1 : 0;
	}
}

int
threadctl_use_pthreads(void)
{
	struct threadctl_lock_callbacks cbs = {
		THREADCTL_LOCK_API_VERSION,
		THREADCTL_LOCKTYPE_RECURSIVE,
		threadctl_posix_lock_alloc,
		threadctl_posix_lock_free,
		threadctl_posix_lock,
		threadctl_posix_unlock
	};
	struct threadctl_condition_callbacks cond_cbs = {
		THREADCTL_COND_API_VERSION,
		threadctl_posix_cond_alloc,
		threadctl_posix_cond_free,
		threadctl_posix_cond_signal,
		threadctl_posix_cond_wait
	};
	/* Set ourselves up to get recursive locks. */
	if (pthread_mutexattr_init(&attr_recursive))
		return -1;
	if (pthread_mutexattr_settype(&attr_recursive, PTHREAD_MUTEX_RECURSIVE))
		return -1;

	threadctl_set_lock_callbacks(&cbs);
	threadctl_set_condition_callbacks(&cond_cbs);
	threadctl_set_id_callback(threadctl_posix_get_id);
	return 0;
}


