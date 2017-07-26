#include "stdafx.h"
#include "threadctrl.h"
#include <malloc.h>


#ifdef _WIN32
#ifndef _WIN32_WINNT
/* Minimum required for InitializeCriticalSectionAndSpinCount */
#define _WIN32_WINNT 0x0403
#endif
#include <limits>
#include <winsock2.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
#include <sys/locking.h>
#endif

#define SPIN_COUNT 2000



void* threadctl_win32_lock_create(unsigned locktype)
{
	CRITICAL_SECTION *lock = (CRITICAL_SECTION*)malloc(sizeof(CRITICAL_SECTION));
	if (!lock)
		return NULL;
	if (InitializeCriticalSectionAndSpinCount(lock, SPIN_COUNT) == 0) {
		free(lock);
		return NULL;
	}
	return lock;
}

void threadctl_win32_lock_free(void *lock_)
{
	CRITICAL_SECTION *lock = (CRITICAL_SECTION*)lock_;
	if(lock==NULL)
		return ;
	DeleteCriticalSection(lock);
	free(lock);
}

int threadctl_win32_lock(unsigned mode, void *lock_)
{
	CRITICAL_SECTION *lock = (CRITICAL_SECTION*)lock_;
	if(lock_==NULL)
		return 1;
	if ((mode & THREADCTL_TRY)) {
		return ! TryEnterCriticalSection(lock);
	} else {
		EnterCriticalSection(lock);
		return 0;
	}
}

int threadctl_win32_unlock(unsigned mode, void *lock_)
{
	CRITICAL_SECTION *lock = (CRITICAL_SECTION*)lock_;
	if(lock_==NULL)
		return 1;
	LeaveCriticalSection(lock);
	return 0;
}

unsigned long threadctl_win32_get_id(void)
{
	return (unsigned long)::GetCurrentThreadId();
}


struct threadctl_win32_cond {
	HANDLE event;

	CRITICAL_SECTION lock;
	int n_waiting;
	int n_to_wake;
	int generation;
};

void * threadctl_win32_cond_alloc(unsigned flags)
{
	struct threadctl_win32_cond *cond;
	if (!(cond = (threadctl_win32_cond*)malloc(sizeof(struct threadctl_win32_cond))))
		return NULL;
	if (InitializeCriticalSectionAndSpinCount(&cond->lock, SPIN_COUNT)==0) {
		free(cond);
		return NULL;
	}
	if ((cond->event = CreateEvent(NULL,TRUE,FALSE,NULL)) == NULL) {
		DeleteCriticalSection(&cond->lock);
		free(cond);
		return NULL;
	}
	cond->n_waiting = cond->n_to_wake = cond->generation = 0;
	return cond;
}

void threadctl_win32_cond_free(void *cond_)
{
	struct threadctl_win32_cond *cond = (threadctl_win32_cond *)cond_;
	DeleteCriticalSection(&cond->lock);
	CloseHandle(cond->event);
	free(cond);
}

int threadctl_win32_cond_signal(void *cond_, int broadcast)
{
	struct threadctl_win32_cond *cond = (threadctl_win32_cond *)cond_;
	EnterCriticalSection(&cond->lock);
	if (broadcast)
		cond->n_to_wake = cond->n_waiting;
	else
		++cond->n_to_wake;
	cond->generation++;
	SetEvent(cond->event);
	LeaveCriticalSection(&cond->lock);
	return 0;
}


#define MAX_SECONDS_IN_MSEC_LONG \
	(((LONG_MAX) - 999) / 1000)

long
threadctl_tv_to_msec_(const struct timeval *tv)
{
	if (tv->tv_usec > 1000000 || tv->tv_sec > MAX_SECONDS_IN_MSEC_LONG)
		return -1;

	return (tv->tv_sec * 1000) + ((tv->tv_usec + 999) / 1000);
}


int threadctl_win32_cond_wait(void *cond_, void *lock_, const struct timeval *tv)
{
	struct threadctl_win32_cond *cond = (threadctl_win32_cond *)cond_;
	CRITICAL_SECTION *lock = (CRITICAL_SECTION *)lock_;
	int generation_at_start;
	int waiting = 1;
	int result = -1;
	DWORD ms = INFINITE, ms_orig = INFINITE, startTime, endTime;
	if (tv)
		ms_orig = ms = threadctl_tv_to_msec_(tv);

	EnterCriticalSection(&cond->lock);
	++cond->n_waiting;
	generation_at_start = cond->generation;
	LeaveCriticalSection(&cond->lock);

	LeaveCriticalSection(lock);

	startTime = GetTickCount();
	do {
		DWORD res;
		res = WaitForSingleObject(cond->event, ms);
		EnterCriticalSection(&cond->lock);
		if (cond->n_to_wake &&
		    cond->generation != generation_at_start) {
			--cond->n_to_wake;
			--cond->n_waiting;
			result = 0;
			waiting = 0;
			goto out;
		} else if (res != WAIT_OBJECT_0) {
			result = (res==WAIT_TIMEOUT) ? 1 : -1;
			--cond->n_waiting;
			waiting = 0;
			goto out;
		} else if (ms != INFINITE) {
			endTime = GetTickCount();
			if (startTime + ms_orig <= endTime) {
				result = 1; /* Timeout */
				--cond->n_waiting;
				waiting = 0;
				goto out;
			} else {
				ms = startTime + ms_orig - endTime;
			}
		}
		/* If we make it here, we are still waiting. */
		if (cond->n_to_wake == 0) {
			/* There is nobody else who should wake up; reset
			 * the event. */
			ResetEvent(cond->event);
		}
	out:
		LeaveCriticalSection(&cond->lock);
	} while (waiting);

	EnterCriticalSection(lock);

	EnterCriticalSection(&cond->lock);
	if (!cond->n_waiting)
		ResetEvent(cond->event);
	LeaveCriticalSection(&cond->lock);

	return result;
}



#ifdef WIN32_HAVE_CONDITION_VARIABLES
typedef  void  (WINAPI*InitializeConditionVariable_ptr)(PCONDITION_VARIABLE);
typedef BOOL  (WINAPI*SleepConditionVariableCS_ptr)(PCONDITION_VARIABLE, PCRITICAL_SECTION, DWORD) ;
typedef void  (WINAPI*WakeAllConditionVariable_ptr)(PCONDITION_VARIABLE) ;
typedef void  (WINAPI*WakeConditionVariable_ptr)(PCONDITION_VARIABLE);
InitializeConditionVariable_ptr InitializeConditionVariable_fn=NULL ; 
SleepConditionVariableCS_ptr SleepConditionVariableCS_fn =NULL ; 
WakeAllConditionVariable_ptr	WakeAllConditionVariable_fn=NULL;
WakeConditionVariable_ptr	WakeConditionVariable_fn=NULL ; 


 int threadctl_win32_condvar_init(void)
{
	HMODULE  lib;

	lib = GetModuleHandle(TEXT("kernel32.dll"));
	if (lib == NULL)
		return 0;

#define LOAD(name)				\
	name##_fn = (name##_ptr)GetProcAddress(lib, #name)

	LOAD(InitializeConditionVariable);
	LOAD(SleepConditionVariableCS);
	LOAD(WakeAllConditionVariable);
	LOAD(WakeConditionVariable);

	return InitializeConditionVariable_fn && SleepConditionVariableCS_fn &&
	    WakeAllConditionVariable_fn && WakeConditionVariable_fn;
}

 void *
threadctl_win32_condvar_alloc(unsigned condflags)
{
	CONDITION_VARIABLE *cond = (CONDITION_VARIABLE *)malloc(sizeof(CONDITION_VARIABLE));
	if (!cond)
		return NULL;
	InitializeConditionVariable_fn(cond);
	return cond;
}

 void
threadctl_win32_condvar_free(void *cond_)
{
	CONDITION_VARIABLE *cond = (CONDITION_VARIABLE *)cond_;
	/* There doesn't _seem_ to be a cleaup fn here... */
	free(cond);
}

 int
threadctl_win32_condvar_signal(void *cond_, int broadcast)
{
	CONDITION_VARIABLE *cond = (CONDITION_VARIABLE*)cond_;
	if (broadcast)
		WakeAllConditionVariable_fn(cond);
	else
		WakeConditionVariable_fn(cond);
	return 0;
}

 int
threadctl_win32_condvar_wait(void *cond_, void *lock_, const struct timeval *tv)
{
	CONDITION_VARIABLE *cond = (CONDITION_VARIABLE*)cond_;
	CRITICAL_SECTION *lock = (CRITICAL_SECTION*)lock_;
	DWORD ms ; 
	BOOL result;

	if (tv)
		ms = threadctl_tv_to_msec_(tv);
	else
		ms = INFINITE;
	result = SleepConditionVariableCS_fn(cond, lock, ms);
	if (result) {
		if (GetLastError() == WAIT_TIMEOUT)
			return 1;
		else
			return -1;
	} else {
		return 0;
	}
}
#endif


 int threadctl_use_windows_threads(void)
 {
	 struct threadctl_lock_callbacks cbs = {
		 THREADCTL_LOCK_API_VERSION,
		 THREADCTL_LOCKTYPE_RECURSIVE,
		 threadctl_win32_lock_create,
		 threadctl_win32_lock_free,
		 threadctl_win32_lock,
		 threadctl_win32_unlock
	 };


	 struct threadctl_condition_callbacks cond_cbs = {
		 THREADCTL_COND_API_VERSION,
		 threadctl_win32_cond_alloc,
		 threadctl_win32_cond_free,
		 threadctl_win32_cond_signal,
		 threadctl_win32_cond_wait
	 };
	   
#ifdef WIN32_HAVE_CONDITION_VARIABLES
	 struct threadctl_condition_callbacks condvar_cbs = {
		 THREADCTL_COND_API_VERSION,
		 threadctl_win32_condvar_alloc,
		 threadctl_win32_condvar_free,
		 threadctl_win32_condvar_signal,
		 threadctl_win32_condvar_wait
	 };
#endif

	 threadctl_set_lock_callbacks(&cbs);
	 threadctl_set_id_callback(threadctl_win32_get_id);
#ifdef WIN32_HAVE_CONDITION_VARIABLES
	 if (threadctl_win32_condvar_init()) {
		 threadctl_set_condition_callbacks(&condvar_cbs);
		 return 0;
	 }
#endif
	 threadctl_set_condition_callbacks(&cond_cbs);

	 return 0;
 }
