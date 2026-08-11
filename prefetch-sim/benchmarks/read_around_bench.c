#define _GNU_SOURCE

#include "lru_buffer_pool.h"
#include "read_around.h"
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

// Stage 3 benchmark: read-around against a "randomly distributed but
// localized" workload -- confined to a single hot region of region_width
// blocks (like Stage 1/2's benchmarks, this trace DOES revisit blocks,
// unlike interrupted_bench/adaptive_bench, see docs/stage_progression.md's
// "limits" section), generated as a bounded random walk with per-step
// jitter instead of i.i.d. uniform draws inside the region. That distinction
// matters: read-around's premise is "the next access lands near the current
// one" (e.g. page faults touching adjacent pages) -- i.i.d. uniform draws
// inside a region satisfy "confined to a narrow range" but NOT "next access
// near current access", so read-around would have nothing to exploit even
// though the trace looks localized at a glance. The walk's step size
// (jitter) is what read-around's radius is actually being compared against.
// Driven through LruBufferPool + ReadAround, independent of Stage 1/2's
// FIFO BufferPool.

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

// Picks one random region of region_width blocks within [0, total_blocks),
// then walks inside it: each step moves the current position by a random
// offset in [-jitter, +jitter], clamped back into the region if it would
// step outside. Unlike i.i.d. uniform draws, consecutive out_blocks[i] are
// spatially close by construction (within jitter blocks of each other),
// which is what gives read-around's neighbor-prefetch something real to
// exploit -- and gives the radius sweep a meaningful reference point
// (jitter) to compare against.
static void generate_locality_walk(size_t *out_blocks, size_t num_accesses,
                                    size_t total_blocks, size_t region_width,
                                    size_t jitter) {
    size_t region_start = (size_t)rand() % (total_blocks - region_width + 1);
    size_t region_end = region_start + region_width - 1; // inclusive

    size_t pos = region_start + (size_t)rand() % region_width;
    for (size_t i = 0; i < num_accesses; i++) {
        out_blocks[i] = pos;

        long step = (jitter == 0) ? 0 : ((long)(rand() % (2 * jitter + 1)) - (long)jitter);
        long next = (long)pos + step;
        if (next < (long)region_start) {
            next = (long)region_start;
        } else if (next > (long)region_end) {
            next = (long)region_end;
        }
        pos = (size_t)next;
    }
}

int main(int argc, char *argv[]) {
    if (argc != 10) {
        fprintf(stderr,
                "usage: %s <file_path> <file_size_bytes> <block_size> "
                "<capacity_blocks> <region_width> <jitter> <radius> <num_accesses> <repeats>\n",
                argv[0]);
        return 1;
    }

    const char *file_path = argv[1];
    size_t file_size = strtoull(argv[2], NULL, 10);
    size_t block_size = strtoull(argv[3], NULL, 10);
    size_t capacity_blocks = strtoull(argv[4], NULL, 10);
    size_t region_width = strtoull(argv[5], NULL, 10);
    size_t jitter = strtoull(argv[6], NULL, 10);
    size_t radius = strtoull(argv[7], NULL, 10);
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
    if (region_width == 0) {
        fprintf(stderr, "region_width must be >= 1\n");
        return 1;
    }

    if (prepare_test_file(file_path, file_size) != 0) {
        fprintf(stderr, "failed to prepare test file %s\n", file_path);
        return 1;
    }

    size_t total_blocks = file_size / block_size;
    if (region_width > total_blocks) {
        fprintf(stderr, "region_width must be <= total_blocks\n");
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
        generate_locality_walk(blocks, num_accesses, total_blocks, region_width, jitter);

        LruBufferPool pool;
        lru_buffer_pool_init(&pool, capacity_blocks, block_size);

        ReadAround ra;
        read_around_init(&ra, &pool, radius, total_blocks);

        Timer t;
        timer_start(&t);
        for (size_t i = 0; i < num_accesses; i++) {
            size_t block_num = blocks[i];

            if (!lru_buffer_pool_lookup(&pool, block_num)) {
                off_t off = (off_t)block_num * (off_t)block_size;
                ssize_t n = pread(fd, buf, block_size, off);
                if (n < 0) {
                    perror("pread");
                    lru_buffer_pool_destroy(&pool);
                    free(elapsed_samples);
                    free(hit_rate_samples);
                    free(buf);
                    close(fd);
                    free(blocks);
                    return 1;
                }
                lru_buffer_pool_insert(&pool, block_num);
            }

            read_around_on_access(&ra, block_num);
        }
        timer_stop(&t);

        elapsed_samples[r] = timer_elapsed_sec(&t);

        size_t hits = lru_buffer_pool_hits(&pool);
        size_t misses = lru_buffer_pool_misses(&pool);
        size_t total = hits + misses;
        hit_rate_samples[r] = (total > 0) ? (double)hits / (double)total : 0.0;

        lru_buffer_pool_destroy(&pool);
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

    printf("%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%.6f,%.6f,%.2f,%.4f\n",
           region_width, jitter, radius, capacity_blocks, file_size, block_size, num_accesses, repeats,
           mean_elapsed, stddev_elapsed, mean_throughput_mb_s, mean_hit_rate);

    free(elapsed_samples);
    free(hit_rate_samples);
    free(buf);
    close(fd);
    free(blocks);
    return 0;
}
