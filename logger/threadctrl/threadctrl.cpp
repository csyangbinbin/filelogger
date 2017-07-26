#include "stdafx.h"
#include "threadctrl.h"
#include <string.h>

 struct threadctl_lock_callbacks threadctl_lock_fns_ = {
	0, 0, NULL, NULL, NULL, NULL
};

  struct threadctl_condition_callbacks threadctl_cond_fns_ = {
	0, NULL, NULL, NULL, NULL
};

unsigned long (*threadctl_id_fn_)(void) = NULL;

int threadctl_set_lock_callbacks(const struct threadctl_lock_callbacks * cbs)
{
	threadctl_lock_callbacks* target = &threadctl_lock_fns_ ;

	if (cbs->alloc && cbs->free && cbs->lock && cbs->unlock) {
		memcpy(target, cbs, sizeof(threadctl_lock_callbacks));
		return 0;
	} else {
		return -1;
	}


}
int threadctl_set_condition_callbacks( const struct threadctl_condition_callbacks * cbs)
{
	threadctl_condition_callbacks* target = &threadctl_cond_fns_ ;

	if (cbs->alloc_condition && cbs->free_condition &&
		cbs->signal_condition && cbs->wait_condition) {
			memcpy(target, cbs, sizeof(threadctl_condition_callbacks));
			return 0 ;
	}
	else
		return -1 ;
}


void threadctl_set_id_callback(  unsigned long (*id_fn)(void))
{
threadctl_id_fn_ = id_fn ; 
}