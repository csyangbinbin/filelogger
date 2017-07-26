#ifndef __THREADCTRL_INCLUDE__
#define __THREADCTRL_INCLUDE__

#ifdef __cplusplus
extern "C" {
#endif

/*ËøÀàÐÍ:µÝ¹éËø*/
#define THREADCTL_LOCKTYPE_RECURSIVE 1
/*ËøÀàÐÍ:¶ÁÐ´Ëø*/
#define THREADCTL_LOCKTYPE_READWRITE 2

/*locktype*/
#define THREADCTL_WRITE	0x04
#define THREADCTL_READ	0x08
#define THREADCTL_TRY    0x10


struct threadctl_lock_callbacks {
	/** The current version of the locking API.  Set this to
	 * threadctl_LOCK_API_VERSION */
	int lock_api_version;
	/** Which kinds of locks does this version of the locking API
	 * support?  A bitfield of threadctl_LOCKTYPE_RECURSIVE and
	 * threadctl_LOCKTYPE_READWRITE.
	 *
	 * (Note that RECURSIVE locks are currently mandatory, and
	 * READWRITE locks are not currently used.)
	 **/
	unsigned supported_locktypes;
	/** Function to allocate and initialize new lock of type 'locktype'.
	 * Returns NULL on failure. */
	void *(*alloc)(unsigned locktype);
	/** Funtion to release all storage held in 'lock', which was created
	 * with type 'locktype'. */
	void (*free)(void *lock);
	/** Acquire an already-allocated lock at 'lock' with mode 'mode'.
	 * Returns 0 on success, and nonzero on failure. */
	int (*lock)(unsigned mode, void *lock);
	/** Release a lock at 'lock' using mode 'mode'.  Returns 0 on success,
	 * and nonzero on failure. */
	int (*unlock)(unsigned mode, void *lock);
};

struct threadctl_condition_callbacks {
	/** The current version of the conditions API.  Set this to
	 * threadctl_CONDITION_API_VERSION */
	int condition_api_version;
	/** Function to allocate and initialize a new condition variable.
	 * Returns the condition variable on success, and NULL on failure.
	 * The 'condtype' argument will be 0 with this API version.
	 */
	void *(*alloc_condition)(unsigned condtype);
	/** Function to free a condition variable. */
	void (*free_condition)(void *cond);
	/** Function to signal a condition variable.  If 'broadcast' is 1, all
	 * threads waiting on 'cond' should be woken; otherwise, only on one
	 * thread is worken.  Should return 0 on success, -1 on failure.
	 * This function will only be called while holding the associated
	 * lock for the condition.
	 */
	int (*signal_condition)(void *cond, int broadcast);
	/** Function to wait for a condition variable.  The lock 'lock'
	 * will be held when this function is called; should be released
	 * while waiting for the condition to be come signalled, and
	 * should be held again when this function returns.
	 * If timeout is provided, it is interval of seconds to wait for
	 * the event to become signalled; if it is NULL, the function
	 * should wait indefinitely.
	 *
	 * The function should return -1 on error; 0 if the condition
	 * was signalled, or 1 on a timeout. */
	int (*wait_condition)(void *cond, void *lock,
	    const struct timeval *timeout);
};

#define THREADCTL_LOCK_API_VERSION		1
#define THREADCTL_COND_API_VERSION		1

int	threadctl_set_lock_callbacks(const struct threadctl_lock_callbacks * cbs);
int	threadctl_set_condition_callbacks( const struct threadctl_condition_callbacks * cbs);
void	threadctl_set_id_callback(  unsigned long (*id_fn)(void)) ; 


int	threadctl_use_windows_threads(void) ; 
int	threadctl_use_pthreads(void);



extern struct threadctl_lock_callbacks threadctl_lock_fns_ ; 
extern struct threadctl_condition_callbacks threadctl_cond_fns_ ;
extern unsigned long (*threadctl_id_fn_)(void);


/** Return the ID of the current thread, or 1 if threading isn't enabled. */
#define THREADCTL_GET_ID() \
	(threadctl_id_fn_ ? threadctl_id_fn_() : 1)


/** Allocate a new lock, and store it in lockvar, a void*.  Sets lockvar to
    NULL if locking is not enabled. */
#define THREADCTL_ALLOC_LOCK(lockvar, locktype)		\
	((lockvar) = threadctl_lock_fns_.alloc ?		\
	    threadctl_lock_fns_.alloc(locktype) : NULL)


#define THREADCTL_ALLOC_NORMAL_LOCK(lockvar)		\
	((lockvar) = threadctl_lock_fns_.alloc ?		\
	threadctl_lock_fns_.alloc(0) : NULL)

#define THREADCTL_ALLOC_RW_LOCK(lockvar)		\
	((lockvar) = threadctl_lock_fns_.alloc ?		\
	threadctl_lock_fns_.alloc(THREADCTL_LOCKTYPE_READWRITE) : NULL)

#define THREADCTL_ALLOC_RECURSIVE_LOCK(lockvar)		\
	((lockvar) = threadctl_lock_fns_.alloc ?		\
	threadctl_lock_fns_.alloc(THREADCTL_LOCKTYPE_RECURSIVE) : NULL)


/** Free a given lock, if it is present and locking is enabled. */
#define THREADCTL_FREE_LOCK(lockvar, locktype)				\
	do {								\
		void *lock_tmp_ = (lockvar);				\
		if (lock_tmp_ && threadctl_lock_fns_.free)		\
			threadctl_lock_fns_.free(lock_tmp_); \
	} while (0)

/** Acquire a lock. */
#define THREADCTL_LOCK(lockvar,mode)					\
	do {								\
		if (lockvar)						\
			threadctl_lock_fns_.lock(mode, lockvar);		\
	} while (0)

/** Release a lock */
#define THREADCTL_UNLOCK(lockvar,mode)					\
	do {								\
		if (lockvar)						\
			threadctl_lock_fns_.unlock(mode, lockvar);	\
	} while (0)

/** Helper: put lockvar1 and lockvar2 into pointerwise ascending order. */
#define THREADCTL_SORTLOCKS_(lockvar1, lockvar2)				\
	do {								\
		if (lockvar1 && lockvar2 && lockvar1 > lockvar2) {	\
			void *tmp = lockvar1;				\
			lockvar1 = lockvar2;				\
			lockvar2 = tmp;					\
		}							\
	} while (0)

/** Acquire both lock1 and lock2.  Always allocates locks in the same order,
 * so that two threads locking two locks with LOCK2 will not deadlock. */
#define THREADCTL_LOCK2(lock1,lock2,mode1,mode2)				\
	do {								\
		void *lock1_tmplock_ = (lock1);				\
		void *lock2_tmplock_ = (lock2);				\
		EVLOCK_SORTLOCKS_(lock1_tmplock_,lock2_tmplock_);	\
		EVLOCK_LOCK(lock1_tmplock_,mode1);			\
		if (lock2_tmplock_ != lock1_tmplock_)			\
			EVLOCK_LOCK(lock2_tmplock_,mode2);		\
	} while (0)
/** Release both lock1 and lock2.  */
#define THREADCTL_UNLOCK2(lock1,lock2,mode1,mode2)				\
	do {								\
		void *lock1_tmplock_ = (lock1);				\
		void *lock2_tmplock_ = (lock2);				\
		EVLOCK_SORTLOCKS_(lock1_tmplock_,lock2_tmplock_);	\
		if (lock2_tmplock_ != lock1_tmplock_)			\
			EVLOCK_UNLOCK(lock2_tmplock_,mode2);		\
		EVLOCK_UNLOCK(lock1_tmplock_,mode1);			\
	} while (0)


/** Try to grab the lock for 'lockvar' without blocking, and return 1 if we
 * manage to get it. */
 inline int
THREADCTL_TRY_LOCK_(void *lock)
{
	if (lock && threadctl_lock_fns_.lock) {
		int r = threadctl_lock_fns_.lock(THREADCTL_TRY, lock);
		return !r;
	} else {
		/* Locking is disabled either globally or for this thing;
		 * of course we count as having the lock. */
		return 1;
	}
}

/** Allocate a new condition variable and store it in the void *, condvar */
#define THREADCTL_ALLOC_COND(condvar)					\
	do {								\
		(condvar) = threadctl_cond_fns_.alloc_condition ?	\
		    threadctl_cond_fns_.alloc_condition(0) : NULL;	\
	} while (0)
/** Deallocate and free a condition variable in condvar */
#define THREADCTL_FREE_COND(cond)					\
	do {								\
		if (cond)						\
			threadctl_cond_fns_.free_condition((cond));	\
	} while (0)
/** Signal one thread waiting on cond */
#define THREADCTL_COND_SIGNAL(cond)					\
	( (cond) ? threadctl_cond_fns_.signal_condition((cond), 0) : 0 )
/** Signal all threads waiting on cond */
#define THREADCTL_COND_BROADCAST(cond)					\
	( (cond) ? threadctl_cond_fns_.signal_condition((cond), 1) : 0 )
/** Wait until the condition 'cond' is signalled.  Must be called while
 * holding 'lock'.  The lock will be released until the condition is
 * signalled, at which point it will be acquired again.  Returns 0 for
 * success, -1 for failure. */
#define THREADCTL_COND_WAIT(cond, lock)					\
	( (cond) ? threadctl_cond_fns_.wait_condition((cond), (lock), NULL) : 0 )
/** As THREADCTL_COND_WAIT, but gives up after 'tv' has elapsed.  Returns 1
 * on timeout. */
#define THREADCTL_COND_WAIT_TIMED(cond, lock, tv)			\
	( (cond) ? threadctl_cond_fns_.wait_condition((cond), (lock), (tv)) : 0 )

#ifdef __cplusplus
}
#endif

#endif