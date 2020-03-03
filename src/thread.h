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

#ifndef CREATING
#include <pthread.h>
#include <setjmp.h>

/* Thread states */

#define CREATING     0
#define STARTED      1
#define RUNNING      2
#define WAITING      3
#define SUSPENDED    5

typedef struct thread Thread;

typedef struct monitor {
    pthread_mutex_t lock;
    pthread_cond_t cv;
    Thread *owner;
    int count;
    int waiting;
    int notifying;
    int interrupting;
    int entering;
    struct monitor *next;
    char in_use;
} Monitor;

struct thread {
    char state;
    char interrupted;
    char interrupting;
    char suspend;
    char blocking;
    pthread_t tid;
    int id;
    ExecEnv *ee;
    void *stack_top;
    void *stack_base;
    Monitor *wait_mon;
    Thread *prev, *next;
};

extern Thread *threadSelf();
extern Thread *threadSelf0(Object *jThread);

extern void *getStackTop(Thread *thread);
extern void *getStackBase(Thread *thread);

extern void threadInterrupt(Thread *thread);
extern void threadSleep(Thread *thread, long long ms, int ns);
extern int systemIdle(Thread *self);

extern void disableSuspend0(Thread *thread, void *stack_top);
extern void enableSuspend(Thread *thread);

#define disableSuspend(thread)          \
{                                       \
    sigjmp_buf *env;                    \
    env = alloca(sizeof(sigjmp_buf));   \
    sigsetjmp(*env, FALSE);             \
    disableSuspend0(thread, (void*)env);\
}

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t cv;
} VMWaitLock;

typedef pthread_mutex_t VMLock;

#define initVMLock(lock) pthread_mutex_init(&lock, NULL)
#define initVMWaitLock(wait_lock) {            \
    pthread_mutex_init(&wait_lock.lock, NULL); \
    pthread_cond_init(&wait_lock.cv, NULL);    \
}

#define lockVMLock(lock, self) { \
    self->state = WAITING;       \
    pthread_mutex_lock(&lock);   \
    self->state = RUNNING;       \
}

#define unlockVMLock(lock, self) pthread_mutex_unlock(&lock)

#define lockVMWaitLock(wait_lock, self) lockVMLock(wait_lock.lock, self)
#define unlockVMWaitLock(wait_lock, self) unlockVMLock(wait_lock.lock, self)
#define waitVMWaitLock(wait_lock, self) {              \
    self->state = WAITING;                             \
    pthread_cond_wait(&wait_lock.cv, &wait_lock.lock); \
    self->state = RUNNING;                             \
}
#define notifyVMWaitLock(wait_lock, self) pthread_cond_signal(&wait_lock.cv)
#endif
