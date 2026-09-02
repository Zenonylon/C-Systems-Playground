# Read-around IO Split — Experiment Plan

Working plan for the one action under test: split the single
`ra_pages`-page read-around IO in `do_sync_mmap_readahead()` into
`n/2 + n/2`. Problem framing and scope live in
[plan.md](plan.md); this file is the how.

## 1. Problem (one sentence)

Read-around bundles `n` (= `ra_pages`) pages into one IO to serve a
single fault page, so the fault page waits on the speculative remainder.
Splitting that IO into `n/2 + n/2` asks: **(1)** does the fault page's
latency drop, and **(2)** does the split propagate through
`mm/filemap.c` -> `mm/readahead.c` -> `block/bio.c` ->
`block/blk-merge.c` -> `block/blk-mq.c` -> device, or does some layer
re-merge it? (2) is the primary deliverable.

## 2. Hypotheses — what the split changes and how

| # | Hypothesis | Lens |
|---|---|---|
| H1 | The first `n/2` completes before the second, so the fault page (window centre ≈ end of first half) waits on an `n/2` IO instead of an `n` IO -> lower latency; larger effect under contention. | (a), expected payoff |
| H2 | The two halves still fetch `n` pages total -> over-read unchanged. Only cancelling the second half would reduce it. | (b), limit |
| H3 | `mark = start + size - async_size` is computed per window; splitting forces a choice of which half carries the marker, and a wrong choice makes the follow-on `do_async_mmap_readahead()` fire twice or not at all. | (c), side effect |
| H4 | The two halves are contiguous, back-to-back, same thread -> blk-mq plug merging recombines them into one request -> the split disappears below the mm layer. | propagation (expected first result) |

## 3. Metrics + baseline

**Baseline:** a kernel with the `RAROUND` printk only and no split logic
(i.e. current `7.1.2-rasplit #4`). The split build adds only the
splitting code on top and is compared against it.

| Metric | What | How |
|---|---|---|
| M1 (a) | fault -> page-present latency, p50/p99 | `ktime_get_ns()` at read-around entry in `filemap.c` .. folio uptodate; or userspace `clock_gettime` around first touch in the trigger program |
| M2 (propagation) | "one IO or two" at each layer | count of `->readahead()` calls / `submit_bio` calls / merges in `blk-merge.c` / requests reaching the device and their sizes |
| M3 (b) | pages read vs pages actually touched (over-read ratio) | must be identical split vs. baseline (checks H2) |
| M4 (c) | count / timing of `do_async_mmap_readahead()` after the split | detect duplicate or missing follow-on readahead (checks H3) |

**Workload:** contention — one thread doing a large (hundreds of MB)
sequential mmap traversal, plus one thread doing random single-page mmap
faults on a second file, both on `nvme0n1`. Requires extending
`tools/mmaptest.c`.

**Prerequisite:** `nvme0n1` is raw — needs a filesystem, a mount, and a
test file.

## 4. Split boundary — decision: A (filemap level, two calls)

In the read-around `else` branch of `do_sync_mmap_readahead()`, turn the
single `page_cache_ra_order(&ractl, ra)` into two:

```
window: [ra->start, ra->start + n)
  call 1:  _index = ra->start,        size = n/2,      async_size = 0
  call 2:  _index = ra->start + n/2,  size = n - n/2,  async_size = n/4   (marker in the second half)
```

**Why A:** cleanest way to observe question (2) — make the split
explicit at the top and follow it down. Code change is confined to this
branch in `filemap.c` plus its call site. B (splitting inside
`page_cache_ra_order`) pollutes a function shared with non-mmap
readahead for no gain. C (bio-level tagging) is needed to *keep* the
split alive but is overkill for the first observation -> phase 2.

Odd `n`: first half `n/2`, second half `n - n/2`.

## 5. Two implementation sites

| | Layer | What | Expected result |
|---|---|---|---|
| **Phase 1** | upper (mm) | Decision A above. Two calls in `filemap.c`; `readahead.c` untouched. | plug re-merges -> one request at the device ("split disappeared") |
| **Phase 2** | lower (block) | Add a hint bit to `struct readahead_control` -> carried into the bio (flag) by `read_pages` / `->readahead()` -> the back-merge gate in `blk-merge.c` and `blk_attempt_plug_merge` in `blk-mq.c` refuse to merge across the `n/2` boundary when the flag is set. | both requests survive to the device -> M1 (latency) becomes measurable |

The assignment covers both upper and lower layers, so both phases are
walked: from "split it and watch it disappear" (phase 1) to "keep it
alive and measure the latency change" (phase 2).

## 6. Anticipated traps

- **merge (central):** the blk-mq plug list auto-merges adjacent bios.
  `/sys/block/nvme0n1/queue/nomerges` can disable it temporarily — a
  control for phase 1 ("does it actually stay two if merging is off?").
  The real goal is *selective* refusal via the flag.
- **large folio:** `page_cache_ra_order` tries for large folios
  (`new_order = min(..., ilog2(ra->size))`). Halving `ra->size` changes
  the folio order, so the folio layout diverges from baseline over the
  same page range and pollutes the comparison. Read-around forces
  `ra->order = 0`, so the real impact may be small, but verify —
  including the `mapping_large_folio_support` branch
  (`readahead.c:482`).
- **async marker:** putting the marker in the first half risks immediate
  re-trigger (faults usually land at the window centre); put it in the
  second half to preserve the original "extend at the tail" meaning
  (reflected in the §4 table).
- **ractl reuse:** update `_index` before the second call; also reset
  `_nr_pages`, `_workingset`, and friends — re-init via
  `DEFINE_READAHEAD` or reset the fields by hand. Call
  `maybe_unlock_mmap_for_io` only once, before both calls.

## Status

Decision A / phase-1 agreed.

Done:
- `nvme0n1`: ext4 (label `raround`, UUID
  `37f12713-154e-43a0-9906-90ba66786be2`), mounted at `/mnt/raround` via
  guest `/etc/fstab` (`defaults,nofail`).
- Test files: `/mnt/raround/seq512.bin` (sequential antagonist),
  `/mnt/raround/rnd.bin` (random target), 512 MiB each, `/dev/urandom`.
  Host `nvme_disk.raw` now ~1.1 GB (was sparse).
- `tools/mmaptest.c`: random single-page timed faults, fresh
  open/mmap/munmap/close per sample (so `f_ra.mmap_miss` never
  saturates past `MMAP_LOTSAMISS`), `POSIX_FADV_DONTNEED` per sample,
  CSV `pgoff,latency_ns` to stdout. Built `-static` on the host.
- Driver <-> sensor correlation verified on the booted `7.1.2-rasplit`
  kernel: 50 samples all hit the read-around branch, CSV pgoffs match
  the `RAROUND: pgoff=` lines in dmesg. `/mnt/raround` has
  `ra_pages = 32` (128 KiB window) -> the split is **16 + 16**.
- Informal pre-split M1 on emulated NVMe: p50 ~650 us, p99 ~930 us
  (50 cold random faults, first 5 dropped).

Next (phase 1):
- `src/mm/filemap.c` read-around `else` branch: one
  `page_cache_ra_order` call -> two (16 + 16, marker in the second
  half).
- Per-layer printk in `src/mm/readahead.c`,
  `src/block/{bio,blk-merge,blk-mq}.c` — bio count / merge decisions /
  requests reaching the device.
- sync -> build -> install -> boot -> `mmaptest` -> read dmesg: does
  16 + 16 survive, and where does it re-merge?
- Workload step 2 (sequential antagonist) when phase 2 needs M1 under
  contention.

---

# Read-around IO Split — 실험 계획 (한국어)

실험 대상이 되는 행동 하나에 대한 작업 계획: `do_sync_mmap_readahead()`
의 `ra_pages` 페이지 read-around IO 하나를 `n/2 + n/2`로 분리한다.
문제 프레이밍과 범위는 [plan.md](plan.md)에 있고, 이 파일은 방법이다.

## 1. 문제 (한 문장)

read-around는 fault 페이지 하나를 위해 `n`(=`ra_pages`) 페이지를 **한
IO**로 묶기 때문에, fault 페이지가 speculative한 나머지의 완료를
기다린다. 이 IO를 `n/2 + n/2`로 분리하면 묻게 된다: **(1)** fault
페이지 지연이 줄어드는가, **(2)** 그 분리가 `mm/filemap.c` ->
`mm/readahead.c` -> `block/bio.c` -> `block/blk-merge.c` ->
`block/blk-mq.c` -> device를 관통하는가, 아니면 어느 계층이 다시
병합하는가. (2)가 핵심 산출물.

## 2. 가설 — split하면 무엇이 어떻게 바뀌나

| # | 가설 | 렌즈 |
|---|---|---|
| H1 | 첫 `n/2`가 두 번째보다 먼저 완료 → fault 페이지(창 중심 ≈ 첫 절반 끝)가 기다리는 IO가 `n` → `n/2`로 줄어 지연 감소; 경합 시 효과 커짐 | (a), 기대 효과 |
| H2 | 두 절반이 여전히 총 `n` 페이지를 가져옴 → over-read 불변. 두 번째 절반을 취소해야만 줄어듦 | (b), 한계 |
| H3 | `mark = start + size − async_size`는 한 창 기준 → 분리하면 마커를 어느 절반에 실을지 정해야 하고, 잘못 실으면 후속 `do_async_mmap_readahead()`가 중복/누락 | (c), 부작용 |
| H4 | 두 절반이 연속·인접·동일 스레드 → blk-mq plug 병합이 다시 하나의 request로 합침 → mm 계층 아래에서 분리가 사라짐 | 관통성 (예상되는 1차 결과) |

## 3. 측정 지표 + baseline

**baseline:** split 로직 없이 `RAROUND` printk만 있는 커널 (= 현재
`7.1.2-rasplit #4`). split 버전은 여기에 분리 코드만 얹어 비교.

| 지표 | 무엇 | 측정법 |
|---|---|---|
| M1 (a) | fault → 페이지 present 지연 p50/p99 | `filemap.c`의 read-around 진입 `ktime_get_ns()` ~ folio uptodate; 또는 트리거 프로그램의 사용자공간 `clock_gettime` (첫 접근 전후) |
| M2 (관통) | 계층별 "IO 1개냐 2개냐" | `->readahead()` 호출 수 / `submit_bio` 수 / `blk-merge.c` 병합 횟수 / device로 나간 request 수·크기 |
| M3 (b) | 읽은 페이지 vs 실제 touch된 페이지 (over-read 비율) | split 전후 동일해야 함 (H2 검증) |
| M4 (c) | split 후 `do_async_mmap_readahead()` 발동 횟수·시점 | 중복/누락 탐지 (H3 검증) |

**워크로드:** 경합 — 큰 파일(수백 MB) 순차 mmap 순회 스레드 + 다른
파일 무작위 단일 페이지 mmap fault 스레드, 둘 다 `nvme0n1` 위.
`tools/mmaptest.c` 확장 필요.

**선행 작업:** `nvme0n1`이 raw 상태 → 파일시스템 생성·마운트·테스트
파일.

## 4. Split 경계 — 결정: A (filemap 레벨, 두 번 호출)

`do_sync_mmap_readahead()`의 read-around `else` 블록에서
`page_cache_ra_order(&ractl, ra)` 한 번 → 두 번으로:

```
창: [ra->start, ra->start + n)
  호출 1:  _index = ra->start,        size = n/2,      async_size = 0
  호출 2:  _index = ra->start + n/2,  size = n − n/2,  async_size = n/4   (마커는 뒤쪽 절반에)
```

**A인 이유:** 질문 (2)를 가장 깨끗하게 관측 — 최상위에서 분리를 명시하고
아래로 따라감. 코드 변경은 `filemap.c`의 이 블록 + 호출부에 국한. B
(`page_cache_ra_order` 내부 분리)는 비-mmap readahead와 공유하는 함수를
오염시키고 이득 없음. C(bio 태깅)는 분리를 *유지*하는 데 필요하지만
1차 관측엔 과함 → 2단계로.

`n`이 홀수면: 첫 절반 `n/2`, 둘째 절반 `n − n/2`.

## 5. 구현 위치 두 갈래

| | 계층 | 내용 | 예상 결과 |
|---|---|---|---|
| **1단계** | 상위 (mm) | 위 결정 A. `filemap.c`에서 두 호출, `readahead.c` 손대지 않음 | plug에서 재병합 → device엔 request 1개 ("분리가 사라졌다") |
| **2단계** | 하위 (block) | `struct readahead_control`에 힌트 비트 → `read_pages` / `->readahead()`가 bio flag로 반영 → `blk-merge.c`의 back-merge 게이트와 `blk-mq.c`의 `blk_attempt_plug_merge`가 flag 있으면 `n/2` 경계 넘는 병합 거부 | 두 request가 device까지 생존 → M1(지연) 측정 가능 |

과제가 상위·하위 계층을 다 다루므로 두 단계를 다 밟는다: "쪼갰더니
사라졌다"(1단계) → "유지시켰고 지연이 이렇게 변했다"(2단계).

## 6. 예상 함정

- **merge (핵심):** blk-mq plug list가 인접 bio를 자동 병합.
  `/sys/block/nvme0n1/queue/nomerges`로 임시 비활성화 → 1단계 대조군
  ("병합만 없으면 실제로 2개 유지되나?"). 최종 목표는 flag 기반 *선택적*
  거부.
- **large folio:** `page_cache_ra_order`는 큰 folio를 시도
  (`new_order = min(…, ilog2(ra->size))`). `ra->size`를 반으로 줄이면
  folio order가 달라져 같은 페이지 범위인데 folio 구성이 baseline과
  어긋나 비교 오염. read-around는 `ra->order = 0`을 강제하므로 실제
  영향은 작을 수 있으나 `mapping_large_folio_support` 분기
  (`readahead.c:482`) 포함해 확인.
- **async marker:** 마커를 첫 절반에 실으면 fault가 보통 창 중심이라
  즉시 재트리거될 위험 → 둘째 절반에 실어 "창 뒤끝에서 연장"이라는
  원래 의미 유지 (§4 표에 반영).
- **ractl 재사용:** 두 번째 호출 전 `_index` 갱신 필수. `_nr_pages`,
  `_workingset` 등 내부 상태도 리셋 — `DEFINE_READAHEAD`로 재초기화하거나
  필드 수동 리셋. `maybe_unlock_mmap_for_io`는 두 호출 전에 한 번만.

## 진행 상황

결정 A / 1단계 합의됨.

완료:
- `nvme0n1`: ext4 (레이블 `raround`, UUID
  `37f12713-154e-43a0-9906-90ba66786be2`), 게스트 `/etc/fstab`
  (`defaults,nofail`)로 `/mnt/raround`에 마운트.
- 테스트 파일: `/mnt/raround/seq512.bin`(순차 적대자),
  `/mnt/raround/rnd.bin`(무작위 대상), 각 512 MiB, `/dev/urandom`.
  호스트 `nvme_disk.raw` 이제 ~1.1 GB (sparse였음).
- `tools/mmaptest.c`: 무작위 단일 페이지 타이밍 fault, 샘플마다 새
  open/mmap/munmap/close (그래서 `f_ra.mmap_miss`가 `MMAP_LOTSAMISS`를
  넘겨 포화되지 않음), 샘플마다 `POSIX_FADV_DONTNEED`, `pgoff,latency_ns`
  CSV를 stdout으로. 호스트에서 `-static` 빌드.
- 부팅된 `7.1.2-rasplit` 커널에서 드라이버 <-> 센서 대조 검증: 50샘플
  전부 read-around 분기 진입, CSV pgoff가 dmesg `RAROUND: pgoff=` 줄과
  일치. `/mnt/raround`는 `ra_pages = 32` (128 KiB 윈도우) -> split은
  **16 + 16**.
- 비공식 pre-split M1 (에뮬레이션 NVMe): p50 ~650 us, p99 ~930 us
  (콜드 무작위 fault 50개, 앞 5개 버림).

다음 (1단계):
- `src/mm/filemap.c` read-around `else` 분기: `page_cache_ra_order` 한
  번 호출 -> 두 번 (16 + 16, 마커는 뒤쪽 절반).
- `src/mm/readahead.c`, `src/block/{bio,blk-merge,blk-mq}.c`에 계층별
  printk — bio 개수 / 병합 결정 / device 도달 request 수.
- sync -> build -> 설치 -> 부팅 -> `mmaptest` -> dmesg: 16 + 16이
  유지되는가, 어디서 재병합되는가?
- 워크로드 2단계(순차 적대자)는 2단계에서 경합 하 M1이 필요할 때.
