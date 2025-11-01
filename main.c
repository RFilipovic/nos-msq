#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include "priority_queue.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))

typedef struct {
    int resource[2];
    int occupied;
} Table;

int msqid;
long Ci;
int rcvd_counter = 0;
Table *shared_table = NULL;
int shmid;

void retreat(int failure) 
{
    if (msgctl(msqid, IPC_RMID, NULL) == -1) {
        perror("msgctl");
        exit(1);
    }
    exit(0);
}

void *message_receiver_thread(void* arg) {
    PriorityQueue *pq = (PriorityQueue *)arg;
    
    printf("Thread started\n");
    
    while (1) {
        Message msg;
        
        if (msgrcv(msqid, &msg, sizeof(msg) - sizeof(long), 1, IPC_NOWAIT) == -1) {
            perror("msgrcv");
            continue;
        }
        
        if (msg.mtype == 0) {
            enqueue(pq, &msg);
            printf("Received REQUEST from process %d, Tm=%ld\n", msg.process_num, msg.Tm);
        }
        else if (msg.mtype == 1) {
            printf("Received REPLY from process %d, Tm=%ld\n", msg.process_num, msg.Tm);
            rcvd_counter++;
        }
        else if (msg.mtype == 2) {
            printf("Received RELEASE from process %d, Tm=%ld\n", msg.process_num, msg.Tm);
            Message removed;
            findByProcessNum(pq, msg.process_num, &removed);  // or removeAt if you track index
        }
        else {
            printf("Received UNKNOWN message type %ld\n", msg.mtype);
        }

        Ci = MAX(Ci, msg.Tm) + 1;
    }
    return NULL;
}

int main(){

    PriorityQueue pq;
    initQueue(&pq);
    pthread_t thread;
    srand(time(NULL));
    signal(SIGINT, retreat);

    shmid = shmget(56789, sizeof(Table), 0666 | IPC_CREAT);
    if (shmid == -1) {
        perror("shmget failed");
        exit(1);
    }

    int pid = fork();

    if(pid == 0){

        Ci = rand() % 10;
        msqid = msgget(12345, 0666 | IPC_CREAT);
        pthread_create(&thread, NULL, message_receiver_thread, &pq);
        Table *shared_table = (Table *) shmat(shmid, NULL, 0);

        while (1) {
            Message message;
            peek(&pq, &message);
            //provjeri ako je proces 1 dobio 3 odgovora i ako je prvi u prioritetnom redu
            if(rcvd_counter == 3 && message.process_num == 1) {
                rcvd_counter = 0;
                //dopusti dolazak na stol
                //posalji poruku redu od trgovca da trazis resurse recimo 0 i 1
                //ako su potrebne stvari na stolu udi u K.O i uzmi ih
                //javi drugima da si otisao sa stola
            }
        }
    }

    pid = fork();

    if(pid == 0){
        Ci = rand() % 10;
        msqid = msgget(23456, 0666 | IPC_CREAT);
        pthread_create(&thread, NULL, message_receiver_thread, &pq);
        while (1) {
            //do child stuff forever
        }
    }

    pid = fork();

    if(pid == 0){
        Ci = rand() % 10;
        msqid = msgget(34567, 0666 | IPC_CREAT);
        pthread_create(&thread, NULL, message_receiver_thread, &pq);
        while (1) {
            //do child stuff forever
        }
    }

    int resource_1, resource_2;
    Ci = rand() % 10;
    msqid = msgget(45678, 0666 | IPC_CREAT);
    pthread_create(&thread, NULL, message_receiver_thread, &pq);
    
    while (1) {
        resource_1 = rand() % 3;
        resource_2 = rand() % 3;

        //0 - duhan
        //1 - papir
        //2 - upaljac

        if(resource_1 == resource_2) continue;

        if(resource_1 == 0 && resource_2 == 1){
            //pusac 1. (duhan i papir)

        }else if(resource_1 == 1 && resource_2 == 2){
            //pusac 2. (papir i upaljac)

        }else if(resource_1 == 0 && resource_2 == 2){
            //pusac 3. (duhan i upaljac)

        } 
    }

    return 0;
}
