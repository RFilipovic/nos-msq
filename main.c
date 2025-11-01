#include <signal.h>
#include <stdio.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include "priority_queue.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define ZAHTJEV 1
#define ODGOVOR 2
#define IZLAZ 3

#define PRVI 1
#define DRUGI 2
#define TRECI 3
#define TRGOVAC 4


typedef struct {
    int resource[2];
    int occupied;
} Table;

typedef struct {
    PriorityQueue *pq;
    int msqid;
    int process_num;
} ThreadArgs;

//typedef struct my_msgbuf {
//    long mtype;
//    char mtext[200];
//    long Tm;
//    int process_num;
//} Message;

int msqid1, msqid2, msqid3, msqid4;
long Ci;
int rcvd_counter = 0;
Table *shared_table = NULL;
int shmid;
int deferred [5];

void retreat(int failure) 
{
    if (msgctl(msqid1, IPC_RMID, NULL) == -1) {
        perror("msgctl");
        exit(1);
    }
    if (msgctl(msqid2, IPC_RMID, NULL) == -1) {
        perror("msgctl");
        exit(1);
    }
    if (msgctl(msqid3, IPC_RMID, NULL) == -1) {
        perror("msgctl");
        exit(1);
    }
    if (msgctl(msqid4, IPC_RMID, NULL) == -1) {
        perror("msgctl");
        exit(1);
    }
    exit(0);
}

void *message_receiver_thread(void* arg) {

    ThreadArgs *targs = (ThreadArgs *)arg;
    PriorityQueue *pq = targs->pq;
    int msqid = targs->msqid;
    int proc_num = targs->process_num;
    
    while (1) {
        Message msg;
        
        if (msgrcv(msqid, &msg, sizeof(msg) - sizeof(long), 0, IPC_NOWAIT) == -1) {
            perror("msgrcv");
            continue;
        }

        Ci = MAX(Ci, msg.Tm) + 1;
        
        if (msg.mtype == ZAHTJEV) {
            enqueue(pq, &msg);
            printf("Pid=%d primio zahtjev(pi=%d, Tm=%ld)\n", getpid(), msg.process_num, msg.Tm);

            Message top;
            peek(pq, &top);

            if (top.process_num != proc_num) {
                Message reply;
                reply.mtype = ODGOVOR;
                reply.Tm = Ci;
                reply.process_num = top.process_num;
                int target_msqid;
                switch (msg.process_num) {
                    case PRVI: target_msqid = msqid1; break;
                    case DRUGI: target_msqid = msqid2; break;
                    case TRECI: target_msqid = msqid3; break;
                }
                if (msgsnd(target_msqid, &reply, sizeof(reply) - sizeof(long), IPC_NOWAIT) == -1)
                    perror("msgsnd ODGOVOR");
                else
                    printf("Pid=%d poslao odgovor pi=%d\n", getpid(), msg.process_num);
            } else {
                deferred[msg.process_num] = 1;
            }
        }
        else if (msg.mtype == ODGOVOR) {
            printf("Ja sam pid=%d i primio sam odgovor(pi=%d, Tm=%ld).\n", getpid(), msg.process_num, msg.Tm);
            rcvd_counter++;
        }
        else if (msg.mtype == IZLAZ) {
            printf("Ja sam pid=%d i primio sam izlaz(pi=%d, Tm=%ld).\n", getpid(), msg.process_num, msg.Tm);
            Message removed;
            dequeue(pq, &removed);
        }
        else {
            printf("Nepoznat broj poruke %ld\n", msg.mtype);
        }
    }
    return NULL;
}

//================================================================================================================================================================
int main(){

    msqid1 = msgget(12345, 0666 | IPC_CREAT);
    msqid2 = msgget(23456, 0666 | IPC_CREAT);
    msqid3 = msgget(34567, 0666 | IPC_CREAT);
    msqid4 = msgget(45678, 0666 | IPC_CREAT);

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

    if(pid == 0){ //msqid1

        Ci = rand() % 10;
        ThreadArgs *targs = malloc(sizeof(ThreadArgs));
        targs->pq = &pq;
        targs->msqid = msqid1;
        targs->process_num = PRVI;
        Table *shared_table = (Table *) shmat(shmid, NULL, 0);
        pthread_create(&thread, NULL, message_receiver_thread, targs);

        while (1) {

            //ako zahtjev nije u redu, posalji ga svima
            if(findByProcessNum(&pq, PRVI) == -1) {

                Message m;
                m.mtype = ZAHTJEV;
                m.Tm = Ci;
                m.process_num = PRVI;

                enqueue(&pq, &m);

                if (msgsnd(msqid2, &m, sizeof(m) - sizeof(long), IPC_NOWAIT) == -1)
                    printf("Izlaz nije poslan.");
                else
                    printf("Ja sam pid=%d i poslao sam zahtjev(pi=%d, Tm=%ld).\n", getpid(), m.process_num, m.Tm);

                if (msgsnd(msqid3, &m, sizeof(m) - sizeof(long), IPC_NOWAIT) == -1)
                    printf("Izlaz nije poslan.");
                else
                    printf("Ja sam pid=%d i poslao sam zahtjev(pi=%d, Tm=%ld).\n", getpid(), m.process_num, m.Tm);

                if (msgsnd(msqid4, &m, sizeof(m) - sizeof(long), IPC_NOWAIT) == -1)
                    printf("Izlaz nije poslan.");
                else
                    printf("Ja sam pid=%d i poslao sam zahtjev(pi=%d, Tm=%ld).\n", getpid(), m.process_num, m.Tm);

                continue;
            }

            Message message;
            peek(&pq, &message);
            //provjeri ako je proces 1 dobio 3 odgovora i ako je prvi u prioritetnom redu
            if(rcvd_counter == 3 && message.process_num == PRVI) {
                rcvd_counter = 0;
                dequeue(&pq, &message);
                //provjeri ako su na stolu duhan i papir i ako jesu udi u K.O. i posalji izlaz, inace samo posalji izlaz
                if(shared_table->occupied == 1 &&
                    (shared_table->resource[0] == 0 && shared_table->resource[1] == 1 ||
                    shared_table->resource[0] == 1 && shared_table->resource[1] == 0)) {
                    printf("Ja sam pid=%d i ulazim u kriticni odsjecak (duhan i papir).\n", getpid());
                    //isprazni stol
                    shared_table->occupied = 0;
                }

                //promjeni poruku iz zahtjeva u izlaz
                message.mtype = IZLAZ;

                if (msgsnd(msqid2, &message, sizeof(message) - sizeof(long), IPC_NOWAIT) == -1)
                    printf("Izlaz nije poslan.");
                else
                    printf("Ja sam pid=%d i poslao sam izlaz(pi=%d, Tm=%ld).\n", getpid(), message.process_num, message.Tm);

                if (msgsnd(msqid3, &message, sizeof(message) - sizeof(long), IPC_NOWAIT) == -1)
                    printf("Izlaz nije poslan.");
                else
                    printf("Ja sam pid=%d i poslao sam izlaz(pi=%d, Tm=%ld).\n", getpid(), message.process_num, message.Tm);

                if (msgsnd(msqid4, &message, sizeof(message) - sizeof(long), IPC_NOWAIT) == -1)
                    printf("Izlaz nije poslan.");
                else
                    printf("Ja sam pid=%d i poslao sam izlaz(pi=%d, Tm=%ld).\n", getpid(), message.process_num, message.Tm);


                for (int i = 1; i <= 4; i++) { 
                    if (deferred[i]) {
                        Message reply;
                        reply.mtype = ODGOVOR;
                        reply.Tm = Ci;
                        reply.process_num = i; 
                        if (i == PRVI) continue; //msgsnd(msqid1, &reply, sizeof(reply) - sizeof(long), IPC_NOWAIT);
                        if (i == DRUGI) msgsnd(msqid2, &reply, sizeof(reply) - sizeof(long), IPC_NOWAIT);
                        else if (i == TRECI) msgsnd(msqid3, &reply, sizeof(reply) - sizeof(long), IPC_NOWAIT);
                        else if (i == TRGOVAC) msgsnd(msqid4, &reply, sizeof(reply) - sizeof(long), IPC_NOWAIT);

                        deferred[i] = 0; 
                        printf("Ja sam pid=%d i poslao sam odgodu(pi=%d).\n", getpid(), i);
                    }
                }
            }
        }
    }

    pid = fork();

    if(pid == 0){ //msqid2
/*
        Ci = rand() % 10;
        ThreadArgs *targs = malloc(sizeof(ThreadArgs));
        targs->pq = &pq;
        targs->msqid = msqid2;
        Table *shared_table = (Table *) shmat(shmid, NULL, 0);
        pthread_create(&thread, NULL, message_receiver_thread, targs);

        while (1) {
            
            //ako zahtjev nije u redu, posalji ga svima
            if(findByProcessNum(&pq, 1) == -1) {

                Message m;
                m.mtype = ZAHTJEV;
                m.Tm = Ci;
                m.process_num = 2;

                if (msgsnd(msqid1, &m, sizeof(m) - sizeof(long), IPC_NOWAIT) == -1)
                    printf("Izlaz nije poslan.");
                if (msgsnd(msqid3, &m, sizeof(m) - sizeof(long), IPC_NOWAIT) == -1)
                    printf("Izlaz nije poslan.");
                if (msgsnd(msqid4, &m, sizeof(m) - sizeof(long), IPC_NOWAIT) == -1)
                    printf("Izlaz nije poslan.");
            }

            Message message;
            peek(&pq, &message);
            //provjeri ako je proces 1 dobio 3 odgovora i ako je prvi u prioritetnom redu
            if(rcvd_counter == 3 && message.process_num == 1) {
                rcvd_counter = 0;
                dequeue(&pq, &message);
                //provjeri ako su na stolu duhan i papir i ako jesu udi u K.O. i posalji izlaz, inace samo posalji izlaz
                if(shared_table->occupied == 1 &&
                    (shared_table->resource[0] == 0 && shared_table->resource[1] == 1 ||
                    shared_table->resource[0] == 1 && shared_table->resource[1] == 0)) {
                    printf("Ja sam pid=%d i ulazim u kriticni odsjecak (duhan i papir).\n", getpid());
                    //isprazni stol
                    shared_table->occupied = 0;
                }

                //promjeni poruku iz zahtjeva u izlaz
                message.mtype = IZLAZ;

                if (msgsnd(msqid2, &message, sizeof(message) - sizeof(long), IPC_NOWAIT) == -1)
                    printf("Izlaz nije poslan.");
                if (msgsnd(msqid3, &message, sizeof(message) - sizeof(long), IPC_NOWAIT) == -1)
                    printf("Izlaz nije poslan.");
                if (msgsnd(msqid4, &message, sizeof(message) - sizeof(long), IPC_NOWAIT) == -1)
                    printf("Izlaz nije poslan.");
            }
        }
            */
    }

    pid = fork();

    if(pid == 0){
        Ci = rand() % 10;
        pthread_create(&thread, NULL, message_receiver_thread, &pq);
        while (1) {
            //do child stuff forever
        }
    }

    int resource_1, resource_2;
    Ci = rand() % 10;
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
