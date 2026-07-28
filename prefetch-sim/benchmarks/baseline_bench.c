#define _GNU_SOURCE

#include "timer.h"
#include "workload_gen.h"

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ALIGNMENT 4096

// Fills file_path with file_size bytes if it isn't already that size.
// Reused across the sequential/random/direct sweep in baseline_bench.sh.
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

int main(int argc, char *argv[]) {
    if (argc != 7) {
        fprintf(stderr,
                "usage: %s <file_path> <file_size_bytes> <block_size> "
                "<sequential|random> <direct:0|1> <repeats>\n",
                argv[0]);
        return 1;
    }

    const char *file_path = argv[1];
    size_t file_size = strtoull(argv[2], NULL, 10);
    size_t block_size = strtoull(argv[3], NULL, 10);
    const char *pattern_str = argv[4];
    int direct = atoi(argv[5]);
    size_t repeats = strtoull(argv[6], NULL, 10);

    if (repeats == 0) {
        fprintf(stderr, "repeats must be >= 1\n");
        return 1;
    }

    AccessPattern pattern;
    if (strcmp(pattern_str, "sequential") == 0) {
        pattern = PATTERN_SEQUENTIAL;
    } else if (strcmp(pattern_str, "random") == 0) {
        pattern = PATTERN_RANDOM;
    } else {
        fprintf(stderr, "unknown pattern: %s\n", pattern_str);
        return 1;
    }

    if (prepare_test_file(file_path, file_size) != 0) {
        fprintf(stderr, "failed to prepare test file %s\n", file_path);
        return 1;
    }

    size_t num_blocks = file_size / block_size;
    size_t *offsets = malloc(num_blocks * sizeof(size_t));
    if (offsets == NULL) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    int flags = O_RDONLY | (direct ? O_DIRECT : 0);
    int fd = open(file_path, flags);
    if (fd < 0) {
        perror("open");
        free(offsets);
        return 1;
    }

    void *buf = NULL;
    if (posix_memalign(&buf, ALIGNMENT, block_size) != 0) {
        fprintf(stderr, "posix_memalign failed\n");
        close(fd);
        free(offsets);
        return 1;
    }

    double *elapsed_samples = malloc(repeats * sizeof(double));
    if (elapsed_samples == NULL) {
        fprintf(stderr, "out of memory\n");
        free(buf);
        close(fd);
        free(offsets);
        return 1;
    }

    for (size_t r = 0; r < repeats; r++) {
        // Regenerate offsets each repeat so a "random" run isn't just
        // replaying the same one shuffled order every time.
        if (generate_offsets(pattern, file_size, block_size, num_blocks, offsets) != WORKLOAD_OK) {
            fprintf(stderr, "generate_offsets failed\n");
            free(elapsed_samples);
            free(buf);
            close(fd);
            free(offsets);
            return 1;
        }

        Timer t;
        timer_start(&t);
        for (size_t i = 0; i < num_blocks; i++) {
            ssize_t n = pread(fd, buf, block_size, (off_t)offsets[i]);
            if (n < 0) {
                perror("pread");
                free(elapsed_samples);
                free(buf);
                close(fd);
                free(offsets);
                return 1;
            }
        }
        timer_stop(&t);

        elapsed_samples[r] = timer_elapsed_sec(&t);
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

    double total_mb = (double)(num_blocks * block_size) / (1024.0 * 1024.0);
    double mean_throughput_mb_s = mean_elapsed > 0.0 ? total_mb / mean_elapsed : 0.0;

    printf("%s,%d,%zu,%zu,%zu,%.6f,%.6f,%.2f\n",
           pattern_str, direct, file_size, block_size, repeats,
           mean_elapsed, stddev_elapsed, mean_throughput_mb_s);

    free(elapsed_samples);
    free(buf);
    close(fd);
    free(offsets);
    return 0;
}
