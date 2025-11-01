#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

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

// typedef struct my_msgbuf {
//     long mtype;
//     char mtext[200];
//     long Tm;
//     int process_num;
// } Message;

int msqid1, msqid2, msqid3, msqid4;
long Ci;
int rcvd_counter = 0;
Table *shared_table = NULL;
int shmid;
int deferred[5];
const char *RESOURCE_NAMES[] = {"duhan", "papir", "sibice"};

void retreat(int failure) {
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
  shmdt(shared_table);
  shmctl(shmid, IPC_RMID, NULL);

  exit(0);
}

void *message_receiver_thread(void *arg) {
    ThreadArgs *targs = (ThreadArgs *)arg;
    PriorityQueue *pq = targs->pq;
    int msqid = targs->msqid;
    int proc_num = targs->process_num;

    while (1) {
        Message msg;
        struct timespec sleep_time;
        sleep_time.tv_sec = 0;
        sleep_time.tv_nsec = 100000000; // 100ms wait

        // Try to receive message without blocking
        if (msgrcv(msqid, &msg, sizeof(msg) - sizeof(long), 0, IPC_NOWAIT) == -1) {
            if (errno == ENOMSG) {
                // No message available, wait a bit then continue
                nanosleep(&sleep_time, NULL);
                continue;
            }
            if (errno != EINTR) { // Ignore interrupts
                perror("msgrcv");
            }
            nanosleep(&sleep_time, NULL);
            continue;
        }

        // Update logical clock
        Ci = MAX(Ci, msg.Tm) + 1;

        // Handle different message types
        if (msg.mtype == ZAHTJEV) {
            enqueue(pq, &msg);
            printf("Pid=%d primio zahtjev(pi=%d, Tm=%ld)\n", 
                   getpid(), msg.process_num, msg.Tm);

            Message top;
            if (peek(pq, &top)) {
                if (top.process_num != proc_num) {
                    Message reply;
                    reply.mtype = ODGOVOR;
                    reply.Tm = Ci;
                    reply.process_num = top.process_num;
                    
                    int target_msqid;
                    switch (msg.process_num) {
                        case PRVI:
                            target_msqid = msqid1;
                            break;
                        case DRUGI:
                            target_msqid = msqid2;
                            break;
                        case TRECI:
                            target_msqid = msqid3;
                            break;
                        case TRGOVAC:
                            target_msqid = msqid4;
                            break;
                        default:
                            continue;
                    }
                    
                    if (msgsnd(target_msqid, &reply, sizeof(reply) - sizeof(long), IPC_NOWAIT) == -1) {
                        if (errno != EAGAIN) {
                            perror("msgsnd ODGOVOR");
                        }
                    } else {
                        printf("Pid=%d poslao odgovor pi=%d\n", 
                               getpid(), msg.process_num);
                    }
                } else {
                    deferred[msg.process_num] = 1;
                }
            }
            
        } else if (msg.mtype == ODGOVOR) {
            printf("Ja sam pid=%d i primio sam odgovor(pi=%d, Tm=%ld).\n", 
                   getpid(), msg.process_num, msg.Tm);
            rcvd_counter++;
            
        } else if (msg.mtype == IZLAZ) {
            printf("Ja sam pid=%d i primio sam izlaz(pi=%d, Tm=%ld).\n", 
                   getpid(), msg.process_num, msg.Tm);
            Message removed;
            dequeue(pq, &removed);
            
        } else {
            printf("Nepoznat broj poruke %ld\n", msg.mtype);
        }

        // Small delay after processing each message
        nanosleep(&sleep_time, NULL);
    }
    
    free(targs); // Clean up thread args
    return NULL;
}

//================================================================================================================================================================
int main() {

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

  shared_table = (Table *)shmat(shmid, NULL, 0);
  if (shared_table == (void *)-1) {
    perror("shmat failed");
    exit(1);
  }
  shared_table->occupied = 0;
  shared_table->resource[0] = -1;
  shared_table->resource[1] = -1;

  int pid = fork();

  if (pid == 0) { // msqid1

    Ci = rand() % 10;
    ThreadArgs *targs = malloc(sizeof(ThreadArgs));
    targs->pq = &pq;
    targs->msqid = msqid1;
    targs->process_num = PRVI;
    pthread_create(&thread, NULL, message_receiver_thread, targs);

    while (1) {

      // ako zahtjev nije u redu, posalji ga svima
      if (findByProcessNum(&pq, PRVI) == -1) {

        Message m;
        m.mtype = ZAHTJEV;
        m.Tm = Ci;
        m.process_num = PRVI;

        enqueue(&pq, &m);

        if (msgsnd(msqid2, &m, sizeof(m) - sizeof(long), IPC_NOWAIT) == -1)
          printf("Izlaz nije poslan.");
        else
          printf("Ja sam pid=%d i poslao sam zahtjev(pi=%d, Tm=%ld).\n",
                 getpid(), m.process_num, m.Tm);

        if (msgsnd(msqid3, &m, sizeof(m) - sizeof(long), IPC_NOWAIT) == -1)
          printf("Izlaz nije poslan.");
        else
          printf("Ja sam pid=%d i poslao sam zahtjev(pi=%d, Tm=%ld).\n",
                 getpid(), m.process_num, m.Tm);

        if (msgsnd(msqid4, &m, sizeof(m) - sizeof(long), IPC_NOWAIT) == -1)
          printf("Izlaz nije poslan.");
        else
          printf("Ja sam pid=%d i poslao sam zahtjev(pi=%d, Tm=%ld).\n",
                 getpid(), m.process_num, m.Tm);

        continue;
      }

      Message message;
      peek(&pq, &message);
      // provjeri ako je proces 1 dobio 3 odgovora i ako je prvi u prioritetnom
      // redu
      if (rcvd_counter == 3 && message.process_num == PRVI) {
        rcvd_counter = 0;
        dequeue(&pq, &message);
        // provjeri ako su na stolu duhan i papir i ako jesu udi u K.O. i
        // posalji izlaz, inace samo posalji izlaz
        if (shared_table->occupied == 1) {
          if ((shared_table->resource[0] == 0 &&
               shared_table->resource[1] == 1) ||
              (shared_table->resource[0] == 1 &&
               shared_table->resource[1] == 0)) {
            printf("\nPusac 1 (pid=%d) uzima %s i %s sa stola\n", getpid(),
                   RESOURCE_NAMES[shared_table->resource[0]],
                   RESOURCE_NAMES[shared_table->resource[1]]);
            shared_table->occupied = 0;
            printf("Pusac 1 mota i pali cigaretu\n");
            sleep(2); // Simulate smoking
            printf("Pusac 1 je popusio cigaretu\n\n");
          }
        }

        // promjeni poruku iz zahtjeva u izlaz
        message.mtype = IZLAZ;

        if (msgsnd(msqid2, &message, sizeof(message) - sizeof(long),
                   IPC_NOWAIT) == -1)
          printf("Izlaz nije poslan.");
        else
          printf("Ja sam pid=%d i poslao sam izlaz(pi=%d, Tm=%ld).\n", getpid(),
                 message.process_num, message.Tm);

        if (msgsnd(msqid3, &message, sizeof(message) - sizeof(long),
                   IPC_NOWAIT) == -1)
          printf("Izlaz nije poslan.");
        else
          printf("Ja sam pid=%d i poslao sam izlaz(pi=%d, Tm=%ld).\n", getpid(),
                 message.process_num, message.Tm);

        if (msgsnd(msqid4, &message, sizeof(message) - sizeof(long),
                   IPC_NOWAIT) == -1)
          printf("Izlaz nije poslan.");
        else
          printf("Ja sam pid=%d i poslao sam izlaz(pi=%d, Tm=%ld).\n", getpid(),
                 message.process_num, message.Tm);

        for (int i = 1; i <= 4; i++) {
          if (deferred[i]) {
            Message reply;
            reply.mtype = ODGOVOR;
            reply.Tm = Ci;
            reply.process_num = i;
            if (i == PRVI)
              continue; // msgsnd(msqid1, &reply, sizeof(reply) - sizeof(long),
                        // IPC_NOWAIT);
            if (i == DRUGI)
              msgsnd(msqid2, &reply, sizeof(reply) - sizeof(long), IPC_NOWAIT);
            else if (i == TRECI)
              msgsnd(msqid3, &reply, sizeof(reply) - sizeof(long), IPC_NOWAIT);
            else if (i == TRGOVAC)
              msgsnd(msqid4, &reply, sizeof(reply) - sizeof(long), IPC_NOWAIT);

            deferred[i] = 0;
            printf("Ja sam pid=%d i poslao sam odgodu(pi=%d).\n", getpid(), i);
          }
        }
      }
    }
  }

  pid = fork();

  if (pid == 0) { // msqid2

    Ci = rand() % 10;
    ThreadArgs *targs = malloc(sizeof(ThreadArgs));
    targs->pq = &pq;
    targs->msqid = msqid2;
    targs->process_num = DRUGI;
    pthread_create(&thread, NULL, message_receiver_thread, targs);

    while (1) {

      // ako zahtjev nije u redu, posalji ga svima
      if (findByProcessNum(&pq, DRUGI) == -1) {

        Message m;
        m.mtype = ZAHTJEV;
        m.Tm = Ci;
        m.process_num = DRUGI;

        enqueue(&pq, &m);

        if (msgsnd(msqid1, &m, sizeof(m) - sizeof(long), IPC_NOWAIT) == -1)
          printf("Izlaz nije poslan.");
        else
          printf("Ja sam pid=%d i poslao sam zahtjev(pi=%d, Tm=%ld).\n",
                 getpid(), m.process_num, m.Tm);

        if (msgsnd(msqid3, &m, sizeof(m) - sizeof(long), IPC_NOWAIT) == -1)
          printf("Izlaz nije poslan.");
        else
          printf("Ja sam pid=%d i poslao sam zahtjev(pi=%d, Tm=%ld).\n",
                 getpid(), m.process_num, m.Tm);

        if (msgsnd(msqid4, &m, sizeof(m) - sizeof(long), IPC_NOWAIT) == -1)
          printf("Izlaz nije poslan.");
        else
          printf("Ja sam pid=%d i poslao sam zahtjev(pi=%d, Tm=%ld).\n",
                 getpid(), m.process_num, m.Tm);

        continue;
      }

      Message message;
      peek(&pq, &message);
      // provjeri ako je proces 2 dobio 3 odgovora i ako je prvi u prioritetnom
      // redu
      if (rcvd_counter == 3 && message.process_num == DRUGI) {
        rcvd_counter = 0;
        dequeue(&pq, &message);
        // provjeri ako su na stolu papir i upaljac i ako jesu udi u K.O. i
        // posalji izlaz, inace samo posalji izlaz
        if (shared_table->occupied == 1) {
          if ((shared_table->resource[0] == 1 &&
               shared_table->resource[1] == 2) ||
              (shared_table->resource[0] == 2 &&
               shared_table->resource[1] == 1)) {
            printf("\nPusac 2 (pid=%d) uzima %s i %s sa stola\n", getpid(),
                   RESOURCE_NAMES[shared_table->resource[0]],
                   RESOURCE_NAMES[shared_table->resource[1]]);
            shared_table->occupied = 0;
            printf("Pusac 2 mota i pali cigaretu\n");
            sleep(2);
            printf("Pusac 2 je popusio cigaretu\n\n");
          }
        }

        // promjeni poruku iz zahtjeva u izlaz
        message.mtype = IZLAZ;

        if (msgsnd(msqid1, &message, sizeof(message) - sizeof(long),
                   IPC_NOWAIT) == -1)
          printf("Izlaz nije poslan.");
        else
          printf("Ja sam pid=%d i poslao sam izlaz(pi=%d, Tm=%ld).\n", getpid(),
                 message.process_num, message.Tm);

        if (msgsnd(msqid3, &message, sizeof(message) - sizeof(long),
                   IPC_NOWAIT) == -1)
          printf("Izlaz nije poslan.");
        else
          printf("Ja sam pid=%d i poslao sam izlaz(pi=%d, Tm=%ld).\n", getpid(),
                 message.process_num, message.Tm);

        if (msgsnd(msqid4, &message, sizeof(message) - sizeof(long),
                   IPC_NOWAIT) == -1)
          printf("Izlaz nije poslan.");
        else
          printf("Ja sam pid=%d i poslao sam izlaz(pi=%d, Tm=%ld).\n", getpid(),
                 message.process_num, message.Tm);

        for (int i = 1; i <= 4; i++) {
          if (deferred[i]) {
            Message reply;
            reply.mtype = ODGOVOR;
            reply.Tm = Ci;
            reply.process_num = i;
            if (i == PRVI)
              msgsnd(msqid1, &reply, sizeof(reply) - sizeof(long), IPC_NOWAIT);
            if (i == DRUGI)
              continue; // msgsnd(msqid2, &reply, sizeof(reply) - sizeof(long),
                        // IPC_NOWAIT);
            else if (i == TRECI)
              msgsnd(msqid3, &reply, sizeof(reply) - sizeof(long), IPC_NOWAIT);
            else if (i == TRGOVAC)
              msgsnd(msqid4, &reply, sizeof(reply) - sizeof(long), IPC_NOWAIT);

            deferred[i] = 0;
            printf("Ja sam pid=%d i poslao sam odgodu(pi=%d).\n", getpid(), i);
          }
        }
      }
    }
  }

  pid = fork();

  if (pid == 0) { // msqid3

    Ci = rand() % 10;
    ThreadArgs *targs = malloc(sizeof(ThreadArgs));
    targs->pq = &pq;
    targs->msqid = msqid3;
    targs->process_num = TRECI;
    pthread_create(&thread, NULL, message_receiver_thread, targs);

    while (1) {

      // ako zahtjev nije u redu, posalji ga svima
      if (findByProcessNum(&pq, TRECI) == -1) {

        Message m;
        m.mtype = ZAHTJEV;
        m.Tm = Ci;
        m.process_num = TRECI;

        enqueue(&pq, &m);

        if (msgsnd(msqid1, &m, sizeof(m) - sizeof(long), IPC_NOWAIT) == -1)
          printf("Izlaz nije poslan.");
        else
          printf("Ja sam pid=%d i poslao sam zahtjev(pi=%d, Tm=%ld).\n",
                 getpid(), m.process_num, m.Tm);

        if (msgsnd(msqid2, &m, sizeof(m) - sizeof(long), IPC_NOWAIT) == -1)
          printf("Izlaz nije poslan.");
        else
          printf("Ja sam pid=%d i poslao sam zahtjev(pi=%d, Tm=%ld).\n",
                 getpid(), m.process_num, m.Tm);

        if (msgsnd(msqid4, &m, sizeof(m) - sizeof(long), IPC_NOWAIT) == -1)
          printf("Izlaz nije poslan.");
        else
          printf("Ja sam pid=%d i poslao sam zahtjev(pi=%d, Tm=%ld).\n",
                 getpid(), m.process_num, m.Tm);

        continue;
      }

      Message message;
      peek(&pq, &message);
      // provjeri ako je proces 3 dobio 3 odgovora i ako je prvi u prioritetnom
      // redu
      if (rcvd_counter == 3 && message.process_num == TRECI) {
        rcvd_counter = 0;
        dequeue(&pq, &message);
        // provjeri ako su na stolu duhan i upaljac i ako jesu udi u K.O. i
        // posalji izlaz, inace samo posalji izlaz
        if (shared_table->occupied == 1) {
          if ((shared_table->resource[0] == 0 &&
               shared_table->resource[1] == 2) ||
              (shared_table->resource[0] == 2 &&
               shared_table->resource[1] == 0)) {
            printf("\nPusac 3 (pid=%d) uzima %s i %s sa stola\n", getpid(),
                   RESOURCE_NAMES[shared_table->resource[0]],
                   RESOURCE_NAMES[shared_table->resource[1]]);
            shared_table->occupied = 0;
            printf("Pusac 3 mota i pali cigaretu\n");
            sleep(2);
            printf("Pusac 3 je popusio cigaretu\n\n");
          }
        }

        // promjeni poruku iz zahtjeva u izlaz
        message.mtype = IZLAZ;

        if (msgsnd(msqid1, &message, sizeof(message) - sizeof(long),
                   IPC_NOWAIT) == -1)
          printf("Izlaz nije poslan.");
        else
          printf("Ja sam pid=%d i poslao sam izlaz(pi=%d, Tm=%ld).\n", getpid(),
                 message.process_num, message.Tm);

        if (msgsnd(msqid2, &message, sizeof(message) - sizeof(long),
                   IPC_NOWAIT) == -1)
          printf("Izlaz nije poslan.");
        else
          printf("Ja sam pid=%d i poslao sam izlaz(pi=%d, Tm=%ld).\n", getpid(),
                 message.process_num, message.Tm);

        if (msgsnd(msqid4, &message, sizeof(message) - sizeof(long),
                   IPC_NOWAIT) == -1)
          printf("Izlaz nije poslan.");
        else
          printf("Ja sam pid=%d i poslao sam izlaz(pi=%d, Tm=%ld).\n", getpid(),
                 message.process_num, message.Tm);

        for (int i = 1; i <= 4; i++) {
          if (deferred[i]) {
            Message reply;
            reply.mtype = ODGOVOR;
            reply.Tm = Ci;
            reply.process_num = i;
            if (i == PRVI)
              msgsnd(msqid1, &reply, sizeof(reply) - sizeof(long), IPC_NOWAIT);
            if (i == DRUGI)
              msgsnd(msqid2, &reply, sizeof(reply) - sizeof(long), IPC_NOWAIT);
            else if (i == TRECI)
              continue; // msgsnd(msqid3, &reply, sizeof(reply) - sizeof(long),
                        // IPC_NOWAIT);
            else if (i == TRGOVAC)
              msgsnd(msqid4, &reply, sizeof(reply) - sizeof(long), IPC_NOWAIT);

            deferred[i] = 0;
            printf("Ja sam pid=%d i poslao sam odgodu(pi=%d).\n", getpid(), i);
          }
        }
      }
    }
  }

  int resource_1, resource_2;
  Ci = rand() % 10;
  ThreadArgs *targs = malloc(sizeof(ThreadArgs));
  targs->pq = &pq;
  targs->msqid = msqid4;
  targs->process_num = TRGOVAC;
  pthread_create(&thread, NULL, message_receiver_thread, targs);

  while (1) {

    // ako zahtjev nije u redu, posalji ga svima
    if (findByProcessNum(&pq, TRGOVAC) == -1) {

      Message m;
      m.mtype = ZAHTJEV;
      m.Tm = Ci;
      m.process_num = TRGOVAC;

      enqueue(&pq, &m);

      if (msgsnd(msqid1, &m, sizeof(m) - sizeof(long), IPC_NOWAIT) == -1)
        printf("Izlaz nije poslan.");
      else
        printf("Ja sam pid=%d i poslao sam zahtjev(pi=%d, Tm=%ld).\n", getpid(),
               m.process_num, m.Tm);

      if (msgsnd(msqid2, &m, sizeof(m) - sizeof(long), IPC_NOWAIT) == -1)
        printf("Izlaz nije poslan.");
      else
        printf("Ja sam pid=%d i poslao sam zahtjev(pi=%d, Tm=%ld).\n", getpid(),
               m.process_num, m.Tm);

      if (msgsnd(msqid3, &m, sizeof(m) - sizeof(long), IPC_NOWAIT) == -1)
        printf("Izlaz nije poslan.");
      else
        printf("Ja sam pid=%d i poslao sam zahtjev(pi=%d, Tm=%ld).\n", getpid(),
               m.process_num, m.Tm);

      continue;
    }

    Message message;
    peek(&pq, &message);
    // provjeri ako je proces 4 dobio 3 odgovora i ako je prvi u
    // prioritetnom redu
    if (rcvd_counter == 3 && message.process_num == TRGOVAC) {
      rcvd_counter = 0;
      dequeue(&pq, &message);
      // provjeri ako su na stolu duhan i upaljac i ako jesu udi u K.O. i
      // posalji izlaz, inace samo posalji izlaz
      if (shared_table->occupied == 0) {
        // napuni stol
        // resource_1 = rand() % 3;
        // resource_2 = rand() % 3;

        // 0 - duhan
        // 1 - papir
        // 2 - upaljac

        printf("\nTrgovac (pid=%d) ulazi u kriticni odsjecak\n", getpid());

        // Generate two different random resources
        int res1 = rand() % 3;
        int res2;
        do {
          res2 = rand() % 3;
        } while (res2 == res1);

        shared_table->resource[0] = res1;
        shared_table->resource[1] = res2;
        shared_table->occupied = 1;

        printf("Trgovac stavlja na stol: %s i %s\n", RESOURCE_NAMES[res1],
               RESOURCE_NAMES[res2]);
      }

      // promjeni poruku iz zahtjeva u izlaz
      message.mtype = IZLAZ;

      if (msgsnd(msqid1, &message, sizeof(message) - sizeof(long),
                 IPC_NOWAIT) == -1)
        printf("Izlaz nije poslan.");
      else
        printf("Ja sam pid=%d i poslao sam izlaz(pi=%d, Tm=%ld).\n", getpid(),
               message.process_num, message.Tm);

      if (msgsnd(msqid2, &message, sizeof(message) - sizeof(long),
                 IPC_NOWAIT) == -1)
        printf("Izlaz nije poslan.");
      else
        printf("Ja sam pid=%d i poslao sam izlaz(pi=%d, Tm=%ld).\n", getpid(),
               message.process_num, message.Tm);

      if (msgsnd(msqid3, &message, sizeof(message) - sizeof(long),
                 IPC_NOWAIT) == -1)
        printf("Izlaz nije poslan.");
      else
        printf("Ja sam pid=%d i poslao sam izlaz(pi=%d, Tm=%ld).\n", getpid(),
               message.process_num, message.Tm);

      for (int i = 1; i <= 4; i++) {
        if (deferred[i]) {
          Message reply;
          reply.mtype = ODGOVOR;
          reply.Tm = Ci;
          reply.process_num = i;
          if (i == PRVI)
            msgsnd(msqid1, &reply, sizeof(reply) - sizeof(long), IPC_NOWAIT);
          if (i == DRUGI)
            msgsnd(msqid2, &reply, sizeof(reply) - sizeof(long), IPC_NOWAIT);
          else if (i == TRECI)
            msgsnd(msqid3, &reply, sizeof(reply) - sizeof(long), IPC_NOWAIT);
          else if (i == TRGOVAC)
            continue; // msgsnd(msqid4, &reply, sizeof(reply) - sizeof(long),
                      // IPC_NOWAIT);

          deferred[i] = 0;
          printf("Ja sam pid=%d i poslao sam odgodu(pi=%d).\n", getpid(), i);
        }
      }
      sleep(3);
    }
  }

  return 0;
}
