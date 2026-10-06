#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "resource.h"

typedef struct {
    const char *path;
    ResBudget *budget;
    pthread_mutex_t *mutex;
    pthread_cond_t *condition;
    int *start;
    int *finished;
    int *release;
    int opened;
    char error[256];
} Worker;

static void *open_and_hold(void *opaque) {
    Worker *worker = (Worker *)opaque;
    pthread_mutex_lock(worker->mutex);
    while (!*worker->start) pthread_cond_wait(worker->condition, worker->mutex);
    pthread_mutex_unlock(worker->mutex);

    ResFile file = {0};
    worker->opened = res_file_open(worker->path, worker->budget, &file,
                                   worker->error, sizeof worker->error);

    pthread_mutex_lock(worker->mutex);
    (*worker->finished)++;
    pthread_cond_broadcast(worker->condition);
    while (!*worker->release) pthread_cond_wait(worker->condition, worker->mutex);
    pthread_mutex_unlock(worker->mutex);
    if (worker->opened) res_file_close(&file);
    return NULL;
}

static void write_file(const char *path) {
    static const char bytes[] = "one-owner-only-buffer";
    FILE *file = fopen(path, "wb"); assert(file);
    assert(fwrite(bytes, 1, sizeof bytes, file) == sizeof bytes);
    assert(fclose(file) == 0);
}

int main(void) {
    char path[] = "/tmp/openug2-resource-concurrency-XXXXXX";
    int fd = mkstemp(path); assert(fd >= 0); assert(close(fd) == 0);
    write_file(path);

    ResBudget budget = {0};
    assert(res_budget_init(&budget, 4096, 128));
    ResFile probe = {0}; char error[256];
    assert(res_file_open(path, &budget, &probe, error, sizeof error));
    uint32_t exact_cost = budget.used;
    long payload_len = probe.len;
    res_file_close(&probe);
    assert(exact_cost > (uint32_t)payload_len);
    assert(res_budget_init(&budget, exact_cost, (uint32_t)payload_len));

    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
    int start = 0, finished = 0, release = 0;
    Worker workers[2] = {
        {path, &budget, &mutex, &condition, &start, &finished, &release, 0, {0}},
        {path, &budget, &mutex, &condition, &start, &finished, &release, 0, {0}}
    };
    pthread_t threads[2];
    assert(pthread_create(&threads[0], NULL, open_and_hold, &workers[0]) == 0);
    assert(pthread_create(&threads[1], NULL, open_and_hold, &workers[1]) == 0);

    pthread_mutex_lock(&mutex);
    start = 1; pthread_cond_broadcast(&condition);
    while (finished != 2) pthread_cond_wait(&condition, &mutex);
    pthread_mutex_unlock(&mutex);

    assert(workers[0].opened + workers[1].opened == 1);
    assert((workers[0].opened && strstr(workers[1].error, "budget")) ||
           (workers[1].opened && strstr(workers[0].error, "budget")));
    ResBudgetStats snapshot; res_budget_snapshot(&budget, &snapshot);
    assert(snapshot.used <= snapshot.limit && snapshot.used == exact_cost &&
           snapshot.peak == exact_cost && snapshot.live == 1);

    pthread_mutex_lock(&mutex);
    release = 1; pthread_cond_broadcast(&condition);
    pthread_mutex_unlock(&mutex);
    assert(pthread_join(threads[0], NULL) == 0);
    assert(pthread_join(threads[1], NULL) == 0);
    res_budget_snapshot(&budget, &snapshot);
    assert(snapshot.used == 0 && snapshot.live == 0 && snapshot.peak == exact_cost);

    assert(pthread_cond_destroy(&condition) == 0);
    assert(pthread_mutex_destroy(&mutex) == 0);
    assert(unlink(path) == 0);
    puts("resource_concurrency_test: PASS (two simultaneous opens, one budget slot)");
    return 0;
}
