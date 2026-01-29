/*
 * queue.c
 *
 * Definition of the queue structure and implemenation of its API functions.
 *
 */

#include "queue.h"
#include <stdlib.h>
#include <assert.h>

struct _fifo_queue {
    int size;
    int capacity;
    node_item_t *head;
    node_item_t *tail;
    node_item_t **owner;
};

void node_init(node_item_t * node, int id)
{
    node->id = id;
    node->in_queue = false;
}

bool node_in_queue(node_item_t * node)
{
    return node->in_queue;
}

fifo_queue_t * queue_create(unsigned capacity)
{
    if(capacity <= 0){
        return NULL;
    }
    fifo_queue_t *queue = malloc(sizeof(fifo_queue_t));
    if (!queue) {
        return NULL;
    }
    queue->capacity = capacity;
    queue->size = 0;
    queue->head = NULL;
    queue->tail = NULL;
    queue->owner = NULL;
    return queue;
}

void queue_destroy(fifo_queue_t * queue)
{
    assert(queue->size == 0); // Ensure the queue is empty
    assert(queue->head == NULL); // Ensure the head is NULL
    assert(queue->tail == NULL); // Ensure the tail is NULL
    free(queue);
}

node_item_t * queue_pop(fifo_queue_t * queue)
{
    if(queue->size == 0){
        return NULL;
    }
    if(queue->size == 1){
        node_item_t *head = queue->head;
        queue->head = NULL;
        queue->tail = NULL;
        queue->size -= 1;
        head->in_queue = false;
        head->prev = NULL;
        head->next = NULL;
        return head;
    }
    else{
        node_item_t *head = queue->head;
        queue->head = queue->head->next; // head is now shifted one to the right
        queue->size -= 1; // size decreases
        head->in_queue = false; // toggle information about the dumped head
        head->prev = NULL;
        head->next = NULL;
        return head;
    }
}

node_item_t * queue_top(fifo_queue_t * queue)
{
    if(queue->size == 0){
        return NULL;
    }
    return queue->head;
}

int queue_push(fifo_queue_t * queue, node_item_t * node)
{
    // make sure we aren't enqueuing a node that already belongs to another queue.
    assert(!node_in_queue(node));
    
    assert(!node_in_queue(node));

    if (queue->size == queue->capacity) {
        return -1;
    }
    node_item_t *tail = queue->tail;
    if (tail != NULL) {
        tail->next = node;
        node->prev = tail;
        queue->size += 1;
        queue->tail = node;
        node->in_queue = true;
        return 0;
    }
    else{
        queue->head = node;
        queue->tail = node;
        queue->size += 1;
        node->in_queue = true;
        return 0;
    }

    return -1;
}

node_item_t * queue_remove(fifo_queue_t * queue, int id)
{
    if (queue->size == 0) {
        return NULL;
    }
    else{
        int index = 0;
        node_item_t *curr = queue->head;
        while(curr != NULL){
            if (curr->id == id) {
                if (queue->size == 1){
                    queue->head = NULL;
                    queue->tail = NULL;
                }
                else if (index == 0){
                    queue->head = curr->next;
                }
                else if(index == queue->size - 1){
                    queue->tail = curr->prev;
                }
                else{
                    curr->prev->next = curr->next;
                    curr->next->prev = curr->prev;
                }
                queue->size -= 1;
                curr->in_queue = false;
                curr->next = NULL;
                curr->prev = NULL;
                return curr;
            }
            curr = curr->next;
            index += 1;
        }
    }
    return NULL;
}


int 
queue_count(fifo_queue_t * queue)
{
    return queue->size;
}

void * queue_get_owner(fifo_queue_t * queue){
    if (queue == NULL){
        return NULL;
    }
    if(queue->owner == NULL){
        return NULL;
    }
    if(*(queue->owner) == NULL){
        return NULL;
    }
    return *(queue->owner);
}

void queue_set_owner(fifo_queue_t * queue, void *owner){
    if (owner == NULL){
        queue->owner = NULL;
    }
    if(*(struct thread **)owner == NULL){
        queue->owner = NULL;
    }
    queue->owner = (struct thread **)owner;
}