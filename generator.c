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
#define Q1_CHARS_COUNT 3
#define Q2_CHARS_COUNT 5
#define BUF_SIZE 64
#define MQ_COUNT 2

void usage(void)
{
    printf("USAGE: \n");
    exit(EXIT_FAILURE);
}

volatile sig_atomic_t exitApp = 0;

int sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        return -1;
    return 0;
}

void sigint_handler(int sig)
{
    exitApp = 1;
}

typedef struct generatorArguments_t
{
    int t;
    int p;
    char **mq_names;      // names
    mqd_t *mq_ds;         // descriptors
    struct mq_attr *attr; // attributes
} generatorArguments_t;

void sendMessages(generatorArguments_t *arg, char **msgs, int n)
{
    for (int i = 0; i < n; i++)
    {
        int pid = getpid();

        sprintf(msgs[i], "%d/", pid);
        int len = strlen(msgs[i]);
        for (int j = 0; j < Q1_CHARS_COUNT; j++)
            msgs[i][len + j] = rand() % ('z' - 'a' + 1) + 'a';

        msgs[i][len + Q1_CHARS_COUNT] = '\0';

        printf("Sending to Q1: %s\n", msgs[i]);
        // send to q1 n messages
        if (mq_send(arg->mq_ds[0], msgs[i], BUF_SIZE, 0) == -1)
            ERR("mq_send");
    }
}

void workWithNArgument(generatorArguments_t *arg, char **msgs, int n)
{
    // if there is 'n' parameter, create q1 and q2 queues (I assumed, that if queue exists,
    // don't do anything with it)
    for (int i = 0; i < MQ_COUNT; i++)
    {
        arg->mq_ds[i] = mq_open(arg->mq_names[i], O_RDWR | O_CREAT, 0666, arg->attr);

        if (arg->mq_ds[i] == -1)
            ERR("mq_open");
    }

    // send n messages with content "<PID>/<3 random chars from 'a' to 'b'>""
    sendMessages(arg, msgs, n);
}

void workWithoutNArgument(generatorArguments_t *arg)
{
    for (int i = 0; i < 2; i++)
    {
        arg->mq_ds[i] = mq_open(arg->mq_names[i], O_RDWR, 0666, &arg->attr);
        if (arg->mq_ds[i] == -1)
        {
            if (errno == ENOENT) // mq doesn't exist
            {
                printf("Kolejka %s nie istnieje\n", arg->mq_names[i]);
                exit(EXIT_FAILURE);
            }
            else
                ERR("mq_open");
        }
    }
}

void generatorMainWork(generatorArguments_t *arg)
{
    char receivedMsg[BUF_SIZE];
    char outMsg[BUF_SIZE];

    if (mq_receive(arg->mq_ds[0], receivedMsg, BUF_SIZE, NULL) == -1)
        ERR("mq_receive");

    printf("Generator received %s\n", receivedMsg);
    sleep(arg->t);

    if (rand() % (100) < arg->p) // write to q2 with priority 1
    {
        int pid = getpid();
        sprintf(outMsg, "%d/", pid);
        int len = strlen(outMsg);
        for (int i = 0; i < Q1_CHARS_COUNT; i++)
            outMsg[len + i] = receivedMsg[strlen(receivedMsg) - Q1_CHARS_COUNT + i];

        outMsg[len + Q1_CHARS_COUNT] = '/';
        len = len + Q1_CHARS_COUNT + 1;
        for (int i = 0; i < Q2_CHARS_COUNT; i++)
            outMsg[len + i] = rand() % ('z' - 'a' + 1) + 'a';

        outMsg[len + Q2_CHARS_COUNT] = '\0';

        printf("Sending to Q2: %s\n", outMsg);
        if (mq_send(arg->mq_ds[1], outMsg, BUF_SIZE, 1) == -1)
            ERR("mq_send");
    }

    // write to q1 received message
    if (mq_send(arg->mq_ds[0], receivedMsg, BUF_SIZE, 0) == -1)
        ERR("mq_send");
}

int main(int argc, char *argv[])
{
    srand(time(NULL));
    if (argc < 5)
        usage();

    int t = atoi(argv[1]);
    int p = atoi(argv[2]);

    char *names[MQ_COUNT];
    names[0] = argv[3];
    names[1] = argv[4];

    int n = -1;
    if (argc == 6)
        n = atoi((argv[5]));

    if (sethandler(sigint_handler, SIGINT) == -1)
        ERR("sethandler");

    char **msgs = NULL;

    struct mq_attr attr;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = BUF_SIZE;
    mqd_t ds[MQ_COUNT];

    generatorArguments_t generatorArgs = {
        .attr = &attr,
        .mq_ds = ds,
        .mq_names = names,
        .p = p,
        .t = t};

    if (n != -1)
    {
        if ((msgs = (char **)malloc(sizeof(char *) * n)) == NULL)
            ERR("malloc");
        for (int i = 0; i < n; i++)
            if ((msgs[i] = (char *)malloc(sizeof(char) * BUF_SIZE)) == NULL)
                ERR("malloc");

        workWithNArgument(&generatorArgs, msgs, n);
    }
    else
        workWithoutNArgument(&generatorArgs);

    for (;;)
    {
        if (exitApp == 1)
            break;
        generatorMainWork(&generatorArgs);
    }

    if (mq_close(ds[0]))
        ERR("mq_close");
    if (mq_close(ds[1]))
        ERR("mq_close");

    if (msgs != NULL)
    {
        free(msgs);
        for (int i = 0; i < n; i++)
            free(msgs[i]);
    }

    return EXIT_SUCCESS;
}