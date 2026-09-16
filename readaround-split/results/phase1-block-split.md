# Phase 1 — Block-layer size-triggered read-around split

**Date:** 2026-09-09
**Question answered:** does splitting the read-around IO at the block
layer produce two IOs that *survive to the device*, or does some layer
re-merge them?
**Answer:** the split survives. Both halves reach the device as separate
requests; nothing re-merges them.

## Setup

| Piece | Value |
|---|---|
| Kernels | `7.1.2-rasplit #5` (RASPLIT probes, no split) vs `#6` (probes + split) |
| Change | `src/block/blk-merge.c` `get_max_io_size()`: for `REQ_OP_READ` + `REQ_RAHEAD` + exactly 256-sector bios, return 128 instead of the device max. No other file changed. |
| Device | QEMU emulated NVMe, `logical_block_size=512`, ext4 on `/mnt/raround` |
| `read_ahead_kb` | 128 → `ra_pages = 32` → 128 KiB window → 256 sectors → split 128 + 128 |
| Workload | `mmaptest --samples 5 --seed 1` on `/mnt/raround/rnd.bin` (512 MiB); per-sample open/mmap/fault/munmap/`DONTNEED`/close, cold cache |

## Propagation (M2) — the primary result

Per fault, every one of the 5 samples produced this exact chain
(raw log: [`data/phase1-split-dmesg.txt`](data/phase1-split-dmesg.txt)):

```
RASPLIT/ra_order:  index=129905 size=32 async=8 order=0   mm window, unchanged
RASPLIT/read_pages: start=129905 nr=32                    one readahead batch, 32 pages
RASPLIT:           sector=2431880 sectors=256 -> 128+128  get_max_io_size() hook decides to halve
RASPLIT/bio_split: sector=2431880 at=128 bytes=131072     the cut, at sector 128
RASPLIT/newrq:     sector=2431880 bytes=65536 nr_segs=16  front request, 64 KiB
RASPLIT/newrq:     sector=2432008 bytes=65536 nr_segs=16  back request,  64 KiB  (base + 128)
RASPLIT/issue:     sector=2431880 bytes=65536             front -> device
RASPLIT/issue:     sector=2432008 bytes=65536             back  -> device
```

| Check | Result |
|---|---|
| `RASPLIT` (decision) + `RASPLIT/bio_split` (cut) as a pair | 5 / 5 |
| Two `RASPLIT/newrq` at 65536 B | 5 / 5 — the split became two `struct request`s |
| Two `RASPLIT/issue` at 65536 B | 5 / 5 — **both halves reached the device** |
| `RASPLIT/bmerge` | never — the plug did not re-merge |
| mm layer (`ra_order`, `read_pages`) | unchanged — one 32-page window, `async_size = 8` untouched |

`nr_segs` on the back request is sometimes 3 instead of 16 — physically
contiguous pages coalescing into fewer bvecs. Cosmetic; both halves are
65536 B.

## Hypotheses

- **H4 (re-merge)** — *not observed.* `bio_submit_split()` stamps
  `REQ_NOMERGE` on the front half (`blk-merge.c:163`), which fails
  `rq_mergeable()` in the plug merge path, so the two halves cannot
  recombine. This is the concrete reason a block-layer split holds where
  an mm-layer split would be re-merged — the basis for the advisor's
  "split at the block layer" guidance.
- **H3 ((c) async marker)** — *sidestepped.* The split is below
  `page_cache_ra_order`, so the `PG_readahead` marker stays where mm put
  it (`async_size = 8`, unchanged vs baseline). No follow-on-readahead
  ambiguity is introduced.
- **H1 (latency)** — *preliminary signal, positive* (see below).

## Latency (M1) — preliminary

> **Superseded (2026-09-16).** The table below is a measurement artifact, not
> a kernel effect. QEMU was running without `cache=none`, so the host page
> cache served the second kernel measured from RAM warmed by the first; with
> `--seed 1` both kernels read the same pages, so run order decided the
> winner. The corrected 200-sample result (p50 −3.4%, p90 −9.0%) and the full
> confound analysis are in [`phase1-m1-latency.md`](phase1-m1-latency.md).

Same seed, same 5 page offsets, one run each
([`data/phase1-latency.csv`](data/phase1-latency.csv)):

| pgoff | baseline #5 (ns) | split #6 (ns) | Δ |
|---:|---:|---:|---:|
| 129921 | 1,628,918 | 1,389,865 | −15% |
| 77307 | 1,980,734 | 824,595 | −58% |
| 126804 | 878,368 | 767,201 | −13% |
| 62943 | 941,792 | 786,864 | −16% |
| 29153 | 961,657 | 646,857 | −33% |

All 5 faster with the split. The fault page sits at the window centre
(`start = pgoff - ra_pages/2`), i.e. it is the first page of the *second*
half — yet latency still drops, which means the two 64 KiB requests are
dispatched concurrently to the NVMe queue and the fault page's half
completes before a single 128 KiB transfer would.

**Caveats:** n = 5, warmup included, single run per kernel, QEMU emulated
NVMe (timing and queue behaviour not representative of real hardware).
Direction is consistent but this is not a rigorous p50/p99. A proper M1
run (≥50 samples, warmup dropped, #5 vs #6) is the next step.

## Known limitations of the Phase 1 change

- **Size match is a heuristic.** `REQ_RAHEAD` is also set on sequential
  read-ahead; only the exact 256-sector size distinguishes read-around
  here. Not exercised by this random-fault workload, wrong under a
  sequential antagonist — this is what Phase 2's explicit tag replaces.
- **`RASPLIT_TARGET_SECTORS = 256` is hard-coded** to this guest's
  `ra_pages`. Confirmed correct here by `read_ahead_kb` and by the
  observed `bytes=131072` bio.
- **`rasplit_enabled` is compile-time**, not a runtime knob.
- **`pr_info_ratelimited`** — fine at 5 samples; will drop lines under a
  heavy workload (10 lines / 5 s).

## Next

1. Proper M1: `#5` vs `#6`, `--samples 50 --seed 1`, drop first 5,
   compare p50 / p99.
2. Phase 1b (optional): move the cut so the fault page lands in the
   first half (e.g. 136 + 120) and re-measure M1.
3. Phase 2: replace the size match with a "read-around" tag carried
   mm → bio.

---

# Phase 1 — block 계층 크기 기반 read-around split (한국어)

**날짜:** 2026-09-09
**답한 질문:** read-around IO를 block 계층에서 쪼개면 두 IO가 *device까지
살아남는가*, 아니면 어느 계층이 다시 합치는가?
**답:** 살아남는다. 두 절반이 각각 별도 request로 device에 도달하고,
아무 계층도 재병합하지 않는다.

## 셋업

| 항목 | 값 |
|---|---|
| 커널 | `7.1.2-rasplit #5` (RASPLIT 프로브만) vs `#6` (프로브 + split) |
| 변경 | `src/block/blk-merge.c` `get_max_io_size()`: `REQ_OP_READ` + `REQ_RAHEAD` + 정확히 256섹터 bio면 장치 최대치 대신 128 반환. 다른 파일 무수정 |
| 장치 | QEMU 에뮬레이션 NVMe, `logical_block_size=512`, `/mnt/raround` ext4 |
| `read_ahead_kb` | 128 → `ra_pages = 32` → 128 KiB 창 → 256섹터 → 128 + 128 분할 |
| 워크로드 | `/mnt/raround/rnd.bin`(512 MiB)에 `mmaptest --samples 5 --seed 1`; 샘플마다 open/mmap/fault/munmap/`DONTNEED`/close, 콜드 캐시 |

## 관통성 (M2) — 핵심 결과

fault 하나당, 5샘플 전부 정확히 이 체인
(원시 로그: [`data/phase1-split-dmesg.txt`](data/phase1-split-dmesg.txt)):

```
RASPLIT/ra_order:  index=129905 size=32 async=8 order=0   mm 창, 그대로
RASPLIT/read_pages: start=129905 nr=32                    readahead 배치 1개, 32페이지
RASPLIT:           sector=2431880 sectors=256 -> 128+128  get_max_io_size() 훅이 절반 결정
RASPLIT/bio_split: sector=2431880 at=128 bytes=131072     섹터 128에서 자름
RASPLIT/newrq:     sector=2431880 bytes=65536 nr_segs=16  앞 request, 64 KiB
RASPLIT/newrq:     sector=2432008 bytes=65536 nr_segs=16  뒤 request, 64 KiB  (base + 128)
RASPLIT/issue:     sector=2431880 bytes=65536             앞 -> device
RASPLIT/issue:     sector=2432008 bytes=65536             뒤 -> device
```

| 확인 | 결과 |
|---|---|
| `RASPLIT`(결정) + `RASPLIT/bio_split`(자름) 짝 | 5 / 5 |
| `RASPLIT/newrq` 65536 B 2줄 | 5 / 5 — split이 `struct request` 2개가 됨 |
| `RASPLIT/issue` 65536 B 2줄 | 5 / 5 — **두 절반 다 device 도달** |
| `RASPLIT/bmerge` | 안 뜸 — plug가 재병합 안 함 |
| mm 계층 (`ra_order`, `read_pages`) | 그대로 — 32페이지 창 1개, `async_size = 8` 안 건드림 |

뒤 request의 `nr_segs`가 16 대신 3인 경우가 있는데, 물리적으로 연속인
페이지가 더 적은 bvec으로 합쳐진 것. 외형만 다르고 둘 다 65536 B.

## 가설

- **H4 (재병합)** — *관측 안 됨.* `bio_submit_split()`이 앞 절반에
  `REQ_NOMERGE`를 붙임(`blk-merge.c:163`) → plug 병합 경로의
  `rq_mergeable()`에 걸려 두 절반이 다시 하나로 못 합쳐짐. mm 계층
  split이면 재병합될 것이 block 계층 split에서는 유지되는 구체적 이유 —
  교수님의 "block 계층에서 split" 조언의 근거.
- **H3 ((c) async 마커)** — *비켜감.* split이 `page_cache_ra_order`
  아래라 `PG_readahead` 마커가 mm이 둔 자리 그대로(`async_size = 8`,
  baseline과 동일). 후속 readahead 모호성 안 생김.
- **H1 (지연)** — *예비 신호, 긍정적* (아래).

## 지연 (M1) — 예비

> **대체됨 (2026-09-16).** 아래 표는 커널 효과가 아니라 측정 아티팩트다.
> QEMU가 `cache=none` 없이 돌고 있어서, 나중에 측정한 커널이 앞 실행이
> 데워놓은 호스트 페이지 캐시에서 읽었다. `--seed 1`이라 두 커널이 같은
> 페이지를 읽으므로 실행 순서가 승자를 정한 셈이다. 교정된 200샘플 결과
> (p50 −3.4%, p90 −9.0%)와 교란 요인 분석 전문은
> [`phase1-m1-latency.md`](phase1-m1-latency.md)에 있다.

같은 시드, 같은 5개 page offset, 각 1회
([`data/phase1-latency.csv`](data/phase1-latency.csv)):

| pgoff | baseline #5 (ns) | split #6 (ns) | Δ |
|---:|---:|---:|---:|
| 129921 | 1,628,918 | 1,389,865 | −15% |
| 77307 | 1,980,734 | 824,595 | −58% |
| 126804 | 878,368 | 767,201 | −13% |
| 62943 | 941,792 | 786,864 | −16% |
| 29153 | 961,657 | 646,857 | −33% |

5개 전부 split이 빠름. fault 페이지는 창 중심(`start = pgoff -
ra_pages/2`) = *뒤* 절반의 첫 페이지인데도 지연이 줄었다는 건, 두 64 KiB
request가 NVMe 큐에 동시 발행되고 fault 페이지가 든 절반이 128 KiB 전송
하나보다 먼저 끝난다는 뜻.

**주의:** n = 5, warmup 포함, 커널당 1회, QEMU 에뮬레이션 NVMe (타이밍·큐
동작이 실물과 다름). 방향은 일관되나 엄밀한 p50/p99는 아님. 제대로 된
M1(≥50샘플, warmup 제거, #5 vs #6)이 다음 단계.

## Phase 1 변경의 알려진 한계

- **크기 매칭은 휴리스틱.** `REQ_RAHEAD`는 순차 read-ahead에도 붙음;
  여기선 정확히 256섹터라는 크기만으로 read-around를 구분. 이 무작위
  fault 워크로드엔 무관, 순차 적대자 하에선 틀림 — Phase 2의 명시적
  태그가 이걸 대체.
- **`RASPLIT_TARGET_SECTORS = 256`은 이 게스트 `ra_pages`에 하드코딩.**
  `read_ahead_kb`와 관측된 `bytes=131072` bio로 여기선 맞음 확인.
- **`rasplit_enabled`는 컴파일 타임**, 런타임 노브 아님.
- **`pr_info_ratelimited`** — 5샘플엔 무해; 무거운 워크로드에선 줄이
  누락(10줄 / 5초).

## 다음

1. 제대로 된 M1: `#5` vs `#6`, `--samples 50 --seed 1`, 앞 5개 버림,
   p50 / p99 비교.
2. Phase 1b (선택): fault 페이지가 앞 절반에 오도록 경계 이동
   (예: 136 + 120) 후 M1 재측정.
3. Phase 2: 크기 매칭을 mm → bio로 전달되는 "read-around" 태그로 교체.
