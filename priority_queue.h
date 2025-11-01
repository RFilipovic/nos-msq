#ifndef PRIORITY_QUEUE_H
#define PRIORITY_QUEUE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct my_msgbuf {
    long mtype;
    char mtext[200];
    long Tm;
    int process_num;
} Message;

typedef struct {
    Message *messages;
    int size;
    int capacity;
} PriorityQueue;

// Funkcije za rad s redom
void initQueue(PriorityQueue *pq);
void freeQueue(PriorityQueue *pq);
int isEmpty(PriorityQueue *pq);
int enqueue(PriorityQueue *pq, Message *new_msg);
int dequeue(PriorityQueue *pq, Message *result);
int peek(PriorityQueue *pq, Message *result);
int removeAt(PriorityQueue *pq, int index, Message *result);
int findByProcessNum(PriorityQueue *pq, int process_num/*, Message *result*/);
void printQueue(PriorityQueue *pq);

#endif