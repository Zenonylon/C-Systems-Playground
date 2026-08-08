#define _GNU_SOURCE

#include "buffer_pool.h"
#include "readahead.h"
#include "adaptive_readahead.h"
#include "timer.h"

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define ALIGNMENT 4096

// Stage 1 vs Stage 2 head-to-head: interrupted_bench.c and adaptive_bench.c
// each sweep a single FIXED burst_len per run, so a Stage 1 window tuned
// for that run's burst_len never gets to look bad. This generator instead
// draws each burst's length at random from a mixed set, so a single trace
// contains both short and long sequential runs -- the case where a fixed
// window has to be a compromise. The identical trace (same repeat, same
// blocks[]) is then replayed through both a Stage 1 fixed-window Readahead
// and a Stage 2 AdaptiveReadahead so the two are compared on exactly the
// same accesses, not on separately-sampled traces.

static const size_t kBurstLens[] = {4, 16, 64, 256};
static const size_t kNumBurstLens = sizeof(kBurstLens) / sizeof(kBurstLens[0]);

static int prepare_test_file(const char *path, size_t file_size) {
    int fd = open(path, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) == 0 && (size_t)st.st_size == file_size) {
        close(fd);
        return 0;
    }

    char *buf = malloc(ALIGNMENT);
    if (buf == NULL) {
        close(fd);
        return -1;
    }
    memset(buf, 0xAB, ALIGNMENT);

    size_t written = 0;
    while (written < file_size) {
        size_t chunk = (file_size - written < ALIGNMENT) ? (file_size - written) : ALIGNMENT;
        ssize_t n = write(fd, buf, chunk);
        if (n <= 0) {
            free(buf);
            close(fd);
            return -1;
        }
        written += (size_t)n;
    }
    free(buf);

    fsync(fd);
    close(fd);
    return 0;
}

// Fills out_blocks[0..num_accesses-1] with back-to-back sequential runs
// whose length is drawn per-burst from kBurstLens (uniformly at random),
// each run starting at a fresh random block. The last run in a fill is
// truncated if it would overrun num_accesses.
static void generate_mixed_sequence(size_t *out_blocks, size_t num_accesses,
                                     size_t total_blocks) {
    size_t max_burst = kBurstLens[kNumBurstLens - 1];
    size_t i = 0;
    while (i < num_accesses) {
        size_t burst_len = kBurstLens[(size_t)rand() % kNumBurstLens];
        size_t start = (size_t)rand() % (total_blocks - max_burst);
        size_t run = burst_len;
        if (i + run > num_accesses) {
            run = num_accesses - i;
        }
        for (size_t j = 0; j < run; j++) {
            out_blocks[i++] = start + j;
        }
    }
}

static int run_fixed(int fd, void *buf, size_t block_size, size_t capacity_blocks,
                      size_t window, const size_t *blocks, size_t num_accesses,
                      double *out_elapsed, double *out_hit_rate) {
    BufferPool pool;
    buffer_pool_init(&pool, capacity_blocks, block_size);

    Readahead ra;
    readahead_init(&ra, &pool, window);

    Timer t;
    timer_start(&t);
    for (size_t i = 0; i < num_accesses; i++) {
        size_t block_num = blocks[i];

        if (!buffer_pool_lookup(&pool, block_num)) {
            off_t off = (off_t)block_num * (off_t)block_size;
            ssize_t n = pread(fd, buf, block_size, off);
            if (n < 0) {
                perror("pread");
                buffer_pool_destroy(&pool);
                return -1;
            }
            buffer_pool_insert(&pool, block_num);
        }

        readahead_on_access(&ra, block_num);
    }
    timer_stop(&t);

    *out_elapsed = timer_elapsed_sec(&t);
    size_t hits = buffer_pool_hits(&pool);
    size_t misses = buffer_pool_misses(&pool);
    size_t total = hits + misses;
    *out_hit_rate = (total > 0) ? (double)hits / (double)total : 0.0;

    buffer_pool_destroy(&pool);
    return 0;
}

static int run_adaptive(int fd, void *buf, size_t block_size, size_t capacity_blocks,
                         size_t min_window, size_t max_window, const size_t *blocks,
                         size_t num_accesses, double *out_elapsed, double *out_hit_rate) {
    BufferPool pool;
    buffer_pool_init(&pool, capacity_blocks, block_size);

    AdaptiveReadahead ra;
    adaptive_readahead_init(&ra, &pool, min_window, max_window);

    Timer t;
    timer_start(&t);
    for (size_t i = 0; i < num_accesses; i++) {
        size_t block_num = blocks[i];

        if (!buffer_pool_lookup(&pool, block_num)) {
            off_t off = (off_t)block_num * (off_t)block_size;
            ssize_t n = pread(fd, buf, block_size, off);
            if (n < 0) {
                perror("pread");
                buffer_pool_destroy(&pool);
                return -1;
            }
            buffer_pool_insert(&pool, block_num);
        }

        adaptive_readahead_on_access(&ra, block_num);
    }
    timer_stop(&t);

    *out_elapsed = timer_elapsed_sec(&t);
    size_t hits = buffer_pool_hits(&pool);
    size_t misses = buffer_pool_misses(&pool);
    size_t total = hits + misses;
    *out_hit_rate = (total > 0) ? (double)hits / (double)total : 0.0;

    buffer_pool_destroy(&pool);
    return 0;
}

static double mean_of(const double *samples, size_t n) {
    double sum = 0.0;
    for (size_t i = 0; i < n; i++) {
        sum += samples[i];
    }
    return sum / (double)n;
}

static double stddev_of(const double *samples, size_t n, double mean) {
    if (n <= 1) {
        return 0.0;
    }
    double sq_diff_sum = 0.0;
    for (size_t i = 0; i < n; i++) {
        double diff = samples[i] - mean;
        sq_diff_sum += diff * diff;
    }
    return sqrt(sq_diff_sum / (double)(n - 1));
}

int main(int argc, char *argv[]) {
    if (argc != 10) {
        fprintf(stderr,
                "usage: %s <file_path> <file_size_bytes> <block_size> "
                "<capacity_blocks> <fixed_window> <min_window> <max_window> "
                "<num_accesses> <repeats>\n",
                argv[0]);
        return 1;
    }

    const char *file_path = argv[1];
    size_t file_size = strtoull(argv[2], NULL, 10);
    size_t block_size = strtoull(argv[3], NULL, 10);
    size_t capacity_blocks = strtoull(argv[4], NULL, 10);
    size_t fixed_window = strtoull(argv[5], NULL, 10);
    size_t min_window = strtoull(argv[6], NULL, 10);
    size_t max_window = strtoull(argv[7], NULL, 10);
    size_t num_accesses = strtoull(argv[8], NULL, 10);
    size_t repeats = strtoull(argv[9], NULL, 10);

    if (repeats == 0) {
        fprintf(stderr, "repeats must be >= 1\n");
        return 1;
    }
    if (capacity_blocks == 0) {
        fprintf(stderr, "capacity_blocks must be >= 1\n");
        return 1;
    }
    if (max_window < min_window) {
        fprintf(stderr, "max_window must be >= min_window\n");
        return 1;
    }

    if (prepare_test_file(file_path, file_size) != 0) {
        fprintf(stderr, "failed to prepare test file %s\n", file_path);
        return 1;
    }

    size_t total_blocks = file_size / block_size;
    size_t max_burst = kBurstLens[kNumBurstLens - 1];
    if (max_burst >= total_blocks) {
        fprintf(stderr, "largest burst length must be smaller than total_blocks\n");
        return 1;
    }

    size_t *blocks = malloc(num_accesses * sizeof(size_t));
    if (blocks == NULL) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    int fd = open(file_path, O_RDONLY | O_DIRECT);
    if (fd < 0) {
        perror("open");
        free(blocks);
        return 1;
    }

    void *buf = NULL;
    if (posix_memalign(&buf, ALIGNMENT, block_size) != 0) {
        fprintf(stderr, "posix_memalign failed\n");
        close(fd);
        free(blocks);
        return 1;
    }

    double *fixed_elapsed = malloc(repeats * sizeof(double));
    double *fixed_hit_rate = malloc(repeats * sizeof(double));
    double *adaptive_elapsed = malloc(repeats * sizeof(double));
    double *adaptive_hit_rate = malloc(repeats * sizeof(double));
    if (fixed_elapsed == NULL || fixed_hit_rate == NULL ||
        adaptive_elapsed == NULL || adaptive_hit_rate == NULL) {
        fprintf(stderr, "out of memory\n");
        free(fixed_elapsed);
        free(fixed_hit_rate);
        free(adaptive_elapsed);
        free(adaptive_hit_rate);
        free(buf);
        close(fd);
        free(blocks);
        return 1;
    }

    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    for (size_t r = 0; r < repeats; r++) {
        generate_mixed_sequence(blocks, num_accesses, total_blocks);

        if (run_fixed(fd, buf, block_size, capacity_blocks, fixed_window,
                      blocks, num_accesses, &fixed_elapsed[r], &fixed_hit_rate[r]) != 0 ||
            run_adaptive(fd, buf, block_size, capacity_blocks, min_window, max_window,
                         blocks, num_accesses, &adaptive_elapsed[r], &adaptive_hit_rate[r]) != 0) {
            free(fixed_elapsed);
            free(fixed_hit_rate);
            free(adaptive_elapsed);
            free(adaptive_hit_rate);
            free(buf);
            close(fd);
            free(blocks);
            return 1;
        }
    }

    double fixed_mean_elapsed = mean_of(fixed_elapsed, repeats);
    double fixed_stddev_elapsed = stddev_of(fixed_elapsed, repeats, fixed_mean_elapsed);
    double fixed_mean_hit_rate = mean_of(fixed_hit_rate, repeats);

    double adaptive_mean_elapsed = mean_of(adaptive_elapsed, repeats);
    double adaptive_stddev_elapsed = stddev_of(adaptive_elapsed, repeats, adaptive_mean_elapsed);
    double adaptive_mean_hit_rate = mean_of(adaptive_hit_rate, repeats);

    double total_mb = (double)(num_accesses * block_size) / (1024.0 * 1024.0);
    double fixed_mean_throughput = fixed_mean_elapsed > 0.0 ? total_mb / fixed_mean_elapsed : 0.0;
    double adaptive_mean_throughput = adaptive_mean_elapsed > 0.0 ? total_mb / adaptive_mean_elapsed : 0.0;

    printf("%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,"
           "%.6f,%.6f,%.2f,%.4f,"
           "%.6f,%.6f,%.2f,%.4f\n",
           capacity_blocks, fixed_window, min_window, max_window,
           file_size, block_size, num_accesses, repeats,
           fixed_mean_elapsed, fixed_stddev_elapsed, fixed_mean_throughput, fixed_mean_hit_rate,
           adaptive_mean_elapsed, adaptive_stddev_elapsed, adaptive_mean_throughput, adaptive_mean_hit_rate);

    free(fixed_elapsed);
    free(fixed_hit_rate);
    free(adaptive_elapsed);
    free(adaptive_hit_rate);
    free(buf);
    close(fd);
    free(blocks);
    return 0;
}
