#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define TARGET_HOST "127.0.0.1"
#define TARGET_PORT 12345
#define BENCH_COUNT 10
#define BENCHMARK_RESULT_FILE "bench.txt"

/* length of unique message (TODO below) should shorter than this */
#define MAX_MSG_LEN 32

#define MAX_THREAD 1000

static const char *msg_dum = "dummy message";

static pthread_t pt[MAX_THREAD];
static int n_retry;

static pthread_mutex_t worker_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t worker_wait = PTHREAD_COND_INITIALIZER;

static inline long time_diff_us(struct timeval *start, struct timeval *end)
{
    return ((end->tv_sec - start->tv_sec) * 1000000) +
           (end->tv_usec - start->tv_usec);
}

static void *bench_worker(__attribute__((unused)))
{
    int sock_fd;
    char dummy[MAX_MSG_LEN];
    struct timeval start, end;

    /* wait until all workers created */
    pthread_mutex_lock(&worker_lock);
    if (++n_retry == MAX_THREAD) {
        pthread_cond_broadcast(&worker_wait);
    } else {
        while (n_retry < MAX_THREAD) {
            if (pthread_cond_wait(&worker_wait, &worker_lock)) {
                puts("pthread_cond_wait failed");
                exit(-1);
            }
        }
    }
    pthread_mutex_unlock(&worker_lock);
    /* all workers are ready, let's start bombing the server */

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        perror("socket");
        exit(-1);
    }
    printf("Socket created.\n");

    struct sockaddr_in info = {
        .sin_family = PF_INET,
        .sin_addr.s_addr = inet_addr(TARGET_HOST),
        .sin_port = htons(TARGET_PORT),
    };

    if (connect(sock_fd, (struct sockaddr *) &info, sizeof(info)) == -1) {
        perror("connect");
        exit(-1);
    }
    printf("Connected.\n");

    gettimeofday(&start, NULL);

    printf("Sending....\n");
    send(sock_fd, msg_dum, strlen(msg_dum), 0);
    printf("Finish sending!\n");

    printf("Recving....\n");
    recv(sock_fd, dummy, MAX_MSG_LEN, 0);
    printf("Finish recving!\n");

    gettimeofday(&end, NULL);

    shutdown(sock_fd, SHUT_RDWR);
    printf("SHUTDOWN.\n");
    close(sock_fd);

    if (strncmp(msg_dum, dummy, strlen(msg_dum))) {
        puts("echo message validation failed");
        exit(-1);
    }

    long elapsed_time = time_diff_us(&start, &end);

    FILE *bench_fd = fopen(BENCHMARK_RESULT_FILE, "a");
    if (!bench_fd) {
        perror("fopen");
        exit(-1);
    }
    fprintf(bench_fd, "Thread %ld %ld\n", pthread_self(),
            elapsed_time /= BENCH_COUNT);
    fclose(bench_fd);

    pthread_exit(NULL);
}

static void create_worker(int thread_qty)
{
    for (int i = 0; i < thread_qty; i++) {
        if (pthread_create(&pt[i], NULL, bench_worker, NULL)) {
            puts("thread creation failed");
            exit(-1);
        }
    }
}

static void bench(void)
{
    for (int i = 0; i < BENCH_COUNT; i++) {
        n_retry = 0;

        create_worker(MAX_THREAD);

        for (int x = 0; x < MAX_THREAD; x++)
            pthread_join(pt[x], NULL);
    }
}

int main(void)
{
    // Ensure the file is empty before starting the benchmark
    FILE *bench_fd = fopen(BENCHMARK_RESULT_FILE, "w");
    if (!bench_fd) {
        perror("fopen");
        return -1;
    }
    fclose(bench_fd);

    bench();

    return 0;
}
