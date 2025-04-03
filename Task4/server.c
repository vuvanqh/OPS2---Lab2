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

#define MAX_MSG_SIZE 30
#define MAX_CLIENT_COUNT 8
#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s fifo_file\n", name);
    exit(EXIT_FAILURE);
}
volatile sig_atomic_t client_count = 0;
volatile sig_atomic_t last_sig = 0;
typedef struct
{
    mqd_t mq;
    char name[MAX_MSG_SIZE];
    mqd_t server_queue;
    pid_t pid;
}client_t;

void sethandler(void (*f)(int, siginfo_t *, void *), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_sigaction = f;
    act.sa_flags = SA_SIGINFO;
    if (-1 == sigaction(sigNo, &act, NULL))
        ERR("sigaction");
}

void sighandler(int a, siginfo_t * b, void *c)
{
    last_sig=SIGINT;
}

void addClient(char* name,client_t*clients,pid_t pid);
void removeClient(client_t*clients,pid_t pid,mqd_t mq);
void broadcast(client_t*clients,char*msg,unsigned prio, pid_t pid);

void mq_handler(int sig, siginfo_t *info, void *p)
{
    client_t* clients;
    mqd_t *pin;

    clients = (client_t *)info->si_value.sival_ptr;
    pin = &(clients[0].server_queue);

    static struct sigevent notif;
    notif.sigev_notify = SIGEV_SIGNAL;
    notif.sigev_signo = SIGRTMIN;
    notif.sigev_value.sival_ptr = clients;
    if (mq_notify(*pin, &notif) < 0)
        ERR("mq_notify");

    while(1)
    {
        char msg[MAX_MSG_SIZE];
        unsigned prio;
        if(TEMP_FAILURE_RETRY(mq_receive(*pin,msg,MAX_MSG_SIZE,&prio))<0)
        {
            if(errno!=EAGAIN) ERR("mq_receive");
            break;
        }
        switch(prio)
        {
            case 0:
            {
                char client_name[MAX_MSG_SIZE];
                snprintf(client_name,MAX_MSG_SIZE,"%s",msg);
                printf("%s\n",client_name);
                addClient(client_name,clients,info->si_pid);
                break;
            }
            case 1:
            {
                removeClient(clients,info->si_pid, *pin);
                break;
            }
            case 2:
            {
                broadcast(clients,msg,prio,info->si_pid);
                break;
            }
        }
    }
}

void addClient(char* name,client_t*clients,pid_t pid)
{
    strcpy(clients[client_count].name,name);
    clients[client_count].pid = pid;
    clients[client_count].mq = mq_open(name,O_RDWR);
    printf("Client %s has connected!\n",name);
    client_count++;
}
void removeClient(client_t*clients,pid_t pid,mqd_t mq)
{
    int ind;
    for(ind=0;ind<client_count;ind++)
    {
        if(clients[ind].pid==pid)
            break;
    }

    printf("Client %s has disconnected!\n",clients[ind].name);

    for(int i=ind;i<client_count-1;i++)
        clients[i] = clients[i+1];
    
    client_count--;
    memset(&clients[client_count],0,sizeof(client_t));
    clients[client_count].server_queue = mq;
}
void broadcast(client_t*clients,char*msg,unsigned prio,pid_t pid)
{
    int ind;
    for(ind=0;ind<client_count;ind++)
    {
        if(clients[ind].pid==pid)
            break;
    }
    char mg[MAX_MSG_SIZE];
    if(ind!=client_count)
        snprintf(mg,MAX_MSG_SIZE,"[%s]: %s",clients[ind].name,msg);
    else
        snprintf(mg,MAX_MSG_SIZE,"%s",msg);
    printf("%s\n",mg);
    for(int i=0;i<client_count;i++)
    {
        if(i!=ind)
        {
            if(TEMP_FAILURE_RETRY(mq_send(clients[i].mq,mg,MAX_MSG_SIZE,prio))<0)
            {
                if(errno!=EAGAIN) ERR("mq_send");
            }
        }
    }
}
/*
/chat_server/client name
*/
int main(int argc, char** argv)
{
    if(argc!=2) usage(argv[0]);
    char name[MAX_MSG_SIZE];
    snprintf(name,MAX_MSG_SIZE,"/chat_%s",argv[1]);
    mq_unlink(name);

    //init
    struct mq_attr attr = { .mq_maxmsg=10, .mq_msgsize = MAX_MSG_SIZE};
    mqd_t mq = mq_open(name,O_CREAT | O_RDWR | O_EXCL | O_NONBLOCK, 0666,&attr);
    if(mq==(mqd_t)-1) ERR("mq_open");

    client_t* clients = calloc(sizeof(client_t),MAX_CLIENT_COUNT);
    for(int i=0;i<8;i++)
        clients[i].server_queue = mq;
    
    sethandler(sighandler,SIGINT);
    sethandler(mq_handler,SIGRTMIN);
    struct sigevent notif;
    notif.sigev_notify = SIGEV_SIGNAL;
    notif.sigev_signo = SIGRTMIN;
    notif.sigev_value.sival_ptr = clients;
    
    if (mq_notify(mq, &notif) < 0)
        ERR("mq_notify");

    printf("waiting...\n");
    while (1) {
        if(last_sig==SIGINT)
        {
            for(int i=0;i<client_count;i++)
            {
                char gb =' ';
                int res;
                while((res = mq_send(clients[i].mq,&gb,1,1))==-1)
                {
                    if(errno ==EAGAIN)
                        continue;
                    ERR("mq_send");
                }
            }
            break;
        }
        char msg[MAX_MSG_SIZE];
        if (fgets(msg, MAX_MSG_SIZE, stdin) != NULL) {
            snprintf(msg,sizeof(msg),"[SERVER] %s",msg);
            broadcast(clients,msg, 2,10);
        }
    }

    free(clients);
    mq_close(mq);
    mq_unlink(name);
    return 0;
}