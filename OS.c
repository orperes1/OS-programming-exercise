#define INTER_MEM_ACCS_T    10000000
#define MEM_WR_T            300000
#define WS                  2
#define N                   3
#define WR_RATE             0.5
#define SIM_TIME            1
#define HD_ACCS_T           2500
#define P_ID1               0
#define P_ID2               1
#define MMU_ID              2
#define HD_ID               3
#define NUM_OF_PROCSS       4
#define TRUE                1
#define ACK                 3
#define WRITE               1
#define HD                  4
#define MISS               -1
#define LOCKED              1
#define UN_LOCKED           0
#define W_OR_R              1

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <syscall.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <mqueue.h>
#include <signal.h>
#include <pthread.h>
#include <memory.h>
#include <errno.h>
#include <unistd.h>



/* In order to simulate Physical Memory */
typedef struct phy_Table{
    int phy_Table[N];
    int index, pCounter;
}phy_Table;

/* How msg looks like */
typedef struct request{
    long int type;   /* 1 - Read or Write, 3 - ACK, 4 - Write to HD */
    int action; /* 1 - Write, 2 - Read */
    int processId;
    int data;
}msg;


/* Function Declaration */
void* print_mem(void *pvoid); /* print the maemory state */
void* handleMsg(void *pvoid); /* handles the message */
int isExist(msg *m); /* returns the page */
void* evicter(void *pvoid); /* doing stuff */
int receive_Msg(int myPid, msg *m,long int type); /* receive  the msg. if fail -1 */
int send_Msg(int sendToPid, msg *m); /* sends the msg. if fail -1 */
int readOrWrite();  /* returns 0 - Write / 1 - Read */
int pageSelector(); /* generates number with uniform distribution */
void terminate(); /* terminates everything */
void process(int myPid); /* runs each process 1/2 */
void mmuProcess(); /* runs MMU process */
void hd();


/* Global variables */
int pageTable[2*WS], msqid[NUM_OF_PROCSS], pid[NUM_OF_PROCSS], pTMutex = UN_LOCKED, pMMutex = UN_LOCKED;
phy_Table physicalMem;
pthread_t msgHandler, printer, evcitert;


int main() {
    int i;
    struct timespec suspend;
    time_t initialTime;
    srand(time(NULL));
    initialTime = time(NULL);
    suspend.tv_nsec = 0.5*INTER_MEM_ACCS_T;
    suspend.tv_sec = 0;
    /* Initialize memory */
    for (i = 0; i < 2*WS; i++){
        if (i < N)
            physicalMem.phy_Table[i] = 0;
        pageTable[i] = -1;
    }
    physicalMem.index = 0;
    physicalMem.pCounter = 0;

    /* Creates queue for each process */
    for (i=0; i < NUM_OF_PROCSS; i++)
        msqid[i] = msgget(IPC_PRIVATE, 0600|IPC_CREAT);

    /* Create processes */
    for (i = 0; i < NUM_OF_PROCSS; i++){
        if ((pid[i] = fork()) == -1){
            terminate();
            perror(strerror(errno));
        }
    /* Child processes will run the relevant function */
        else if (!pid[i]){
            switch (i){
                case P_ID1:
                    process(P_ID1);
                    break;
                case P_ID2:
                    nanosleep(&suspend, NULL);
                    process(P_ID2);
                    break;
                case MMU_ID:
                    mmuProcess();
                    break;
                case HD_ID:
                    hd();
                    break;
                default:
                    break;
            }
        }
    }

    while((time(NULL)-initialTime) < SIM_TIME);
    printf("Successfully finished sim\n");
    terminate();
    return 0;
}

int readOrWrite() {
    return rand()%10 <= (WR_RATE*10);
}

int pageSelector() {
    return rand() % WS;
}

int send_Msg(int sendToPid, msg *m){
    return msgsnd(msqid[sendToPid],m,(sizeof(msg)-sizeof(long)),0);
}

int receive_Msg(int myPid,msg *m,long int type) {
    return (int)msgrcv(msqid[myPid],m,(sizeof(msg)-sizeof(long)),type,0);
}

void terminate() {
    int i;

    /* Wait for all the open threads to finish */
    pthread_join(printer,NULL);
    pthread_join(evcitert,NULL);
    pthread_join(msgHandler,NULL);

    /* Kills all processes */
    for (i = 0; i < NUM_OF_PROCSS; i++)
        kill(pid[i],SIGKILL);
    pMMutex = UN_LOCKED;
    pTMutex = UN_LOCKED;
}

void process(int myPid) {
        msg m;
        struct timespec suspend;

        suspend.tv_sec = 0;
        suspend.tv_nsec = INTER_MEM_ACCS_T;

    while (TRUE){
        nanosleep(&suspend, NULL); /* sleeps for nanosecs */

        /* Create msg */
        m.type = W_OR_R;
        m.action = readOrWrite();
        m.data = pageSelector();
        m.processId = myPid;

        /* Sending msg and wait for further action*/
        if(send_Msg(MMU_ID,&m) < 0) {
            terminate();
            perror(strerror(errno));
        }

        /* Waiting for ACK */
        if(receive_Msg(myPid,&m,ACK) < 0){
            terminate();
            perror(strerror(errno));
        }
    }
}

void mmuProcess() {
    while(TRUE) {
        pthread_create(&msgHandler, NULL, handleMsg, NULL);
        pthread_join(msgHandler,NULL);
    }
}

void* print_mem(void *pvoid) {
    int i, pTable[2*WS], mTable[N];

    /* Copy current memory state safely using old school mutex */
    while (pTMutex == LOCKED);
    pTMutex = LOCKED;
    for (i = 0; i < 2*WS; i++)
        pTable[i] = pageTable[i];
    pTMutex = UN_LOCKED;

    while (pMMutex == LOCKED);
    pTMutex = LOCKED;
    for (i = 0; i < N; i++)
        mTable[i] = physicalMem.phy_Table[i];
    pTMutex = UN_LOCKED;

    printf("Page table\n");
    for (i = 0; i < 2*WS; i++)
        printf("%d|%d\n", i, pTable[i]);

    printf("\nMemory\n");
    for (i = 0; i < N; i++)
        printf("%d|%d\n", i, mTable[i]);
    printf("\n");
    pthread_exit(0);
}

void *handleMsg(void *pvoid) {
    msg mR, mS;
    int phyAdd, index, sendingProcess;
    struct timespec suspend;

    /* Receives the msg */
    if(receive_Msg(MMU_ID,&mR,W_OR_R) < 0){
        terminate();
        perror(strerror(errno));
    }
    sendingProcess = mR.processId; /* To remember which process sent the request */

    /* MISS */
    if ((phyAdd = isExist(&mR)) == MISS){
        while (pMMutex == LOCKED);
        pMMutex = LOCKED;

        /* Memory !Full */
        if (physicalMem.pCounter < N) {
            pMMutex = UN_LOCKED;

            /* Create msg for HD process and send it, after that pause */
            mS.type = HD;
            mS.processId = MMU_ID;
            mS.data = mR.data;
            if(send_Msg(HD_ID,&mS) < 0) {
                terminate();
                perror(strerror(errno));
            }

            /* After we wake up, receive msg, print memory and break */
            if(receive_Msg(MMU_ID,&mR,ACK) < 0){
                terminate();
                perror(strerror(errno));
            }

            if (mR.type == ACK) {
                /* Update physical */
                while (pMMutex == LOCKED);
                pMMutex = LOCKED;
                phyAdd = physicalMem.pCounter;
                physicalMem.pCounter++;
                physicalMem.phy_Table[phyAdd] = !mR.action;
                pMMutex = UN_LOCKED;

                while (pTMutex == LOCKED);
                pTMutex = LOCKED;
                pageTable[mR.data] = phyAdd;
                pTMutex = UN_LOCKED;
            }
        }
        else {
            pMMutex = UN_LOCKED;
            /* Evicter thread and stuff */
            pthread_create(&evcitert, NULL, &evicter, NULL);
            pthread_join(evcitert, NULL);

            /* Create msg for HD process and send it, after that pause */
            mS.type = HD;
            mS.processId = MMU_ID;
            mS.data = mR.data;
            if(send_Msg(HD_ID,&mS) < 0) {
                terminate();
                perror(strerror(errno));
            }

            /* After we wake up, receive msg, print memory and break */
            if(receive_Msg(MMU_ID,&mR,ACK) < 0){
                terminate();
                perror(strerror(errno));
            }
            if (mR.type == ACK) {
                /* After ACK, updating physical memory and page table */
                while (pMMutex == LOCKED);
                pMMutex = LOCKED;
                index = physicalMem.index;
                if (!index)
                    index = N;
                physicalMem.phy_Table[index - 1] = !mR.action;
                pMMutex = UN_LOCKED;

                while (pTMutex == LOCKED);
                pTMutex = LOCKED;
                pageTable[mR.data] = (index - 1);
                pTMutex = UN_LOCKED;
            }
        }
        pthread_create(&printer,NULL,(void*) &print_mem,NULL);
        pthread_join(printer, NULL);
    }

    /* WRITE whether hit or miss */
    if (mR.action == WRITE){
        suspend.tv_nsec = MEM_WR_T;
        suspend.tv_sec = 0;
        nanosleep(&suspend, NULL); /* sleeps for nanosecs */

        while (pMMutex == LOCKED);
        pMMutex = LOCKED;
        physicalMem.phy_Table[phyAdd] = TRUE;
        pMMutex = UN_LOCKED;
    }

    /* Sending ACK */
    mS.processId = MMU_ID;
    mS.type = ACK;
    if(send_Msg(sendingProcess,&mS) < 0) {
        terminate();
        perror(strerror(errno));
    }
    pthread_exit(0);
}

int isExist(msg *m) {
    switch (m->processId){
        case P_ID1:
            return pageTable[m->data];
        case P_ID2:
            m->data += WS;
            return pageTable[m->data];
        default:
            return -1;
    }
}

void *evicter(void *pvoid) {
    int index,i,dirty = 0;
    msg m;

    /* finding the next index where we can write (FIFO clock) */
    while (pMMutex == LOCKED);
    pMMutex = LOCKED;
    index = physicalMem.index;
    if (physicalMem.phy_Table[index])
        dirty = TRUE;
    if (index == N-1)
        physicalMem.index = 0;
    else
        physicalMem.index++;
    pMMutex = UN_LOCKED;

    /* Update page table */
    while(pTMutex == LOCKED);
    pTMutex = LOCKED;
    for (i = 0; i < 2*WS; i++){
        if (pageTable[i] == index) {
            m.data = i;
            pageTable[i] = MISS;
            break;
        }
    }
    pTMutex = UN_LOCKED;
    /* If dirty, inform HD */
    if (dirty){
        m.processId = MMU_ID;
        m.type = HD;
        if(send_Msg(HD_ID,&m) < 0) {
            terminate();
            perror(strerror(errno));
        }

        /* After we wake up, receive msg, print memory and break */
        if(receive_Msg(MMU_ID,&m,ACK) < 0){
            terminate();
            perror(strerror(errno));
        }
    }
    pthread_exit(0);
}

void hd() {
    msg m;
    struct timespec suspend;

    while(TRUE){
        if(receive_Msg(HD_ID,&m,HD) < 0){
            terminate();
            perror(strerror(errno));
        }

        suspend.tv_nsec = HD_ACCS_T;
        suspend.tv_sec = 0;
        nanosleep(&suspend,NULL); /* sleeps for nanosecs */

        m.type = ACK;
        m.processId = HD_ID;

        if(send_Msg(MMU_ID,&m) < 0) {
            terminate();
            perror(strerror(errno));
        }
    }
}
