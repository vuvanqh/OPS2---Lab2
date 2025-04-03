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

#define MAX_INPUT_LENGTH 255
#define MAX_NAME_LENGTH 64
#define MAX_MSG 2
#define MSG_SIZE 255

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

volatile sig_atomic_t last_signal=0;

void usage(char* pname)
{
    fprintf(stderr,"USAGE:%s\n",pname);
    exit(EXIT_SUCCESS);
}

int sethandler(void(*f)(int), int sig)
{
    struct sigaction act;
    memset(&act,0,sizeof(struct sigaction));
    act.sa_handler=f;
    if(sigaction(sig,&act,NULL)==-1)
        return -1;
    return 0;
}

void sigintHandler(int sig)
{
    last_signal=SIGINT;
}

void child_work(pid_t listen_pid,char* child_name,int T1, int T2,int P)
{
    srand(getpid());
    if(sethandler(sigintHandler,SIGINT)==-1)
        ERR("sethandler");
    printf("[%d] %s has joined the game!\n",getpid(),child_name);

    struct mq_attr attr = {};
    attr.mq_maxmsg = MAX_MSG;
    attr.mq_msgsize = MSG_SIZE;
    char child_queue_name[MAX_NAME_LENGTH];
    snprintf(child_queue_name,MAX_NAME_LENGTH,"/sop_cwg_%d",getpid());
    mqd_t child_q = mq_open(child_queue_name,O_WRONLY|O_CREAT,0600,&attr);
    if(child_q<0)
        ERR("mq_open");

    char listen_queue_name[MAX_NAME_LENGTH];
    snprintf(listen_queue_name,MAX_NAME_LENGTH,"/sop_cwg_%d",listen_pid);
    mqd_t listen_q;
    while(1)
    {
        listen_q = mq_open(listen_queue_name,O_RDONLY);
        if(listen_q<0 && errno!=ENOENT)
            ERR("mq_open");
        if(listen_q>0)
            break;
    }

    while(1)
    {
        if(last_signal==SIGINT)
        {
            printf("[%d] %s KILLED\n",getpid(),child_name);
            exit(EXIT_SUCCESS);
        }
        int t = T1 + rand()% (T2-T1+1);
        struct timespec ts = {0,t*1000000};
        if (ts.tv_nsec >= 1000000000)
        {  
            ts.tv_sec += ts.tv_nsec / 1000000000;
            ts.tv_nsec = ts.tv_nsec % 1000000000;
        }
        while(nanosleep(&ts,&ts)>0){}

        unsigned int msg_prio;
        char message[MAX_INPUT_LENGTH];

        struct timespec tr;
        if(clock_gettime(CLOCK_REALTIME,&tr)==-1)
            ERR("clock_gettime");
        
        tr.tv_nsec += 100000000; 

        if (tr.tv_nsec >= 1000000000) {  
            tr.tv_sec += tr.tv_nsec / 1000000000;
            tr.tv_nsec = tr.tv_nsec % 1000000000;
        }

        int res = mq_timedreceive(listen_q,message,MAX_INPUT_LENGTH,&msg_prio,&tr);
        if(res<0 && (errno == EINTR || errno ==EAGAIN || errno == ETIMEDOUT))
            continue;
        else if(res<0)
            ERR("mq_receive");

        if(msg_prio==1)
            break;
        printf("[%d] %s got the message: %s\n",getpid(),child_name,message);
        int i = 0;
        while(i<MAX_NAME_LENGTH)
        {
            if(message[i]=='\0')
                break;
            int a = rand();
            if(a%P==0)
            {
                char new = 'a' + rand()%('z'-'a');
                message[i]=new;
            }
            i++;
        }
        while(1)
        {
            struct timespec tk;
            if(clock_gettime(CLOCK_REALTIME,&ts)==-1)
                ERR("clock_gettime");
        
            tk.tv_nsec += 100000000; 
            if (tk.tv_nsec >= 1000000000)
            {  
                tk.tv_sec += tk.tv_nsec / 1000000000;
                tk.tv_nsec = tk.tv_nsec % 1000000000;
            }

            res = mq_timedsend(child_q,message,MAX_INPUT_LENGTH,2,&tk);
            if(res<0 && (errno == EINTR || errno ==EAGAIN || errno == ETIMEDOUT))
                continue;
            else if(res<0)
                ERR("mq_receive");
            else
                break;
        }
    }

    char message[MAX_INPUT_LENGTH];
    memset(message,0,MAX_INPUT_LENGTH);

    while(1)
    {
        if(last_signal==SIGINT)
            break;

        struct timespec ts;
        if(clock_gettime(CLOCK_REALTIME,&ts)==-1)
            ERR("clock_gettime");
    
        ts.tv_nsec += 100000000; 
        if (ts.tv_nsec >= 1000000000)
        {  
            ts.tv_sec += ts.tv_nsec / 1000000000;
            ts.tv_nsec = ts.tv_nsec % 1000000000;
        }

        int res = mq_timedsend(child_q,message,MAX_INPUT_LENGTH,1,&ts);
        if(res<0 && (errno == EINTR || errno ==EAGAIN || errno == ETIMEDOUT))
            continue;
        else if(res<0)
            ERR("mq_receive");
        else 
            break;
    }

    printf("[%d] %s has left the game!\n",getpid(),child_name);
    exit(EXIT_SUCCESS);
}

void create_children(int child_count, char** child_names,pid_t* pids,int T1, int T2,int P)
{
    for(int i = 0;i<child_count;i++)
    {
        pid_t child_pid, listen_pid;
        if(i==0)
            listen_pid = getpid();
        else
            listen_pid = pids[i-1];

        if((child_pid=fork())==-1)
            ERR("child_pid");
        if(child_pid==0)
            child_work(listen_pid,child_names[i],T1,T2,P);
        else
            pids[i]=child_pid;
    }
}

void parent_work(char* sentence,pid_t listen_pid)
{
    struct mq_attr attr = {};
    attr.mq_maxmsg = MAX_MSG;
    attr.mq_msgsize = MSG_SIZE;
    char coordinator_queue_name[MAX_NAME_LENGTH];
    snprintf(coordinator_queue_name,MAX_NAME_LENGTH,"/sop_cwg_%d",getpid());
    mqd_t coordinator_q = mq_open(coordinator_queue_name,O_WRONLY|O_CREAT,0600,&attr);
    if(coordinator_q<0)
        ERR("mq_open");

    char* w = strtok(sentence, " ");  
    char word[MAX_INPUT_LENGTH];

    while (w != NULL)
    {
        memset(word,'\0',MAX_INPUT_LENGTH);
        memcpy(word,w,MAX_INPUT_LENGTH);

        if(mq_send(coordinator_q,word,MAX_INPUT_LENGTH,2)==-1)
            ERR("mq_send");
        w = strtok(NULL, " ");
    }

    memset(word,'0',MAX_INPUT_LENGTH);
    if(mq_send(coordinator_q,word,MAX_INPUT_LENGTH,1)==-1)
        ERR("mq_send");

    char listen_queue_name[MAX_NAME_LENGTH];
    snprintf(listen_queue_name,MAX_NAME_LENGTH,"/sop_cwg_%d",listen_pid);
    mqd_t listen_q;
    while(1)
    {
        listen_q = mq_open(listen_queue_name,O_RDONLY);
        if(listen_q<0 && errno !=ENOENT)
            ERR("mq_open");
        if(listen_q>0)
            break;
    }

    while(1)
    {
        if(last_signal==SIGINT)
            break;
        unsigned int msg_prio;
        char message[MAX_INPUT_LENGTH];

        struct timespec ts;
        if(clock_gettime(CLOCK_REALTIME,&ts)==-1)
            ERR("clock_gettime");
        
        ts.tv_nsec += 100000000; 

        if (ts.tv_nsec >= 1000000000) {  
            ts.tv_sec += ts.tv_nsec / 1000000000;
            ts.tv_nsec = ts.tv_nsec % 1000000000;
        }

        int res = mq_timedreceive(listen_q,message,MAX_INPUT_LENGTH,&msg_prio,&ts);
        if(res<0 && (errno == EINTR || errno ==EAGAIN || errno == ETIMEDOUT))
            continue;
        else if(res<0)
            ERR("mq_receive");

        if(msg_prio==1)
            break;
        printf("[%d] Coordinator got the message: %s\n",getpid(),message);
    }
}

int main(int argc, char** argv)
{
    if(argc!=4)
        usage(argv[0]);
    
    int P = atoi(argv[1]);
    int T1 = atoi(argv[2]);
    int T2 = atoi(argv[3]);

    if(P<0 || P>100 || T1<100 || T1>T2 || T2>6000)
        usage(argv[0]);
    if(sethandler(sigintHandler,SIGINT)==-1)
        ERR("sethandler");

    int child_count=0;
    char** child_names = (char**)malloc(sizeof(char*));
    if(!child_names)
        ERR("malloc");
    char input[MAX_INPUT_LENGTH];
    char sentence[MAX_INPUT_LENGTH];

    while(fgets(input,MAX_INPUT_LENGTH,stdin)!=NULL)
    {
        input[strcspn(input, "\n")] = '\0';  //strcspn returns the string until the occurence of the specified part

        if(sscanf(input,"start \"%[^\"]\"",sentence)==1)
           break;
        char *line = strdup(input);
        if (!line)
            ERR("strdup");

        child_names[child_count] = line;
        child_count++;

        char**tmp = realloc(child_names,sizeof(char*)*(child_count+1));
        if (!tmp) {
            for (int i = 0; i < child_count; i++)
                free(child_names[i]);
            free(child_names);
            ERR("realloc");
        }
        child_names = tmp;
    }
    if(child_count<1)
        usage("At least one name required\n");
    pid_t* pids = (pid_t*)malloc(sizeof(pid_t)*child_count);
    create_children(child_count,child_names,pids,T1,T2,P);
    
    parent_work(sentence,pids[child_count-1]);

    while(waitpid(0,NULL,0)>0){}

    printf("GAME OVER!\n");
    
    for(int i = 0;i<child_count;i++)
        free(child_names[i]);
    free(child_names);
    free(pids);
}