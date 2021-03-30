// Oświadczam, że niniejsza praca stanowiąca podstawę do uznania osiągnięcia efektów uczenia się z przedmiotu SOP2
// została wykonana przeze mnie samodzielnie. [Jan Szablanowski] [305893]

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <mqueue.h>
#include <pthread.h>

#define ERR(source) (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), \
                     perror(source),                                 \
                     exit(EXIT_FAILURE))
#define MAX_PID_SIZE 5
#define CHARS_SEND 3
#define MAX_BUF 128

void usage(void)
{
    printf("USAGE: t[1,10] p[0,100] q2[string]\n");
    exit(EXIT_FAILURE);
}

typedef struct processorArguments_t
{
    mqd_t q2_ds;
    int p;
    int t;
    char *bufIn;
    char *bufOut;
    int msg_size;
} processorArguments_t;

volatile sig_atomic_t exitApp = 0;

int sethandler(void (*f)(int, siginfo_t *, void *), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_sigaction = f;
    act.sa_flags = SA_SIGINFO;
    if (-1 == sigaction(sigNo, &act, NULL))
        return -1;
    return 0;
}

void sigint_handler(int sig, siginfo_t *info, void *p)
{
    exitApp = 1;
}

void openMQ(mqd_t *ds, char *name)
{
    if ((*ds = TEMP_FAILURE_RETRY(mq_open(name, O_RDWR))) == -1)
    {
        if (errno == ENOENT) // message queue doesn't exist
        {
            printf("Message queue doesn't exist\n");
            exit(EXIT_FAILURE);
        }
        else
            ERR("mq_open");
    }
}

void sendMessage(processorArguments_t *arg)
{
    int pid = getpid();

    snprintf(arg->bufOut, 64, "%d/000/", pid);
    int bufOut_len = strlen(arg->bufOut);
    int bufIn_len = strlen(arg->bufIn);
    for (int i = 4; i >= 0; i--)
        arg->bufOut[bufOut_len + i] = arg->bufIn[bufIn_len - 1 - (4 - i)];
    arg->bufOut[bufOut_len + 5] = '\0';

    if (rand() % 100 < arg->p)
    {
        printf("Processor sending: %s\n", arg->bufOut);
        if (TEMP_FAILURE_RETRY(mq_send(arg->q2_ds, arg->bufOut, arg->msg_size, 0)) == -1)
            ERR("mq_send");
    }
}

void processorWork(processorArguments_t *arg)
{
    struct timespec ts;

    // time since UNIX epoch !!!
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 1;
    int timeout = 0;

    for (;;)
    {
        if (exitApp == 1)
            break;

        if (mq_timedreceive(arg->q2_ds, arg->bufIn, arg->msg_size, NULL, &ts) == -1)
        {
            if (errno == ETIMEDOUT)
            {
                if (timeout == 0)
                    timeout = 1;
                if (timeout == 1)
                {
                    printf("Timeout! ");
                    printf("Previous message: %s\n", arg->bufIn);
                    clock_gettime(CLOCK_REALTIME, &ts);
                    ts.tv_sec += arg->t;
                    continue;
                }
            }
            else if (errno == EINTR)
            {
                continue;
            }
            else
                ERR("mq_receive");
        }

        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;
        timeout = 0;

        struct timespec ts_sleep = {.tv_nsec = 0, .tv_sec = arg->t};
        struct timespec rts_sleep = {0, 0};
        do
        {
            if(nanosleep(&ts_sleep, &rts_sleep) < 0)
            {
                if (errno == EINTR && exitApp == 1) // exit
                    break;
            }
            rts_sleep = ts_sleep;
        } while (rts_sleep.tv_nsec == 0 && rts_sleep.tv_sec == 0);
        

        if (strlen(arg->bufIn) > 0)
            printf("Processor received: %s\n", arg->bufIn);

        sendMessage(arg);
    }
}

int main(int argc, char *argv[])
{
    srand(time(NULL));
    if (argc != 4)
        usage();

    int t = atoi(argv[1]);
    int p = atoi(argv[2]);
    char *q2_name = argv[3];

    mqd_t q2_ds;

    openMQ(&q2_ds, q2_name);

    struct mq_attr attr;
    if (mq_getattr(q2_ds, &attr) == -1)
        ERR("mq_getattr");
    int msg_size = attr.mq_msgsize;

    char *bufIn = (char *)malloc(sizeof(char) * msg_size);
    if (bufIn == NULL)
        ERR("malloc");
    char *bufOut = malloc(msg_size * sizeof(char));
    if (bufOut == NULL)
        ERR("malloc");

    if (sethandler(sigint_handler, SIGINT))
        ERR("sethandler");

    processorArguments_t processorArgs = {
        .bufIn = bufIn,
        .bufOut = bufOut,
        .msg_size = msg_size,
        .p = p,
        .t = t,
        .q2_ds = q2_ds};

    processorWork(&processorArgs);

    if (mq_close(q2_ds))
        ERR("mq_close");

    free(bufIn);
    free(bufOut);

    return EXIT_SUCCESS;
}