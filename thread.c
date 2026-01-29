/*
 * thread.c
 *
 * Implementation of the threading library.
 */

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "ut369.h"
#include "queue.h"
#include "thread.h"
#include "schedule.h"
#include "interrupt.h"

/* TODO: put your global variables here */

static struct thread *current_thread;
static struct thread *previous_thread;
static struct thread *all_threads[THREAD_MAX_THREADS];
static int available_ids[THREAD_MAX_THREADS];

/**************************************************************************
 * Cooperative threads: Refer to ut369.h and this file for the detailed 
 *                      descriptions of the functions you need to implement. 
 **************************************************************************/

void signal_handler(int signum) {
    if (signum == SIGALRM) {
        thread_yield(THREAD_ANY);
    }
}

/* Initialize the thread subsystem */
void
thread_init(void)
{
	// Initialize the first thread (main thread)
	struct thread *main_thread = malloc(sizeof(struct thread));
	assert(main_thread != NULL);

	main_thread->id = 0;
	main_thread->state = running;
	main_thread->is_killed = false;


	main_thread->self = main_thread;
	main_thread->wait_queue = queue_create(THREAD_MAX_THREADS);
	queue_set_owner(main_thread->wait_queue, &(main_thread->self));

	main_thread->waiting_for_queue = NULL;

	struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, NULL);
	// Set the current thread to the main thread
	current_thread = main_thread;

    for (int i = 1; i < THREAD_MAX_THREADS; i++) {
        available_ids[i] = 1;
    }

	available_ids[0] = 0;

	all_threads[0] = current_thread;
}

/* Returns the tid of the current running thread. */
Tid
thread_id(void)
{
	return current_thread->id;
}

/* Return the thread structure of the thread with identifier tid, or NULL if 
 * does not exist. Used by thread_yield and thread_wait's placeholder 
 * implementation.
 */
static struct thread * 
thread_get(Tid tid)
{
	if(available_ids[tid] == 1){
		return NULL;
	}
	else{
		struct thread *target = all_threads[tid];
		if(target == NULL){
			assert(false);
		}
		return target;
	}
}

/* Return whether the thread with identifier tid is runnable.
 * Used by thread_yield and thread_wait's placeholder implementation
 */
static bool
thread_runnable(Tid tid)
{
	struct thread *target = thread_get(tid);
	if (target == NULL){
		return false;
	}
	return ((target->state) == runnable) || ((target->state) == running);
}

/* Context switch to the next thread. Used by thread_yield. */
static void
thread_switch(struct thread * next)
{
	volatile int flag = 0;
	getcontext(&(current_thread->my_context));
	
	if (flag == 0){
		flag = 1;
		previous_thread = current_thread;
		current_thread = next;
		current_thread->state = running;
		setcontext(&(current_thread->my_context));
	}
	else{
		if (previous_thread->state == zombie){
			if(previous_thread->stack_pointer != NULL){
				free(previous_thread->stack_pointer);
				previous_thread->stack_pointer = NULL;
			}
		}

		if(current_thread->is_killed){
			current_thread->exit_code = THREAD_KILLED;
			thread_exit(THREAD_KILLED);
		}
	}
}

/* Voluntarily pauses the execution of current thread and invokes scheduler
 * to switch to another thread.
 */
Tid
thread_yield(Tid want_tid)
{
	/* TODO: this currently only works when want_tid refers to the tid
	 *       of a different thread. You need to improve or rewrite
	 *       this function.
	 */
	int enabled = interrupt_off();

    if (want_tid == thread_id()) {
		interrupt_set(enabled);
        return thread_id();
    }

    // Case 2: Yield to any available thread
    if (want_tid == THREAD_ANY) {
        struct thread *next_thread = scheduler->dequeue();
        if (next_thread != NULL) {
			if(current_thread->state != blocked){
				current_thread->state = runnable;
				scheduler->enqueue(current_thread);
			}
            thread_switch(next_thread);
			interrupt_set(enabled);
            return next_thread->id;
        }
		interrupt_set(enabled);
        return THREAD_NONE;
    }

    // Case 3: Invalid tid range
    if (want_tid < 0 || want_tid >= THREAD_MAX_THREADS) {
		interrupt_set(enabled);
        return THREAD_INVALID;
    }

    // Case 4: Check if target exists and is runnable
    struct thread *target = thread_get(want_tid);
    if (target == NULL || !thread_runnable(want_tid)) {
		interrupt_set(enabled);
        return THREAD_INVALID;
    }

    // Case 5: Try to get target from scheduler
    struct thread *scheduled_target = scheduler->remove(want_tid);
    if (scheduled_target == NULL) {
		interrupt_set(enabled);
        return THREAD_INVALID;
    }

    // Only modify current thread state and scheduler if we're sure we can switch
    current_thread->state = runnable;
    scheduler->enqueue(current_thread);
    thread_switch(scheduled_target);
	interrupt_set(enabled);
    return want_tid;
}

/* Fully clean up a thread structure and make its tid available for reuse.
 * Used by thread_wait's placeholder implementation
 */
static void
thread_destroy(struct thread * dead)
{
	assert(dead->stack_pointer == NULL);
	assert(dead->wait_queue == NULL);
	available_ids[dead->id] = 1;
	all_threads[dead->id] = NULL;
    free(dead);
}

/* New thread starts by calling thread_stub. The arguments to thread_stub are
 * the thread_main() function, and one argument to the thread_main() function. 
 */
static void
thread_stub(int (*thread_main)(void *), void *arg)
{
	interrupt_on();
	if (previous_thread->state == zombie){
		if (previous_thread->stack_pointer != NULL){
			free(previous_thread->stack_pointer);
			previous_thread->stack_pointer = NULL;
		}
	}
	if (current_thread->is_killed){
		current_thread->exit_code = THREAD_KILLED;
		thread_exit(THREAD_KILLED);
	}

	// Now run the thread's main function
	int ret = thread_main(arg);
	current_thread->exit_code = ret;
	thread_exit(ret);
}

Tid
thread_create(int (*fn)(void *), void *parg)
{
	int enabled = interrupt_off();
    // Find an available thread ID
    int tid = -1;
    for (int i = 0; i < THREAD_MAX_THREADS; i++) {
        if (available_ids[i] == 1) {
            tid = i;
            available_ids[i] = 0;
            break;
        }
    }
    if (tid == -1) {
		interrupt_set(enabled);
        return THREAD_NOMORE;
    }

    // Allocate memory for the new thread
    struct thread *new_thread = malloc(sizeof(struct thread));
    if (new_thread == NULL) {
		available_ids[tid] = 1;
		interrupt_set(enabled);
        return THREAD_NOMEMORY;
    }

	new_thread->self = new_thread;

    // Allocate stack for the new thread
    void *stack = malloc(THREAD_MIN_STACK);
    if (stack == NULL) {
		available_ids[tid] = 1;
        free(new_thread);
		interrupt_set(enabled);
        return THREAD_NOMEMORY;
    }

	new_thread->wait_queue = queue_create(THREAD_MAX_THREADS);
	queue_set_owner(new_thread->wait_queue, &(new_thread->self));
	if (new_thread->wait_queue == NULL) {
		free(new_thread->wait_queue);
		new_thread->wait_queue = NULL;
		interrupt_set(enabled);
		return THREAD_NOMEMORY;
	}

    // Initialize the new thread
    new_thread->id = tid;
    new_thread->state = runnable;
    new_thread->is_killed = false;
	new_thread->late_waiter_succeed = false;
	new_thread->stack_pointer = stack;
	new_thread->waiting_for_queue = NULL;

    // Set up the context for the new thread
    getcontext(&new_thread->my_context);

	// don't automatically switch context when thread exits
	new_thread->my_context.uc_link = NULL;
	// instruction pointer
	new_thread->my_context.uc_mcontext.gregs[REG_RIP] = (greg_t)&thread_stub;
	// stack pointer
    new_thread->my_context.uc_mcontext.gregs[REG_RSP] = (greg_t)(stack + THREAD_MIN_STACK - 8);
	// first argument register (first argument of stub)
    new_thread->my_context.uc_mcontext.gregs[REG_RDI] = (greg_t)fn;
	// second argument register (second argument of stub)
    new_thread->my_context.uc_mcontext.gregs[REG_RSI] = (greg_t)parg;
	// stack is valid to use
	new_thread->my_context.uc_flags = 0;
	
    // Add the new thread to the all_threads array
    all_threads[tid] = new_thread;

    scheduler->enqueue(new_thread);

	interrupt_set(enabled);
    return tid;
}

Tid
thread_kill(Tid tid)
{
	int enabled = interrupt_off();
	if(tid < 0 || tid >= THREAD_MAX_THREADS){
		interrupt_set(enabled);
		return THREAD_INVALID;
	}
	if (tid == thread_id()) {
		interrupt_set(enabled);
		return THREAD_INVALID;
	}

	struct thread *victim_thread = thread_get(tid);
	if (victim_thread == NULL) {
		interrupt_set(enabled);
		return THREAD_INVALID;
	}

	if (victim_thread->state == blocked){
		queue_remove(victim_thread->waiting_for_queue, tid);
		victim_thread->waiting_for_queue = NULL;
		victim_thread->state = runnable;
		scheduler->enqueue(victim_thread);
	}

	victim_thread->is_killed = true; // Mark the thread as killed

	interrupt_set(enabled);
	return tid;
}

void
thread_exit(int exit_code)
{
	int enabled = interrupt_off();
	// Find the next runnable thread
	current_thread->exit_code = exit_code;
	current_thread->state = zombie;
	current_thread->reapers = thread_wakeup(current_thread->wait_queue, 1);
	if (current_thread->reapers == 0){
		current_thread->late_waiter_succeed = true;
	}
	else{
		current_thread->late_waiter_succeed = false;
	}
	struct thread *next_thread = scheduler->dequeue();

	if (next_thread != NULL) {
		interrupt_set(enabled);
		thread_switch(next_thread);
		assert(false);
	}
	ut369_exit(exit_code);
	assert(false);
}

/* Clean-up logic to unload the threading system. Used by ut369.c. You may 
 * assume all threads are either freed or in the zombie state when this is 
 * called.
 */
void
thread_end(void)
{
	if (current_thread->stack_pointer != NULL){
		free(current_thread->stack_pointer);
		current_thread->stack_pointer = NULL;
	}

	if (current_thread->wait_queue != NULL){
		free(current_thread->wait_queue);
		current_thread->wait_queue = NULL;
	}

    for (int i = 0; i < THREAD_MAX_THREADS; i++) {
        if (all_threads[i] != NULL) {
			if (all_threads[i]->stack_pointer != NULL){
				free(all_threads[i]->stack_pointer);
				all_threads[i]->stack_pointer = NULL;
			}
			if (all_threads[i]->wait_queue != NULL){
				free(all_threads[i]->wait_queue);
				all_threads[i]->wait_queue = NULL;
			}
            thread_destroy(all_threads[i]);
        }
    }
}

/**************************************************************************
 * Preemptive threads: Refer to ut369.h for the detailed descriptions of 
 *                     the functions you need to implement. 
 **************************************************************************/

Tid
thread_wait(Tid tid, int *exit_code)
{
	int enabled = interrupt_off();
	
	// Check for invalid conditions
	if (tid < 0 || tid >= THREAD_MAX_THREADS || tid == thread_id()) {
		interrupt_set(enabled);
		return THREAD_INVALID;
	}

	// Get target thread
	struct thread *target = thread_get(tid);
	if (target == NULL) {
		interrupt_set(enabled);
		return THREAD_INVALID;
	}

	if (target->state != zombie) {

		if (target->wait_queue == NULL) {
			assert(false);
		}

		// Sleep on target's wait queue
		Tid result = thread_sleep(target->wait_queue);
		if (result == THREAD_DEADLOCK || result == THREAD_NONE || result == THREAD_INVALID) {
			interrupt_set(enabled);
			return result;
		}

		// After waking up, check if target is zombie
		assert(target->state == zombie);

		if (exit_code != NULL) {
			*exit_code = target->exit_code;
		}
		if (target->reapers == 1){
			if (target->stack_pointer != NULL){
				free(target->stack_pointer);
				target->stack_pointer = NULL;
			}
			if (target->wait_queue != NULL){
				queue_destroy(target->wait_queue);
				target->wait_queue = NULL;
			}
			thread_destroy(target);
			interrupt_set(enabled);
			return tid;
		}
		target->reapers--;
		interrupt_set(enabled);
		return tid;
	}
	else{
		if (target->late_waiter_succeed){
			if (exit_code != NULL) {
				*exit_code = target->exit_code;
			}
			if (target->stack_pointer != NULL){
				free(target->stack_pointer);
				target->stack_pointer = NULL;
			}
			if(target->wait_queue != NULL){
				queue_destroy(target->wait_queue);
				target->wait_queue = NULL;
			}
			thread_destroy(target);
			interrupt_set(enabled);
			return tid;
		}
		else{
			interrupt_set(enabled);
			return THREAD_INVALID;
		}
	}
}


static bool can_deadlock(struct thread * target) {
	// target is who i'm waiting for - waitee
	struct thread *t = target;
	while (t != NULL){
		if (current_thread == t) {
			return true;
		}
		if(t->waiting_for_queue == NULL){
			return false;
		}
		t = queue_get_owner(t->waiting_for_queue);
		
		if (t == target){
			assert(false);
		}
	}
	return false;
}


Tid
thread_sleep(fifo_queue_t *queue)
{
	assert(!interrupt_enabled());
	if (queue == NULL) {
		return THREAD_INVALID;
	}

	if ((queue_get_owner(queue) != NULL) && can_deadlock(queue_get_owner(queue))){
		return THREAD_DEADLOCK;
	}

	current_thread->state = blocked;
	queue_push(queue, current_thread);
	current_thread->waiting_for_queue = queue;
	Tid curr_id = current_thread->id;

	Tid next_tid = thread_yield(THREAD_ANY);

	if (next_tid == THREAD_NONE || next_tid == THREAD_INVALID) {
		current_thread = queue_remove(queue, curr_id);
		current_thread->state = running;
		current_thread->waiting_for_queue = NULL;
		return next_tid;
	}

	return next_tid;
}

/* When the 'all' parameter is 1, wake up all threads waiting in the queue.
 * returns whether a thread was woken up on not. 
 */
int
thread_wakeup(fifo_queue_t *queue, int all)
{
	assert(!interrupt_enabled());
	if (queue == NULL) {
		return 0;
	}

	int count = 0;	

	struct thread *woken_thread;
	if (all == 1) {
		woken_thread = queue_pop(queue);
		while (woken_thread != NULL) {
			assert(woken_thread->state == blocked);
			woken_thread->state = runnable;
			scheduler->enqueue(woken_thread);
			woken_thread->waiting_for_queue = NULL;
			woken_thread = queue_pop(queue);
			count++;
		}
	} else {
		woken_thread = queue_pop(queue);
		if (woken_thread != NULL) {
			assert(woken_thread->state == blocked);
			woken_thread->state = runnable;
			scheduler->enqueue(woken_thread);
			woken_thread->waiting_for_queue = NULL;
			count = 1;
		}
	}
	return count;
}

struct lock {
    struct thread *holder;
	fifo_queue_t *wait_queue;
    int cv_count;
};

struct lock *
lock_create()
{
    int enabled = interrupt_off();
    struct lock *lock = malloc(sizeof(struct lock));
    if (lock == NULL) {
		free(lock);
        interrupt_set(enabled);
        return NULL;
    }
    lock->holder = NULL;
    lock->wait_queue = queue_create(THREAD_MAX_THREADS);
	if (lock->wait_queue == NULL) {
        free(lock);
        interrupt_set(enabled);
        return NULL;
    }
	queue_set_owner(lock->wait_queue, &(lock->holder));
    lock->cv_count = 0;
    
    interrupt_set(enabled);
    return lock;
}

void
lock_destroy(struct lock *lock)
{
    int enabled = interrupt_off();
    assert(lock != NULL);
    assert(lock->holder == NULL);
    assert(queue_count(lock->wait_queue) == 0);
    assert(lock->cv_count == 0); // Ensure no associated CVs
    queue_destroy(lock->wait_queue);
    free(lock);
    interrupt_set(enabled);
}

int
lock_acquire(struct lock *lock)
{
    int enabled = interrupt_off();
    assert(lock != NULL);
    while (!(lock->holder == NULL)) {
        Tid result = thread_sleep(lock->wait_queue);
        interrupt_off(); // Re-disable interrupts after wakeup
        if (result == THREAD_DEADLOCK || result == THREAD_NONE || result == THREAD_INVALID) {
            interrupt_set(enabled);
            return result;
        }
    }
    assert(lock->holder == NULL);
    lock->holder = current_thread;
    interrupt_set(enabled);
    return 0;
}

void
lock_release(struct lock *lock)
{
    int enabled = interrupt_off();
    assert(lock != NULL);
    assert(lock->holder == current_thread);

    lock->holder = NULL;
    thread_wakeup(lock->wait_queue, 0);
    interrupt_set(enabled);
}

struct cv {
    struct lock *associated_lock;
	fifo_queue_t *wait_queue;
};

struct cv *
cv_create(struct lock *lock)
{
    int enabled = interrupt_off();
    assert(lock != NULL);
    struct cv *cv = malloc(sizeof(struct cv));
    if (cv == NULL) {
		free(cv);
        interrupt_set(enabled);
        return NULL;
    }
	cv->wait_queue = queue_create(THREAD_MAX_THREADS);
	if (cv->wait_queue == NULL) {
		free(cv);
		interrupt_set(enabled);
		return NULL;
	}
    cv->associated_lock = lock;
	queue_set_owner(cv->wait_queue, &(cv->associated_lock->holder));

    lock->cv_count += 1;
    interrupt_set(enabled);
    return cv;
}

void
cv_destroy(struct cv *cv)
{
    int enabled = interrupt_off();
    assert(cv != NULL);

    if (cv->associated_lock) {
        cv->associated_lock->cv_count -= 1;
		cv->associated_lock = NULL;
    }
    
	queue_destroy(cv->wait_queue);
    free(cv);
    interrupt_set(enabled);
}

int
cv_wait(struct cv *cv)
{
    int enabled = interrupt_off();
    assert(cv != NULL);
    assert(cv->associated_lock != NULL);
    assert(cv->associated_lock->holder == current_thread);

    lock_release(cv->associated_lock);
    int result = thread_sleep(cv->wait_queue);
    if (result >= 0) {
        int ret = lock_acquire(cv->associated_lock);
		interrupt_set(enabled);
		return ret;
    }
	else{
		interrupt_set(enabled);
		return result;
	}
}

void
cv_signal(struct cv *cv)
{
    int enabled = interrupt_off();
    assert(cv != NULL);
    assert(cv->associated_lock != NULL);

    thread_wakeup(cv->wait_queue, 0);
    interrupt_set(enabled);
}

void
cv_broadcast(struct cv *cv)
{
    int enabled = interrupt_off();
    assert(cv != NULL);
    assert(cv->associated_lock != NULL);

    thread_wakeup(cv->wait_queue, 1);
    interrupt_set(enabled);
}