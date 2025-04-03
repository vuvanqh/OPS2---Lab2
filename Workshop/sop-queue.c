#define _GNU_SOURCE
#include <asm-generic/errno.h>
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_MSG_COUNT 4
#define MAX_ITEMS 8
#define MIN_ITEMS 2
#define SHOP_QUEUE_NAME "/shop"
#define MSG_SIZE 128
#define TIMEOUT 2
#define CLIENT_COUNT 8
#define OPEN_FOR 8
#define START_TIME 8
#define MAX_AMOUNT 16

static const char* const TEXT[] = {"Come back tomorrow", "Out of stock", "There is an item in the packing zone that shouldn't be there"}; //3
static const char* const UNITS[] = {"kg", "l", "dkg", "g"}; //4
static const char* const PRODUCTS[] = {"mięsa", "śledzi", "octu", "wódki stołowej", "żelatyny"};//5

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

void msleep(unsigned int milisec)
{
    time_t sec = (int)(milisec / 1000);
    milisec = milisec - (sec * 1000);
    struct timespec req = {0};
    req.tv_sec = sec;
    req.tv_nsec = milisec * 1000000L;
    if (nanosleep(&req, &req))
        ERR("nanosleep");
}

void child_work()
{
    srand(getpid());
    mqd_t mq;
    if((mq=mq_open(SHOP_QUEUE_NAME, O_RDWR))==(mqd_t)-1)
        ERR("mq_open");
    
    int n = MIN_ITEMS + rand()%(MAX_ITEMS+1);
    for(int i=0;i<n;i++)
    {
        const char* u = UNITS[rand()%4];
        const char* p = PRODUCTS[rand()%5];
        int a = 1+rand()%MAX_AMOUNT;
        unsigned P = rand()%2;
        char message[MSG_SIZE];
        snprintf(message,MSG_SIZE,"%c%c %d",*u,*p,a);
        struct timespec tr;
        if(clock_gettime(CLOCK_REALTIME,&tr)==-1)
            ERR("clock_gettime");
        tr.tv_sec+2;
        if(TEMP_FAILURE_RETRY(mq_timedsend(mq,message,MSG_SIZE,P,&tr))<0) 
        {
            if(errno==ETIMEDOUT) 
            {
                printf("I will never come here again...\n");
                break;
            }
            ERR("mq_send");
        }
        msleep(1000);
    }
    mq_close(mq);
}

void handle_messages(union sigval data);
void register_notification(mqd_t* mq)
{
    struct sigevent notification = {};
    notification.sigev_value.sival_ptr = mq;
    notification.sigev_notify = SIGEV_THREAD;
    notification.sigev_notify_function = handle_messages;

    int res = mq_notify(*mq, &notification);
    if (res == -1)
        ERR("mq_notify");
}

void handle_messages(union sigval data)
{
    mqd_t* mq = data.sival_ptr;
    char message[MSG_SIZE];

    register_notification(mq);

    for (;;)
    {
        char msg[MSG_SIZE];
        unsigned prio;

        if(mq_receive(*mq,msg,MSG_SIZE,&prio)<0)
        {
            if(errno==EAGAIN || errno==EBADF) break;
            ERR("mq_receive");
        }
        switch(prio)
        {
            case 0:
                printf("%s\n",TEXT[rand()%3]);
                break;
            default:
                printf("Please go to the end of the line\n");
                break;
        }
        msleep(100);
    }
}

void checkout_work()
{
    srand(getpid());
    mqd_t mq;
    if((mq=mq_open(SHOP_QUEUE_NAME, O_RDWR | O_NONBLOCK))==(mqd_t)-1)
        ERR("mq_open");
    if((rand()%4)%4==0)
    {
        printf("Closed today\n");
        mq_close(mq);
        exit(0);
    }
    else
    {
        struct sigevent notif = 
        {
            .sigev_value.sival_ptr = &mq,
            .sigev_notify = SIGEV_THREAD,
            .sigev_notify_function = handle_messages,
        };
        if(mq_notify(mq, &notif)<0) 
            ERR("mq_notify");

        printf("Open Today\n\n");
        for(int i=0;i<OPEN_FOR;i++)
        {
            printf("%d:00\n",START_TIME+i);

            msleep(200);
        }
        mq_close(mq);
        printf("Store is closing...\n");
        sleep(1);
    }
}

void create_children()
{
    if(fork()==0)
    {
        checkout_work();
        exit(0);
    }

    for(int i=0;i<CLIENT_COUNT;i++)
    {
        switch(fork())
        {
            case 0: 
                child_work();
                exit(0);
            case -1: ERR("fork");
        }
        msleep(200);
    }
}
/*
CLIENT_COUNT+1 processes 
CLIENT_COUNT = customers
1 - self-serveice (first) - wait 200ms then the rest
*/
int main(void)
{
    mq_unlink(SHOP_QUEUE_NAME);
    struct mq_attr attr = {.mq_maxmsg = MAX_MSG_COUNT, .mq_msgsize=  MSG_SIZE};
    mqd_t mq;
    if((mq=mq_open(SHOP_QUEUE_NAME,O_CREAT | O_RDWR,0666,&attr))==(mqd_t)-1)
        ERR("mq_open");
    mq_close(mq);

    create_children();

    while(wait(NULL)>0);

    printf("End\n");
    mq_unlink(SHOP_QUEUE_NAME);
    return EXIT_SUCCESS;
}