/* Public guest-libc regression for concurrent publication of one directory. */
#define _DARWIN_C_SOURCE 1
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define WORKER_COUNT 16
#define ROUND_COUNT 64

/* Darwin has no pthread_barrier_t. Including main in both phases keeps the
 * next mkdir phase closed until main has checked and removed the directory. */
static struct {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    unsigned waiting;
    unsigned generation;
    int stopped;
} barrier = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .condition = PTHREAD_COND_INITIALIZER
};

struct worker {
    pthread_t thread;
    int result;
    int error;
};

static char root[] = "/private/var/tmp/eunion-mkdir-race.XXXXXX";
static char child[sizeof(root) + sizeof("/child")];

static void sync_check(int error, const char *operation, unsigned round)
{
    if (error != 0) {
        fprintf(stderr, "FAIL eunion-mkdir-race round=%u %s errno=%d (%s)\n",
                round, operation, error, strerror(error));
        /* A broken synchronization primitive cannot safely release or join
         * its users. Never continue to a success oracle in that situation. */
        abort();
    }
}

static int barrier_wait(unsigned round)
{
    sync_check(pthread_mutex_lock(&barrier.mutex), "pthread_mutex_lock", round);
    unsigned generation = barrier.generation;
    if (!barrier.stopped && ++barrier.waiting == WORKER_COUNT + 1) {
        barrier.waiting = 0;
        ++barrier.generation;
        sync_check(pthread_cond_broadcast(&barrier.condition),
                   "pthread_cond_broadcast", round);
    } else {
        while (!barrier.stopped && generation == barrier.generation)
            sync_check(pthread_cond_wait(&barrier.condition, &barrier.mutex),
                       "pthread_cond_wait", round);
    }
    int proceed = !barrier.stopped;
    sync_check(pthread_mutex_unlock(&barrier.mutex), "pthread_mutex_unlock", round);
    return proceed;
}

static void barrier_stop(unsigned round)
{
    sync_check(pthread_mutex_lock(&barrier.mutex), "pthread_mutex_lock", round);
    barrier.stopped = 1;
    sync_check(pthread_cond_broadcast(&barrier.condition),
               "pthread_cond_broadcast", round);
    sync_check(pthread_mutex_unlock(&barrier.mutex), "pthread_mutex_unlock", round);
}

static void *mkdir_worker(void *argument)
{
    struct worker *worker = argument;
    for (unsigned round = 0; round < ROUND_COUNT; ++round) {
        if (!barrier_wait(round))
            break;
        errno = 0;
        worker->result = mkdir(child, 0700);
        worker->error = errno;
        if (!barrier_wait(round))
            break;
    }
    return NULL;
}

static int check_results(const struct worker *workers, unsigned round)
{
    unsigned successes = 0;
    int ok = 1;
    for (unsigned index = 0; index < WORKER_COUNT; ++index) {
        if (workers[index].result == 0) {
            ++successes;
        } else if (workers[index].result != -1 || workers[index].error != EEXIST) {
            fprintf(stderr,
                    "FAIL eunion-mkdir-race round=%u worker=%u mkdir result=%d "
                    "errno=%d (%s); expected -1/EEXIST or the sole success\n",
                    round, index, workers[index].result, workers[index].error,
                    strerror(workers[index].error));
            ok = 0;
        }
    }
    if (successes != 1) {
        fprintf(stderr,
                "FAIL eunion-mkdir-race round=%u successful mkdir calls=%u "
                "expected=1 errno=0\n", round, successes);
        ok = 0;
    }
    return ok;
}

/* Called only with every worker finished with this round's path. Even an
 * invalid non-directory result is removed from our private fixture root. */
static int check_and_remove_child(unsigned round)
{
    struct stat st;
    int result = lstat(child, &st);
    int error = result == -1 ? errno : 0;
    int directory = result == 0 && S_ISDIR(st.st_mode);
    int ok = directory;
    if (!directory) {
        fprintf(stderr,
                "FAIL eunion-mkdir-race round=%u resulting path is not a directory "
                "lstat=%d errno=%d (%s) path=%s\n",
                round, result, error, strerror(error), child);
    }
    if (result == -1 && error == ENOENT)
        return 0;
    result = result == 0 && !directory ? unlink(child) : rmdir(child);
    if (result != 0) {
        error = errno;
        fprintf(stderr,
                "FAIL eunion-mkdir-race round=%u remove child errno=%d (%s) path=%s\n",
                round, error, strerror(error), child);
        ok = 0;
    }
    return ok;
}

int main(void)
{
    struct worker workers[WORKER_COUNT];
    memset(workers, 0, sizeof(workers));
    unsigned created = 0;
    unsigned completed = 0;
    unsigned round = 0;
    int ok = 1;

    if (mkdtemp(root) == NULL) {
        int error = errno;
        fprintf(stderr, "FAIL eunion-mkdir-race round=0 mkdtemp errno=%d (%s)\n",
                error, strerror(error));
        return 1;
    }
    /* The capacity includes the complete fixed template and child suffix. */
    snprintf(child, sizeof(child), "%s/child", root);

    for (; created < WORKER_COUNT; ++created) {
        int error = pthread_create(&workers[created].thread, NULL,
                                   mkdir_worker, &workers[created]);
        if (error != 0) {
            fprintf(stderr,
                    "FAIL eunion-mkdir-race round=0 worker=%u pthread_create "
                    "errno=%d (%s)\n", created, error, strerror(error));
            ok = 0;
            break;
        }
    }

    if (created == WORKER_COUNT) {
        for (round = 0; round < ROUND_COUNT; ++round) {
            struct stat st;
            int result = lstat(child, &st);
            int error = result == -1 ? errno : 0;
            if (result != -1 || error != ENOENT) {
                fprintf(stderr,
                        "FAIL eunion-mkdir-race round=%u child must start missing "
                        "lstat=%d errno=%d (%s) path=%s\n",
                        round, result, error, strerror(error), child);
                if (result == 0)
                    check_and_remove_child(round);
                ok = 0;
                break;
            }
            barrier_wait(round);
            barrier_wait(round);
            int results_ok = check_results(workers, round);
            int directory_ok = check_and_remove_child(round);
            if (!results_ok || !directory_ok) {
                ok = 0;
                break;
            }
            ++completed;
        }
    }

    /* This also releases a partial worker set if pthread_create failed. No
     * worker can have called mkdir before all WORKER_COUNT workers and main
     * reached the first barrier. Do not cancel threads holding its mutex. */
    barrier_stop(round);
    for (unsigned index = 0; index < created; ++index) {
        int error = pthread_join(workers[index].thread, NULL);
        if (error != 0) {
            fprintf(stderr,
                    "FAIL eunion-mkdir-race round=%u worker=%u pthread_join "
                    "errno=%d (%s)\n", round, index, error, strerror(error));
            ok = 0;
        }
    }
    int error = pthread_cond_destroy(&barrier.condition);
    if (error != 0) {
        fprintf(stderr, "FAIL eunion-mkdir-race round=%u pthread_cond_destroy errno=%d (%s)\n",
                round, error, strerror(error));
        ok = 0;
    }
    error = pthread_mutex_destroy(&barrier.mutex);
    if (error != 0) {
        fprintf(stderr, "FAIL eunion-mkdir-race round=%u pthread_mutex_destroy errno=%d (%s)\n",
                round, error, strerror(error));
        ok = 0;
    }
    if (rmdir(root) != 0) {
        error = errno;
        fprintf(stderr, "FAIL eunion-mkdir-race round=%u remove root errno=%d (%s) path=%s\n",
                round, error, strerror(error), root);
        ok = 0;
    }
    if (!ok || completed != ROUND_COUNT)
        return 1;
    puts("EUNION_MKDIR_RACE_GUEST_OK");
    return 0;
}
