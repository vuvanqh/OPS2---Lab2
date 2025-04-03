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

volatile sig_atomic_t lastSig = 0;

void sethandler(void (*f)(int, siginfo_t *, void *), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_sigaction = f;
    act.sa_flags = SA_SIGINFO;
    if (-1 == sigaction(sigNo, &act, NULL))
        ERR("sigaction");
}


int sethandler2(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        return -1;
    return 0;
}
void sighandler(int signo)
{
    lastSig = signo;
}

int main(int argc, char** argv)
{
    struct mq_attr attr = { .mq_maxmsg = 10, .mq_msgsize = 30};

    char n1[256],n2[256],n3[256];
    snprintf(n1,sizeof(n1),"/%d_s",getpid());
    mqd_t mqs;
    if((mqs = mq_open(n1, O_CREAT | O_RDWR | O_NONBLOCK, 0666,&attr))<0)
    {
        if(errno!=EAGAIN) ERR("mq_open");
    }
    snprintf(n2,sizeof(n2),"/%d_d",getpid());
    mqd_t mqd;
    if((mqd = mq_open(n2, O_CREAT | O_RDWR | O_NONBLOCK, 0666,&attr))<0)
    {
        if(errno!=EAGAIN) ERR("mq_open");
    }
    snprintf(n3,sizeof(n3),"/%d_m",getpid());
    mqd_t mqm;
    if((mqm = mq_open(n3, O_CREAT | O_RDWR | O_NONBLOCK, 0666,&attr))<0)
    {
        if(errno!=EAGAIN) ERR("mq_open");
    }
    sethandler2(sighandler,SIGINT);
    char buf[256];
    sleep(1);
    while(lastSig!=SIGINT)
    {
        char client[256];
        int x,y;
        if(TEMP_FAILURE_RETRY(mq_receive(mqs,buf,30,0))<0) 
        {
            if(errno!=EAGAIN) ERR("mq_receive");
        }
        else
        {
            snprintf(client, sizeof(client),"%s",strtok(buf," "));
            mqd_t mq = mq_open(client, O_RDWR);
            x = atoi(strtok(NULL," "));
            y = atoi(strtok(NULL," "));
            printf("%s %d %d\n",client,x,y);
            snprintf(buf,sizeof(buf),"%d",x+y);
            if(TEMP_FAILURE_RETRY(mq_send(mq,buf,30,0))<0) ERR("mq_receive");
            mq_close(mq);
        }
        if(TEMP_FAILURE_RETRY(mq_receive(mqd,buf,30,0))<0) 
        {
            if(errno!=EAGAIN) ERR("mq_receive");
        }
        else
        {
            snprintf(client, sizeof(client),"%s",strtok(buf," "));
            mqd_t mq = mq_open(client, O_RDWR);
            x = atoi(strtok(NULL," "));
            y = atoi(strtok(NULL," "));
            printf("%s %d %d\n",client,x,y);
            snprintf(buf,sizeof(buf),"%d",x/y);
            if(TEMP_FAILURE_RETRY(mq_send(mqd,buf,30,0))<0) ERR("mq_receive");
            mq_close(mq);
        }
        if(TEMP_FAILURE_RETRY(mq_receive(mqm,buf,30,0))<0) 
        {
            if(errno!=EAGAIN) ERR("mq_receive");
        }
        else
        {
            mqd_t mq = mq_open(client, O_RDWR);
            snprintf(client, sizeof(client),"%s",strtok(buf," "));
            x = atoi(strtok(NULL," "));
            y = atoi(strtok(NULL," "));
            printf("%s %d %d\n",client,x,y);
            snprintf(buf,sizeof(buf),"%d",x*y);
            if(TEMP_FAILURE_RETRY(mq_send(mqm,buf,30,0))<0) ERR("mq_receive");
            mq_close(mq);
        }
    }

    printf("%s\n",n1);
    mq_close(mqs);
    mq_unlink(n1);
    printf("%s\n",n2);
    mq_close(mqd);
    mq_unlink(n2);
    printf("%s\n",n3);
    mq_close(mqm);
    mq_unlink(n3);
    return 0;
}