# readaround-split

Splitting the mmap read-around I/O path (`do_sync_mmap_readahead()` in
`mm/filemap.c`) so that read-around requests can be issued/merged
independently of sequential read-ahead, and measuring the effect against a
real, bootable kernel — not just read excerpts.

- [documents/environment.md](documents/environment.md) — QEMU/build/guest
  setup, and the edit -> sync -> build -> install -> boot -> dmesg loop.
- [documents/plan.md](documents/plan.md) — problem definition, goals,
  design, evaluation plan.

---

# readaround-split (한국어)

mmap read-around I/O 경로(`mm/filemap.c`의 `do_sync_mmap_readahead()`)를
분리해서, read-around 요청을 순차 read-ahead와 독립적으로 발행/병합할 수
있게 만들고, 그 효과를 발췌 코드가 아니라 **실제로 빌드·부팅되는 커널**
위에서 측정하는 프로젝트.

- [documents/environment.md](documents/environment.md) — QEMU/빌드/게스트
  설정, 그리고 수정 -> 동기화 -> 빌드 -> 설치 -> 부팅 -> dmesg 확인 루프.
- [documents/plan.md](documents/plan.md) — 문제 정의, 목표, 설계, 평가 계획.
