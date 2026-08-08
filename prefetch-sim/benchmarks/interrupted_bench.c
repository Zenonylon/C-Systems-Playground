#define _GNU_SOURCE

#include "buffer_pool.h"
#include "readahead.h"
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

// Stage 2 motivation probe: unlike stage1_bench (pure sequential / pure
// random for the whole run), this drives the Stage 1 fixed-window
// read-ahead over a trace made of short sequential *bursts* separated by
// random jumps -- burst_len controls how long each sequential run is
// before the access pattern jumps away. This exercises the case
// stage1_bench never does: a fixed window that's larger than the run of
// sequential hits it's covering.
//
// Reuses buffer_pool/readahead exactly as-is (no changes to src/); only the
// offset sequence generation is new, and lives here rather than in
// workload_gen.c since it composes bursts+jumps rather than being a single
// global access pattern.

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

// Fills out_blocks[0..num_accesses-1] with block numbers made of back-to-back
// sequential runs of length burst_len, each run starting at a fresh random
// block. The last run in a fill is truncated if it would overrun
// num_accesses.
static void generate_interrupted_sequence(size_t *out_blocks, size_t num_accesses,
                                           size_t total_blocks, size_t burst_len) {
    size_t i = 0;
    while (i < num_accesses) {
        size_t start = (size_t)rand() % (total_blocks - burst_len);
        size_t run = burst_len;
        if (i + run > num_accesses) {
            run = num_accesses - i;
        }
        for (size_t j = 0; j < run; j++) {
            out_blocks[i++] = start + j;
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 9) {
        fprintf(stderr,
                "usage: %s <file_path> <file_size_bytes> <block_size> "
                "<capacity_blocks> <window> <burst_len> <num_accesses> <repeats>\n",
                argv[0]);
        return 1;
    }

    const char *file_path = argv[1];
    size_t file_size = strtoull(argv[2], NULL, 10);
    size_t block_size = strtoull(argv[3], NULL, 10);
    size_t capacity_blocks = strtoull(argv[4], NULL, 10);
    size_t window = strtoull(argv[5], NULL, 10);
    size_t burst_len = strtoull(argv[6], NULL, 10);
    size_t num_accesses = strtoull(argv[7], NULL, 10);
    size_t repeats = strtoull(argv[8], NULL, 10);

    if (repeats == 0) {
        fprintf(stderr, "repeats must be >= 1\n");
        return 1;
    }
    if (capacity_blocks == 0) {
        fprintf(stderr, "capacity_blocks must be >= 1\n");
        return 1;
    }
    if (burst_len == 0) {
        fprintf(stderr, "burst_len must be >= 1\n");
        return 1;
    }

    if (prepare_test_file(file_path, file_size) != 0) {
        fprintf(stderr, "failed to prepare test file %s\n", file_path);
        return 1;
    }

    size_t total_blocks = file_size / block_size;
    if (burst_len >= total_blocks) {
        fprintf(stderr, "burst_len must be smaller than total_blocks\n");
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

    double *elapsed_samples = malloc(repeats * sizeof(double));
    double *hit_rate_samples = malloc(repeats * sizeof(double));
    if (elapsed_samples == NULL || hit_rate_samples == NULL) {
        fprintf(stderr, "out of memory\n");
        free(elapsed_samples);
        free(hit_rate_samples);
        free(buf);
        close(fd);
        free(blocks);
        return 1;
    }

    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    for (size_t r = 0; r < repeats; r++) {
        generate_interrupted_sequence(blocks, num_accesses, total_blocks, burst_len);

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
                    free(elapsed_samples);
                    free(hit_rate_samples);
                    free(buf);
                    close(fd);
                    free(blocks);
                    return 1;
                }
                buffer_pool_insert(&pool, block_num);
            }

            readahead_on_access(&ra, block_num);
        }
        timer_stop(&t);

        elapsed_samples[r] = timer_elapsed_sec(&t);

        size_t hits = buffer_pool_hits(&pool);
        size_t misses = buffer_pool_misses(&pool);
        size_t total = hits + misses;
        hit_rate_samples[r] = (total > 0) ? (double)hits / (double)total : 0.0;

        buffer_pool_destroy(&pool);
    }

    double sum = 0.0;
    for (size_t r = 0; r < repeats; r++) {
        sum += elapsed_samples[r];
    }
    double mean_elapsed = sum / (double)repeats;

    double sq_diff_sum = 0.0;
    for (size_t r = 0; r < repeats; r++) {
        double diff = elapsed_samples[r] - mean_elapsed;
        sq_diff_sum += diff * diff;
    }
    double stddev_elapsed = (repeats > 1) ? sqrt(sq_diff_sum / (double)(repeats - 1)) : 0.0;

    double hit_rate_sum = 0.0;
    for (size_t r = 0; r < repeats; r++) {
        hit_rate_sum += hit_rate_samples[r];
    }
    double mean_hit_rate = hit_rate_sum / (double)repeats;

    double total_mb = (double)(num_accesses * block_size) / (1024.0 * 1024.0);
    double mean_throughput_mb_s = mean_elapsed > 0.0 ? total_mb / mean_elapsed : 0.0;

    printf("%zu,%zu,%zu,%zu,%zu,%zu,%zu,%.6f,%.6f,%.2f,%.4f\n",
           burst_len, window, capacity_blocks, file_size, block_size, num_accesses, repeats,
           mean_elapsed, stddev_elapsed, mean_throughput_mb_s, mean_hit_rate);

    free(elapsed_samples);
    free(hit_rate_samples);
    free(buf);
    close(fd);
    free(blocks);
    return 0;
}
