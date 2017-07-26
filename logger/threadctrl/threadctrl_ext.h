#ifndef __THREADCTL_EXT_INCLUDE__
#define __THREADCTL_EXT_INCLUDE__
#include "threadctrl.h"
#include <cassert>

class lock_guard  
{  
public:  
	explicit lock_guard(void* lock)  
		: m_(lock)  
	{  
		THREADCTL_LOCK(m_,THREADCTL_WRITE);
	}  

	~lock_guard()  
	{  
		THREADCTL_UNLOCK(m_,THREADCTL_WRITE);
	}  

private:  
	lock_guard(const lock_guard&);  
	lock_guard& operator=(const lock_guard&);  
	void* m_;  
};


class countdown_latch
{
public:
	explicit	countdown_latch(int count)
		:count_(count)
	{
		THREADCTL_ALLOC_LOCK(lock_ ,0);
		THREADCTL_ALLOC_COND(cond_);
	}

	~countdown_latch()
	{
		THREADCTL_FREE_LOCK(lock_ ,0);
		THREADCTL_FREE_COND(cond_);
	}

	void		wait()
	{
		lock_guard guard(lock_);
		while(count_>0)
		{
			THREADCTL_COND_WAIT(cond_ , lock_);
		}
	}

	void		countdown()
	{
		lock_guard guard(lock_);
		--count_;
		if(count_<=0)
			THREADCTL_COND_BROADCAST(cond_);
	}

	int			get_count() const { return count_;}

	void		set_count(int count)
	{
		lock_guard guard(lock_);
		count_ = count ; 
	}
private:	
	void*		lock_;
	void*		cond_;
	int			count_ ;
} ;


class LockWrapper
{
public:
	LockWrapper()
	{
		THREADCTL_ALLOC_LOCK(lock , THREADCTL_LOCKTYPE_READWRITE);
		assert(lock);
	}
	~LockWrapper()
	{
		if(lock)
			THREADCTL_FREE_LOCK(lock ,THREADCTL_LOCKTYPE_READWRITE );
	}

	void* native() { return lock ;}

private:
	void* lock ;
} ; 


#endif