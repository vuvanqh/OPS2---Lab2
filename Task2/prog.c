#define _GNU_SOURCE
#include <errno.h>
#include <mqueue.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#define MAX_MSG_SIZE 30

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

volatile sig_atomic_t last_sig=0;
void usage(char *name)
{
    fprintf(stderr, "USAGE: %s n k p l\n", name);
    fprintf(stderr, "100 > n > 0 - number of children\n");
    exit(EXIT_FAILURE);
}
void sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        ERR("sigaction");
}
void sighandler(int sig)
{
    last_sig=sig;
}
void msleep(int milisec)
{
    time_t sec = (int)(milisec / 1000);
    milisec = milisec - (sec * 1000);
    struct timespec req = {0};
    req.tv_sec = sec;
    req.tv_nsec = milisec * 1000000L;
    if (nanosleep(&req, &req));
}


void child_work(mqd_t mq_s, mqd_t mq)
{
    srand(getpid());
    int time = 500 + rand()%1501;
    printf("[%d] Worker ready!\n",getpid());
    for(int i=0;i<5;i++)
    {
        int res;
        char buf[MAX_MSG_SIZE];
        if((res = TEMP_FAILURE_RETRY(mq_receive(mq_s,buf,MAX_MSG_SIZE,0)))<0)
        {
            if(errno==EAGAIN)
            {
                i--;
                continue;
            }
            ERR("mq_receive");
        }
        if(res==0) break;
        float a = atof(strtok(buf," "));
        float b = atof(strtok(NULL," "));
        if(a==-1 || b==-1) break;
        msleep(time);
        printf("[%d] Result [%f]\n",getpid(),a+b);
        snprintf(buf,sizeof(buf),"%d %.3f",getpid(),a+b);

        if( TEMP_FAILURE_RETRY(mq_send(mq,buf,MAX_MSG_SIZE,0))<0)
        {
            if(errno!=EAGAIN)   
                ERR("mq_receive");
        }
    }
    printf("[%d] Exits!\n",getpid());
    mq_close(mq);
}
void create_children(int n, char(* children)[200],mqd_t mq_s)
{
    struct mq_attr attr =  { .mq_msgsize = MAX_MSG_SIZE, .mq_maxmsg = 10};
    for(int i=0;i<n;i++)
    {
        int x = fork();
        switch(x)
        {
            case 0:
                char name[200];
                snprintf(name,sizeof(name),"/result_queue_%d_%d",getppid(),getpid());
                mqd_t mq;
                if((mq = mq_open(name,O_CREAT | O_NONBLOCK | O_RDWR,0666,&attr))==(mqd_t)-1) ERR("mq_open");
                printf("from child: %d\n",mq);
                child_work(mq_s,mq);
                mq_unlink(name);
                mq_close(mq_s);
                exit(0);
            case -1:
                ERR("fork");
        }
        snprintf(children[i],sizeof(children[i]),"/result_queue_%d_%d",getpid(),x);
        printf("%s\n",children[i]);
    }
}

void parent_work(int n, char(*children)[200],mqd_t mq_s,mqd_t* childq)
{
    srand(getpid());
    struct mq_attr attr =  {.mq_flags = O_NONBLOCK, .mq_msgsize = MAX_MSG_SIZE, .mq_maxmsg = 10};
    mq_setattr(mq_s,&attr,NULL);
    for(int i=0;i<n;i++)
    {
        childq[i] = mq_open(children[i],O_CREAT | O_NONBLOCK | O_RDWR,0666,&attr); //apparently czasem szybciej parent konczy child_work niz child tworzy queue w sobie
        printf("childq[i]: %d\n",childq[i]);
    }
    
    for(int i=0;i<5*n;i++)
    {
        char task[MAX_MSG_SIZE];
        if(last_sig == SIGINT)
        {
            for(int k=0;k<n;k++)
            {
                snprintf(task,sizeof(task),"%d %d",-1,-1);
                if(TEMP_FAILURE_RETRY(mq_send(mq_s,task,sizeof(task),0))<0)
                {
                    if(errno==EAGAIN)
                    {
                        printf("Queue is fullsss!\n");
                        continue;
                    }
                    ERR("mq_send");
                }
            }
            break;
        }
        int time = 100 + rand() %4901;
        float a =  (float)rand() / (float)RAND_MAX * 100;
        float b = (float)rand() / (float)RAND_MAX * 100;
        snprintf(task,sizeof(task),"%.3f %.3f",a,b);
        if(TEMP_FAILURE_RETRY(mq_send(mq_s,task,sizeof(task),0))<0)
        {
            if(errno==EAGAIN)
            {
                printf("Queue is full!\n");
                i--;
                continue;
            }
            ERR("mq_send");
        }
        printf("New task queued: [%.3f, %.3f]\n",a,b);
        msleep(time);
        for(int k=0;k<n;k++)
        {
            int res;
            char buf[MAX_MSG_SIZE];
            if((res = TEMP_FAILURE_RETRY(mq_receive(childq[k],buf,MAX_MSG_SIZE,0)))<0)
            {
                if(errno==EAGAIN)
                    continue;
                printf("%s\n",children[k]);
                printf("%d\n",childq[k]);
                ERR("mq_receive");
            }
            int id = atof(strtok(buf," "));
            float result = atof(strtok(NULL," "));
            printf("Result from worker [%d]: [%f]\n",id,result);
        }
    }

    for(int i=0;i<n;i++)
        mq_close(childq[i]);
}
//100<=T1<T2<=5000 time of adding a task? with a pari of flp [0.0,100.0]
// 2 <= N <= 20 workers
int main(int argc,char**argv)
{
    if(argc!=2) usage(argv[0]);
    int n = atoi(argv[1]);
    if(n<2 || n>20) usage(argv[0]);

    char(* children)[200] = calloc(sizeof(char),n*200); 
    char name[110];
    snprintf(name,sizeof(name),"/task_queue_%d",getpid());
    mqd_t mq_s;
    mqd_t* childq = calloc(sizeof(mqd_t),n);
    struct mq_attr attr = {.mq_maxmsg = 10, .mq_msgsize = MAX_MSG_SIZE};

    if((mq_s = mq_open(name, O_CREAT | O_RDWR ,0666, &attr))==(mqd_t)-1) ERR("mq_open");
    printf("Server is starting...\n");

    sethandler(sighandler,SIGINT);

    create_children(n,children,mq_s);

    parent_work(n,children,mq_s,childq);
    while(wait(NULL)>0);

    printf("All child processes have finished.\n");
    mq_close(mq_s);
    mq_unlink(name);
    free(children);
    free(childq);
    return 0;
}