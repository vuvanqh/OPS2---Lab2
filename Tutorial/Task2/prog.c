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

#define CHILD_COUNT 4
#define ROUNDS 5
#define QUEUE_NAME_MAX_LEN 32
#define CHILD_NAME_MAX_LEN 32

#define MSG_SIZE 64
#define MAX_MSG_COUNT 4

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

typedef struct
{
    char*name;
    mqd_t queue;
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

mqd_t create_queue(char* name)
{
    struct mq_attr attr = {};
    attr.mq_maxmsg = MAX_MSG_COUNT;
    attr.mq_msgsize = MSG_SIZE;

    mqd_t res = mq_open(name, O_RDWR | O_NONBLOCK | O_CREAT, 0600, &attr);
    if (res == -1)
        ERR("mq_open");
    return res;
}

void handle_messages(union sigval data);

void register_notification(data_t* data)
{
    struct sigevent notification = {};
    notification.sigev_value.sival_ptr = data;
    notification.sigev_notify = SIGEV_THREAD;
    notification.sigev_notify_function = handle_messages;

    int res = mq_notify(data->queue, &notification);
    if (res == -1)
        ERR("mq_notify");
}

void handle_messages(union sigval data)
{
    data_t* child_data = data.sival_ptr;
    char message[MSG_SIZE];

    register_notification(child_data);

    for (;;)
    {
        int res = mq_receive(child_data->queue, message, MSG_SIZE, NULL);
        if (res != -1)
            printf("%s: Accepi \"%s\"\n", child_data->name, message);
        else
        {
            if (errno == EAGAIN)
                break;
            else
                ERR("mq_receive");
        }
    }
}

void child_work(char* name, mqd_t* queues, int i)
{
    printf("%s: A PID %d incipiens.\n", name, getpid());
    srand(getpid());
    data_t data = {.name = name, .queue = queues[i]};
    union sigval dataU;
    dataU.sival_ptr = &data;
    handle_messages(dataU);
    for (int i = 0; i < ROUNDS; ++i)
    {
        int receiver;
        do 
        {
            receiver = rand() % CHILD_COUNT;
        }while(receiver==i);

        char message[MSG_SIZE];
        switch (rand() % 3)
        {
            case 0:
                snprintf(message, MSG_SIZE, "%s: Salve %d!", name, receiver);
                break;
            case 1:
                snprintf(message, MSG_SIZE, "%s: Visne garum emere, %d?", name, receiver);
                break;
            case 2:
                snprintf(message, MSG_SIZE, "%s: Fuistine hodie in thermis, %d?", name, receiver);
                break;
        }

        if(mq_send(queues[receiver], message,MSG_SIZE,0)==-1) ERR("mq_send");

        int sleep_time = rand() % 3 + 1;
        sleep(sleep_time);
    }

    printf("%s: Disceo.\n", name);

    exit(EXIT_SUCCESS);
}

void create_children(char(*name)[32], mqd_t* queues)
{
    for(int i=0;i<CHILD_COUNT;i++)
    {
        switch(fork())
        {
            case 0:
                child_work(name[i], queues, i);
            case -1:
                ERR("fork");
       }
    }
}

int main(int argc,char**argv)
{
    mqd_t queues[CHILD_COUNT];
    char names[CHILD_COUNT][CHILD_NAME_MAX_LEN];
    char queue_names[CHILD_COUNT][CHILD_NAME_MAX_LEN];
    
    for (int i = 0; i < CHILD_COUNT;i++)
    {
        snprintf(queue_names[i], QUEUE_NAME_MAX_LEN, "/child_%d", i);
        queues[i] = create_queue(queue_names[i]);

        snprintf(names[i], CHILD_NAME_MAX_LEN, "Persona %d", i);
    }

    create_children(names, queues);

    for(int i=0;i<CHILD_COUNT;i++)
        if(mq_close(queues[i])) ERR("mq_close");

    while (wait(NULL) > 0);

    for(int i=0;i<CHILD_COUNT;i++)
        if(mq_unlink(queue_names[i])) ERR("mq_unlink");

    printf("Parens: Disceo.");
    return EXIT_SUCCESS;
}  