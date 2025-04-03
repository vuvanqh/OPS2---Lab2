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
#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s fifo_file\n", name);
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv)
{
    if(argc!=3) usage(argv[0]);
    char client_name[MAX_MSG_SIZE], server_name[MAX_MSG_SIZE];
    snprintf(server_name,MAX_MSG_SIZE,"/chat_%s",argv[1]);
    snprintf(client_name,MAX_MSG_SIZE,"/chat_%s",argv[2]);
    printf("%s\n",client_name);
    mq_unlink(client_name);

    struct mq_attr attr = { .mq_maxmsg=10, .mq_msgsize = MAX_MSG_SIZE};
    mqd_t mq,server_queue;
    if((mq = mq_open(client_name,O_CREAT | O_EXCL | O_RDWR | O_NONBLOCK,0666,&attr))==(mqd_t)-1)
        ERR("mq_open");
    while(1)
    {
        if((server_queue = mq_open(server_name, O_RDWR | O_NONBLOCK))==(mqd_t)-1 && errno!=ENOENT)
            ERR("mq_open");
        if(server_queue>0) break;
    }

    while(1)
    {
        char msg[MAX_MSG_SIZE];
        snprintf(msg,MAX_MSG_SIZE,"%s",client_name);
        if(TEMP_FAILURE_RETRY(mq_send(server_queue,msg,MAX_MSG_SIZE,0))<0)
        {
            if(errno==EAGAIN) continue;
            ERR("mq_send");
        }  
        break;
    }
    while(1)
    {
        char* inpt;
        char msg[MAX_MSG_SIZE];
        size_t x = MAX_MSG_SIZE;
        char mg[MAX_MSG_SIZE];
        getline(&inpt,&x,stdin);
        snprintf(msg,sizeof(msg),"%s",inpt);
        if(TEMP_FAILURE_RETRY(mq_send(server_queue,msg,MAX_MSG_SIZE,2))<0)
        {
            if(errno!=EAGAIN) ERR("mq_send");
        }
        else printf("%s\n",msg);
        if(TEMP_FAILURE_RETRY(mq_receive(mq,mg,MAX_MSG_SIZE,0))<0)
        {
            if(errno!=EAGAIN) ERR("mq_receive");
        }
        printf("%s\n",mg);
    }
    mq_close(server_queue);
    mq_close(mq);
    mq_unlink(client_name);
    return 0;
}