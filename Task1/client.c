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

#define LIFE_SPAN 10
#define MAX_NUM 10

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

void sethandler(void (*f)(int, siginfo_t *, void *), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_sigaction = f;
    act.sa_flags = SA_SIGINFO;
    if (-1 == sigaction(sigNo, &act, NULL))
        ERR("sigaction");
}

int main(int argc,char** argv)
{
    if(argc!=2) ERR("lol");
    struct mq_attr attr = { .mq_maxmsg = 10, .mq_msgsize = 30};
    mqd_t mqs,mqc;
    char name[256];
    struct timespec ts;
    snprintf(name,sizeof(name),"/%d",getpid());
    if((mqs=TEMP_FAILURE_RETRY(mq_open(argv[1], O_RDWR)))<0)
        ERR("mq_open");
    if((mqc=TEMP_FAILURE_RETRY(mq_open(name,O_CREAT | O_RDWR,0666,&attr)))<0)
        ERR("mq_open");
    sleep(1);
    while(1)
    {
        int x,y;
        if(scanf("%d %d",&x,&y)!=2) ERR("scanf");
        snprintf(name,sizeof(name),"/%d %d %d",getpid(),x,y);
        if(TEMP_FAILURE_RETRY(mq_send(mqs,name,30,0))<0) 
        {
            ERR("mq_send");
        }
        if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
            ERR("clock_gettime");
        ts.tv_nsec =100*1000000;
        int res = 0;
        if((res = TEMP_FAILURE_RETRY(mq_timedreceive(mqc,name,30,0,&ts)))<0) 
        {
            ERR("mq_send");
        }
        if(res==0) break;
        printf("%d\n",atoi(name));
    }
    printf("client exits\n");
    mq_close(mqs);
    mq_close(mqc);
    mq_unlink(name);
    return 0;
}

