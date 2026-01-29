/*
 * fcfs.c
 *
 * Implementation of a first-come first-served scheduler.
 * Becomes round-robin once preemption is enabled.
 */

#include "ut369.h"
#include "queue.h"
#include "thread.h"
#include "schedule.h"
#include <stdlib.h>
#include <assert.h>

static struct _fifo_queue *head = NULL;

int 
fcfs_init(void)
{
    head = queue_create(THREAD_MAX_THREADS);
    if(head != NULL) {
        return 0;
    }
    else {
        return THREAD_NOMEMORY;
    }
}

int
fcfs_enqueue(struct thread * thread)
{
    if (queue_push(head, thread) == 0){
        return 0;
    }
    return THREAD_NOMORE;
}

struct thread *
fcfs_dequeue(void)
{
    return queue_pop(head);
}

struct thread *
fcfs_remove(Tid tid)
{
    return queue_remove(head, tid);
}

void
fcfs_destroy(void)
{
    queue_destroy(head);
    head = NULL;
}