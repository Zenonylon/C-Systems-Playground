/*
 * tools/mmaptest.c — read-around 지연 측정 드라이버
 *
 * 빌드 (호스트 WSL, 게스트로 복사할 정적 바이너리):
 *     gcc -O2 -static -o mmaptest mmaptest.c
 * 실행 (QEMU 게스트):
 *     taskset -c 1 ./mmaptest --samples 200 --seed 1 > out.csv
 *
 * 출력: stdout에 "페이지번호,걸린시간(ns)" CSV. 통계는 호스트에서 awk/python.
 *       (첫 3~5줄은 웜업 이상치라 버리고 계산)
 *
 * 샘플마다 open→mmap→fault→munmap→close 를 반복하는 이유:
 *   커널은 struct file 단위로 mmap_miss 카운터를 두고, MMAP_LOTSAMISS(100)를
 *   넘으면 read-around를 스킵한다. 무작위 콜드 접근은 거의 전부 miss라
 *   fd 하나를 재사용하면 ~100번째 이후 샘플은 read-around가 안 돈다.
 *   매 샘플 새 fd → f_ra.mmap_miss = 0 에서 시작 → 완전 격리.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
 
#define PAGE 4096UL
 
static long long ns_diff(struct timespec a, struct timespec b)
{
    return (b.tv_sec - a.tv_sec) * 1000000000LL + (b.tv_nsec - a.tv_nsec);
}
 
int main(int argc, char **argv)
{
    const char *path = "/mnt/raround/rnd.bin";
    long samples = 200;
    unsigned seed = 1;
 
    /* 1. 인자 파싱 */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--rnd-file") && i + 1 < argc)
            path = argv[++i];
        else if (!strcmp(argv[i], "--samples") && i + 1 < argc)
            samples = atol(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc)
            seed = (unsigned)atol(argv[++i]);
        else {
            fprintf(stderr, "usage: %s [--rnd-file F] [--samples N] [--seed S]\n", argv[0]);
            return 1;
        }
    }
 
    /* 리다이렉트 시에도 한 줄씩 flush (샘플 수 늘려도 안전) */
    setvbuf(stdout, NULL, _IOLBF, 0);
 
    /* 2. 파일 크기 확인 (열지 않고 stat만) */
    struct stat st;
    if (stat(path, &st) < 0) { perror("stat"); return 1; }
    size_t size   = (size_t)st.st_size;
    size_t npages = size / PAGE;
 
    fprintf(stderr, "file=%s size=%zu pages=%zu samples=%ld seed=%u\n",
            path, size, npages, samples, seed);
 
    /* 3. N번 반복: 매 샘플 새 fd/매핑으로 격리 */
    for (long i = 0; i < samples; i++) {
        size_t page = rand_r(&seed) % npages;
        struct timespec t0, t1;
 
        int fd = open(path, O_RDONLY);
        if (fd < 0) { perror("open"); return 1; }
 
        char *p = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (p == MAP_FAILED) { perror("mmap"); return 1; }
 
        clock_gettime(CLOCK_MONOTONIC, &t0);
        volatile char c = p[page * PAGE];      /* ← 여기서 major fault → read-around */
        clock_gettime(CLOCK_MONOTONIC, &t1);
        (void)c;
 
        printf("%zu,%lld\n", page, ns_diff(t0, t1));
 
        /* 매핑 해제 후 page cache 비우기 (cache는 inode 단위라 fd 재오픈만으론 안 빠짐) */
        munmap(p, size);
        posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
        close(fd);
    }
 
    return 0;
}
 