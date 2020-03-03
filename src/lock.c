/*
 * Copyright (C) 2003 Robert Lougher <rob@lougher.demon.co.uk>.
 *
 * This file is part of JamVM.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#include <errno.h>
#include <sys/time.h>

#include "jam.h"
#include "thread.h"
#include "hash.h"
#include "alloc.h"

#include "lock_md.h"

/* Trace lock operations and inflation/deflation */
#ifdef TRACELOCK
#define TRACE(x) printf x
#else
#define TRACE(x)
#endif

#define HASHTABSZE 1<<5
#define HASH(obj) ((int) obj >> LOG_OBJECT_GRAIN)
#define COMPARE(obj, mon, hash1, hash2) hash1 == hash2
#define PREPARE(obj) allocMonitor(obj)
#define FOUND(ptr) ptr->in_use = TRUE

/* lockword format in "thin" mode
  31                                         0
   -------------------------------------------
  |              thread ID          | count |0|
   -------------------------------------------
                                             ^ shape bit

  lockword format in "fat" mode
  31                                         0
   -------------------------------------------
  |                 Monitor*                |1|
   -------------------------------------------
                                             ^ shape bit
*/

#define SHAPE_BIT   0x1
#define COUNT_SIZE  8
#define COUNT_SHIFT 1
#define COUNT_MASK  (((1<<COUNT_SIZE)-1)<<COUNT_SHIFT)

#define TID_SHIFT   (COUNT_SIZE+COUNT_SHIFT)
#define TID_SIZE    32-TID_SHIFT
#define TID_MASK    (((1<<TID_SIZE)-1)<<TID_SHIFT)

#define SCAVENGE(ptr)                            \
({                                               \
    Monitor *mon = (Monitor *)ptr;               \
    char res = !mon->in_use;                     \
    if(res) {                                    \
        mon->next = mon_free_list;               \
        mon_free_list = mon;                     \
	mon->in_use = TRUE;                      \
    }                                            \
    res;                                         \
})

static Monitor *mon_free_list = NULL;
static HashTable mon_cache;

void monitorInit(Monitor *mon) {
    pthread_mutex_init(&mon->lock, NULL);
    pthread_cond_init(&mon->cv, NULL);
    mon->owner = 0;
    mon->count = 0;
    mon->waiting = 0;
    mon->notifying = 0;
    mon->interrupting = 0;
    mon->entering = 0;
}

void monitorLock(Monitor *mon, Thread *self) {
    if(mon->owner == self)
        mon->count++;
    else {
        mon->entering++;
	disableSuspend(self);
	self->state = WAITING;
        pthread_mutex_lock(&mon->lock);
	self->state = RUNNING;
	enableSuspend(self);
        mon->entering--;
	mon->owner = self;
    }
}

int monitorTryLock(Monitor *mon, Thread *self) {
    if(mon->owner == self)
        mon->count++;
    else {
        if(pthread_mutex_trylock(&mon->lock))
            return FALSE;
	mon->owner = self;
    }

    return TRUE;
}

void monitorUnlock(Monitor *mon, Thread *self) {
    if(mon->owner == self)
        if(mon->count == 0) {
            mon->owner = 0;
            pthread_mutex_unlock(&mon->lock);
        } else
            mon->count--;
}

int monitorWait(Monitor *mon, Thread *self, long long ms, int ns) {
    char interrupted = 0;
    int old_count;
    char timed = (ms != 0) || (ns != 0);
    struct timespec ts;

    if(mon->owner != self)
	return FALSE;

    /* We own the monitor */

    disableSuspend(self);

    old_count = mon->count;
    mon->count = 0;
    mon->owner = NULL;
    mon->waiting++;

    if(timed) {
        struct timeval tv;

        gettimeofday(&tv, 0);

        ts.tv_sec = tv.tv_sec + ms/1000;
        ts.tv_nsec = (tv.tv_usec + ((ms%1000)*1000))*1000 + ns;

        if(ts.tv_nsec > 999999999L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
    }

    self->wait_mon = mon;
    self->state = WAITING;

    if(self->interrupted)
        interrupted = TRUE;
    else {

wait_loop:
        if(timed) {
            if(pthread_cond_timedwait(&mon->cv, &mon->lock, &ts) == ETIMEDOUT)
                goto out;
	} else
            pthread_cond_wait(&mon->cv, &mon->lock);

        /* see why we were signalled... */

        if(self->interrupting) {
            interrupted = TRUE;
            self->interrupting = FALSE;
            mon->interrupting--;
        } else
            if(mon->notifying)
                mon->notifying--;
            else
                goto wait_loop;
    }
out:

    self->state = RUNNING;
    self->wait_mon = 0;

    mon->owner = self;
    mon->count = old_count;
    mon->waiting--;

    enableSuspend(self);

    if(interrupted) {
        self->interrupted = FALSE;
        signalException("java/lang/InterruptedException", NULL);
    }

    return TRUE;
}

int monitorNotify(Monitor *mon, Thread *self) {
    if(mon->owner != self)
        return FALSE;

    if((mon->notifying + mon->interrupting) < mon->waiting) {
        mon->notifying++;
        pthread_cond_signal(&mon->cv);
    }

    return TRUE;
}

int monitorNotifyAll(Monitor *mon, Thread *self) {
    if(mon->owner != self)
        return FALSE;

    mon->notifying = mon->waiting - mon->interrupting;
    pthread_cond_broadcast(&mon->cv);

    return TRUE;
}

Monitor *allocMonitor(Object *obj) {
    Monitor *mon;

    if(mon_free_list != NULL) {
        mon = mon_free_list;
        mon_free_list = mon->next;	
    } else {
        mon = (Monitor *)malloc(sizeof(Monitor));
        monitorInit(mon);
        mon->in_use = TRUE;
    }
    return mon;
}

Monitor *findMonitor(Object *obj) {
    int lockword = obj->lock;

    if(lockword & SHAPE_BIT)
        return (Monitor*) (lockword & ~SHAPE_BIT);
    else {
        Monitor *mon;
	findHashEntry(mon_cache, obj, mon, TRUE, TRUE);
        return mon;
    }
}

void inflate(Object *obj, Monitor *mon, Thread *self) {
    TRACE(("Inflating obj 0x%x...\n", obj));
    clear_flc_bit(obj);
    monitorNotifyAll(mon, self);
    obj->lock = (int) mon | SHAPE_BIT;
}

void objectLock(Object *obj) {
    Thread *self = threadSelf();
    unsigned int thin_locked = self->id<<TID_SHIFT;
    Monitor *mon;

    TRACE(("Lock on obj 0x%x...\n", obj));

    if(COMPARE_AND_SWAP(&obj->lock, 0, thin_locked))
        return;

    if((obj->lock & (TID_MASK|SHAPE_BIT)) == thin_locked) {
        int count = obj->lock & COUNT_MASK;

	if(count < (((1<<COUNT_SIZE)-1)<<COUNT_SHIFT))
            obj->lock += 1<<COUNT_SHIFT;
        else {
            mon = findMonitor(obj);
            monitorLock(mon, self);
	    inflate(obj, mon, self);
	    mon->count = 1<<COUNT_SIZE;
	}
	return;
    }

    mon = findMonitor(obj);
    monitorLock(mon, self);

    while((obj->lock & SHAPE_BIT) == 0) {
        set_flc_bit(obj);

	if(COMPARE_AND_SWAP(&obj->lock, 0, self))
            inflate(obj, mon, self);
	else
            monitorWait(mon, self, 0, 0);
    }
}

void objectUnlock(Object *obj) {
    Thread *self = threadSelf();
    unsigned int thin_locked = self->id<<TID_SHIFT;

    TRACE(("Unlock on obj 0x%x...\n", obj));

    if(obj->lock == thin_locked) {
        obj->lock = 0;

retry:
	if(test_flc_bit(obj)) {
            Monitor *mon = findMonitor(obj);

            if(!monitorTryLock(mon, self)) {
                pthread_yield();
                goto retry;
	    }

	    if(test_flc_bit(obj))
                monitorNotify(mon, self);

            monitorUnlock(mon, self);
	}
    } else {
        if((obj->lock & (TID_MASK|SHAPE_BIT)) == thin_locked)
            obj->lock -= 1<<COUNT_SHIFT;
	else
            if((obj->lock & SHAPE_BIT) != 0) {
                Monitor *mon = (Monitor*) (obj->lock & ~SHAPE_BIT);

	        if((mon->count == 0) && (mon->entering == 0) && (mon->waiting == 0)) {
                    TRACE(("Deflating obj 0x%x...\n", obj));
                    obj->lock = 0;
	            mon->in_use = FALSE;
	        }

	        monitorUnlock(mon, self);
            }
    }
}

void objectWait(Object *obj, long long ms, int ns) {
    Thread *self = threadSelf();
    int lockword = obj->lock;
    Monitor *mon;

    TRACE(("Wait on obj 0x%x...\n", obj));

    if((lockword & SHAPE_BIT) == 0) {
        int tid = (lockword&TID_MASK)>>TID_SHIFT;
	if(tid == self->id) {
            mon = findMonitor(obj);
            monitorLock(mon, self);
            inflate(obj, mon, self);
	    mon->count = (lockword&COUNT_MASK)>>COUNT_SHIFT;
	} else
            goto not_owner;
    } else
        mon = (Monitor*) (lockword & ~SHAPE_BIT);

    if(monitorWait(mon, self, ms, ns))
        return;

not_owner:
    signalException("java/lang/IllegalMonitorStateException", "thread not owner");
}

void objectNotify(Object *obj) {
    Thread *self = threadSelf();
    int lockword = obj->lock;

    TRACE(("Notify on obj 0x%x...\n", obj));

    if((lockword & SHAPE_BIT) == 0) {
        int tid = (lockword&TID_MASK)>>TID_SHIFT;
	if(tid == self->id)
            return;
    } else {
        Monitor *mon = (Monitor*) (lockword & ~SHAPE_BIT);
        if(monitorNotify(mon, self))
            return;
    }

    signalException("java/lang/IllegalMonitorStateException", "thread not owner");
}

void objectNotifyAll(Object *obj) {
    Thread *self = threadSelf();
    int lockword = obj->lock;

    TRACE(("NotifyAll on obj 0x%x...\n", obj));

    if((lockword & SHAPE_BIT) == 0) {
        int tid = (lockword&TID_MASK)>>TID_SHIFT;
	if(tid == self->id)
            return;
    } else {
        Monitor *mon = (Monitor*) (lockword & ~SHAPE_BIT);
        if(monitorNotifyAll(mon, self))
            return;
    }

    signalException("java/lang/IllegalMonitorStateException", "thread not owner");
}

void initialiseMonitor() {
    initHashTable(mon_cache, HASHTABSZE);
}

