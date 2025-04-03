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
volatile sig_atomic_t child_count = 0;
void usage(char *name)
{
    fprintf(stderr, "USAGE: %s n k p l\n", name);
    fprintf(stderr, "100 > n > 0 - number of children\n");
    exit(EXIT_FAILURE);
}

typedef struct player_t
{
    char name[256];
    struct player_t* next;
    pid_t listen;
    int t1;
    int t2;
    int p;
}player_t;

void msleep(int milisec)
{
    time_t sec = (int)(milisec / 1000);
    milisec = milisec - (sec * 1000);
    struct timespec req = {0};
    req.tv_sec = sec;
    req.tv_nsec = milisec * 1000000L;
    if (nanosleep(&req, &req));
}

void clean_up(player_t*players)
{
    player_t* pointer;
    while(players!=NULL)
    {
        pointer = players;
        players = players->next;
        free(pointer);
    }
}

void send(mqd_t mq,char* message);

void child_work(player_t*player,mqd_t mqc)
{
    srand(getpid());
    printf("[%d] %s has joined the game!\n",getpid(),player->name);
    int time = player->t1 + rand()%(player->t2-player->t1 + 1);
    msleep(time);
    struct mq_attr attr = {.mq_msgsize = MAX_MSG_SIZE, .mq_maxmsg = 2};
 
    mqd_t mqr;
    char name[256];
    printf("%d\n",player->listen);
    snprintf(name,sizeof(name),"/sop_cwg_%d",player->listen);
    if((mqr = mq_open(name,O_RDWR | O_NONBLOCK | O_CREAT,0666,&attr))==(mqd_t)-1) ERR("mq_open");
    int p = player->p;
    int i=0;
    while(1)
    {
        int res;
        char word[MAX_MSG_SIZE];
        if((res = mq_receive(mqr,word,MAX_MSG_SIZE,0))<0)
        {
            if(errno==EAGAIN) 
            {
                if(i++ == 5)
                    break;
            }
            else ERR("mq_receive");
        }
        printf("[%d] %s got the message: '%s'\n",getpid(),player->name,word);
        int x = rand()%p;
        if(x%(p-1)==0)
        {
            x= rand()%strlen(word);
            word[x] = 'a' + rand()%26;
        }
        send(mqc,word);
    }
    
    printf("[%d] %s has left the game!\n",getpid(),player->name);
}
void create_children(player_t*players, mqd_t*mq)
{
    player_t*player = players;
    pid_t parent = getpid();
    char name[256];
    struct mq_attr attr = {.mq_msgsize = MAX_MSG_SIZE, .mq_maxmsg = 2};
    int i=0;
    while(player!=NULL)
    {
        int x = fork();
        switch(x)
        {
            case 0:
                mqd_t mqc;
                snprintf(name,sizeof(name),"/sop_cwg_%d",getpid());
                if((mqc = mq_open(name,O_CREAT | O_NONBLOCK | O_RDWR,0666,&attr))==(mqd_t)-1) 
                    ERR("mq_open");
                
                player->listen = parent;
                child_work(player,mqc);
                mq_close(mqc);
                mq_unlink(name);
                clean_up(players);
                exit(0);
                
            case -1: ERR("fork");
        }
        player = player->next;
        parent = x;
    }
    snprintf(name,sizeof(name),"/sop_cwg_%d",getpid());
    if((*mq = mq_open(name,O_CREAT | O_NONBLOCK | O_RDWR,0666,&attr))==(mqd_t)-1) 
        ERR("mq_open");

}

void send(mqd_t mq,char* message)
{
    char*str = strtok(message," ");
    char word[MAX_MSG_SIZE];
    while(str!=NULL)
    {
        memset(word,'\0',MAX_MSG_SIZE);
        memcpy(word,str,MAX_MSG_SIZE);
        if(TEMP_FAILURE_RETRY(mq_send(mq,word,MAX_MSG_SIZE,0))<0)
        {
            if(errno==EAGAIN) 
            {
                printf("[%d] queue is full!!\n",getpid());
                msleep(100);
                continue;
            }
            ERR("mq_send");
        }
        str = strtok(NULL," ");
    }
}
int main(int argc, char** argv)
{
    if(argc!=4) usage(argv[0]);
    int p = atoi(argv[1]);
    int t1 = atoi(argv[2]);
    int t2 = atoi(argv[3]);
    if(p<0 || p>100 | t1>t2 | t1<100 | t2>6000) usage(argv[0]);
    char buf[256];
    char sequence[256];
    player_t* players = NULL;
    player_t* pointer = players;
    mqd_t mq;
    while(1)
    {
        char* line=NULL;
        size_t x = 256;
        getline(&line,&x, stdin);
        sscanf(line, "%s",buf);
        snprintf(sequence,sizeof(sequence),"%s",line+strlen(buf));
        //printf("%s\n",buf);
        if(strcmp(buf,"start")==0) 
        {
            free(line);
            break;
        }
        if(players==NULL)
        {
            players = calloc(sizeof(player_t),1);
            strcpy(players->name,buf);
            players->next = NULL;
            players->p = p;
            pointer = players;
        }
        else
        {
            pointer->next = calloc(sizeof(player_t),1);
            pointer = pointer->next;
            strcpy(pointer->name,buf);
            pointer->next = NULL;
            pointer->p = p;
        }
        pointer->t1 = t1;
        pointer->t2 = t2;
        child_count++;
        free(line);
    }
    if(child_count<1) usage(argv[0]);
    
    create_children(players,&mq);
    send(mq,sequence);
    while(wait(NULL)>0);
    clean_up(players);
    mq_close(mq);
    char name[256];
    snprintf(name,sizeof(name),"/sop_cwg_%d",getpid());
    mq_unlink(name);
    return 0;
}   