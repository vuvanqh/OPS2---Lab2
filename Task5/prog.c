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
#include <string.h>

#define MAX_ITEMS 8
#define MIN_ITEMS 2
#define SHOP_QUEUE_NAME "/shop"
#define TIMEOUT 2
#define CLIENT_COUNT 8
#define OPEN_FOR 8
#define START_TIME 8
#define MAX_AMOUNT 16

static const char* const UNITS[] = {"kg", "l", "dkg", "g"};//4
static const char* const PRODUCTS[] = {"mięsa", "śledzi", "octu", "wódki stołowej", "żelatyny"};//5
#define MAX_MSG_SIZE 100
#define MAX_CLIENT_COUNT 8

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

volatile sig_atomic_t last_sig = 0;
void usage(char *name)
{
    fprintf(stderr, "USAGE: %s fifo_file\n", name);
    exit(EXIT_FAILURE);
}
typedef struct
{
    char product[15];
    char units[5];
    char sender[15];
    int quantity;
    mqd_t mq;
}data_t;

void sethandler(void (*f)(int, siginfo_t *, void *), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_sigaction = f;
    act.sa_flags = SA_SIGINFO;
    if (-1 == sigaction(sigNo, &act, NULL))
        ERR("sigaction");
}

void sighandler(int sig, siginfo_t *info, void *p)
{
    last_sig=sig;
}

void mq_handler(int sig, siginfo_t *info, void *p)
{
    mqd_t *pin;
    char ni[MAX_MSG_SIZE];
    unsigned msg_prio;

    pin = (mqd_t *)info->si_value.sival_ptr;

    static struct sigevent notif;
    notif.sigev_notify = SIGEV_SIGNAL;
    notif.sigev_signo = SIGRTMIN;
    notif.sigev_value.sival_ptr = pin;
    if (mq_notify(*pin, &notif) < 0)
        ERR("mq_notify");

    for (;;)
    {
        if (TEMP_FAILURE_RETRY(mq_receive(*pin,ni,MAX_MSG_SIZE, &msg_prio) )< 1)
        {
            if (errno == EAGAIN)
                break;
            else
                ERR("mq_receive");
        }
        printf("[PRINTER] %s\n",ni);
    }
}

void generator_work(mqd_t mq,data_t* data)
{
    srand(getpid());
    printf("wassup from generator\n");
    while(1)
    {
        if(last_sig==SIGINT)break;
        int ind = rand()%5;
        strcpy(data->product,PRODUCTS[ind]);
        ind = rand()%3;
        strcpy(data->units,UNITS[ind]);
        data->quantity = rand()%10;
        printf("%d%s %s\n",data->quantity,data->units,data->product);
        snprintf(data->sender,sizeof(data->sender),"%d",getpid());
        struct mq_attr attr;
        mq_getattr(mq,&attr);
        attr.mq_flags = O_NONBLOCK;
        mq_setattr(mq,&attr,NULL);
        if(TEMP_FAILURE_RETRY(mq_send(mq,(char*)data,MAX_MSG_SIZE,0))<0)
        {
            if(errno!=EAGAIN) ERR("mq_send");
        }
        sleep(5);
    }
    free(data);
    printf("g naura\n");
}
void child_work(mqd_t mq1,mqd_t mq2,data_t* data, pid_t pid)
{
    srand(getpid());
    printf("wassup from child[%d]\n",pid);
    while(1)
    {
        if(last_sig==SIGINT)break;
        data_t dt;
        if(TEMP_FAILURE_RETRY(mq_receive(mq1,(char*)&dt,MAX_MSG_SIZE,0))<0)
        {
            if(errno==EAGAIN) continue;
            ERR("mq_receive");
        }
        if(atoi(dt.sender)!=pid)
        {
            if(TEMP_FAILURE_RETRY(mq_send(mq1,(char*)&dt,MAX_MSG_SIZE,0))<0)
            {
                if(errno==EAGAIN) continue;
                ERR("mq_receive");
            }
        }
        else
        {
            dt.quantity = rand()%10;
            char msg[MAX_MSG_SIZE];
            snprintf(msg,MAX_MSG_SIZE,"%d%s %s\n",dt.quantity,dt.units,dt.product);
            printf("[%d] %d%s %s\n",getpid(),dt.quantity,dt.units,dt.product);
            snprintf(dt.sender,sizeof(dt.sender),"%d",getpid());
            sleep(1);
            if(data->mq==mq2)
            {
                if(TEMP_FAILURE_RETRY(mq_send(data->mq,msg,MAX_MSG_SIZE,0))<0)
                {
                    if(errno!=EAGAIN) ERR("mq_send");
                }
            }
            else
            {
                if(TEMP_FAILURE_RETRY(mq_send(data->mq,(char*)&dt,MAX_MSG_SIZE,0))<0)
                {
                    if(errno!=EAGAIN) ERR("mq_send");
                } 
            }
            //break;  
        }
    }
    sleep(1);
    printf("[%d] naura\n",pid);
    free(data);
}
void printer_work(mqd_t mq)
{
    printf("wassup from printer\n");
    sleep(1);
    while(1)
    {
        if(last_sig==SIGINT)
        {
            printf("p naura\n");
            break;
        }
    }
   // printf("[PRINTER] %s\n",msg);
}
void create_children(int n,mqd_t mq1,mqd_t mq2)
{
    
    data_t* data = calloc(sizeof(data_t),1);
    int x = fork();
    if(x==0)
    {
        mq_close(mq2);
        generator_work(mq1,data);
        mq_close(mq1);
        exit(0);
    }
    sleep(1);
    for(int i=0;i<n;i++)
    {
        int a = fork();
        if(a==0)
        {
            data->mq = mq1;
            if(i==n-1) data->mq = mq2;
            child_work(mq1,mq2,data,x);
            mq_close(mq1);
            mq_close(mq2);
            exit(0);
        }
        else if(a==-1)ERR("fork");
        x=a;
    }

    x=fork();
    if(x==0)
    {
        data->mq = mq2;
        struct sigevent notif;
        notif.sigev_notify = SIGEV_SIGNAL;
        notif.sigev_signo = SIGRTMIN;
        notif.sigev_value.sival_ptr = &mq2;
        if (mq_notify(mq2, &notif) < 0)
            ERR("mq_notify");
        mq_close(mq1);
        printer_work(mq2);
        mq_close(mq2);
        exit(0);
    }
    free(data);
}

int main(int argc,char**argv)
{
    if(argc!=2) usage(argv[0]);
    int n = atoi(argv[1]);
    mqd_t mq_proc,mq_unproc;
    char name1[255],name2[255];
    strcpy(name1,"/mq_proc");
    strcpy(name2,"/mq_unproc");
    mq_unlink(name1);
    mq_unlink(name2);

    struct mq_attr attr = { .mq_msgsize = MAX_MSG_SIZE, .mq_maxmsg = 10};

    mq_proc = mq_open(name1,O_CREAT | O_RDWR, 0666,&attr);
    mq_unproc = mq_open(name2,O_CREAT | O_RDWR | O_NONBLOCK, 0666,&attr);
    sethandler(mq_handler,SIGRTMIN);
    sethandler(sighandler,SIGINT);
    create_children(n,mq_proc,mq_unproc);

    while(wait(NULL)>0); 

    mq_close(mq_proc);
    mq_close(mq_unproc);
    mq_unlink(name1);
    mq_unlink(name2);
    return 0;
}