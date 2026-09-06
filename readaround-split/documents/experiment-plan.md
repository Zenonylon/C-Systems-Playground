# Read-around IO Split — Experiment Plan

Working plan for the one action under test: split the single
`ra_pages`-page read-around IO into `n/2 + n/2`. Per advisor guidance
(2026-09) the split is done at the **block layer**, not in
`do_sync_mmap_readahead()` — see §4. Problem framing and scope live in
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
| H3 | The split is at the block layer, *below* where `page_cache_ra_order` sets the `PG_readahead` marker on a folio, so the marker is untouched — mm still computes one window with its normal `async_size` and the follow-on `do_async_mmap_readahead()` behaves exactly as baseline. (c) is sidestepped, not incurred. | (c), side effect avoided |
| H4 | `bio_split` via `bio_submit_split` stamps `REQ_NOMERGE` on the front half (`blk-merge.c:163`), so the plug list cannot recombine the two halves -> the split survives to the device. The open question is the *back* half (submitted without `REQ_NOMERGE`) back-merging onto an adjacent request under contention. | propagation |

## 3. Metrics + baseline

**Baseline:** a kernel with the `RAROUND` printk only and no split logic
(i.e. current `7.1.2-rasplit #4`). The split build adds only the
splitting code on top and is compared against it.

| Metric | What | How |
|---|---|---|
| M1 (a) | fault -> page-present latency, p50/p99 | `ktime_get_ns()` at read-around entry in `filemap.c` .. folio uptodate; or userspace `clock_gettime` around first touch in the trigger program |
| M2 (propagation) | "one IO or two" at each layer | the `RASPLIT/*` probes already in `src/`: `read_pages` / `ra_order` (mm, expect one window) -> `bio_split` (our cut) -> `bmerge` (any re-merge) -> `newrq` / `issue` (requests + sizes reaching the device; expect two 64 KiB) |
| M3 (b) | pages read vs pages actually touched (over-read ratio) | must be identical split vs. baseline (checks H2) |
| M4 (c) | `do_async_mmap_readahead()` count / timing, and `RASPLIT/ra_order` marker fields (`size`, `async_size`) | must be identical split vs. baseline — the split is below the mm layer, so the marker must not move (checks H3) |

**Workload:** contention — one thread doing a large (hundreds of MB)
sequential mmap traversal, plus one thread doing random single-page mmap
faults on a second file, both on `nvme0n1`. Requires extending
`tools/mmaptest.c`.

**Prerequisite:** `nvme0n1` is raw — needs a filesystem, a mount, and a
test file.

## 4. Split boundary — block layer (revised per advisor, 2026-09)

An earlier draft chose decision A: split at the filemap level by turning
the single `page_cache_ra_order()` call into two.

**Advisor guidance was two-part:**

- **Proper design:** the upper (mm) layer tags the request as
  "read-around"; the block layer does the actual split on that tag. This
  keeps the read-around IO issued and merged *independently* of
  sequential read-ahead — a merging decision, so it belongs at the block
  layer.
- **Quick test:** defer the cross-layer tag. Since read-around always
  emits a fixed-size IO, have the block layer match that size and split
  it unconditionally.

Either way the split moves from filemap to the block layer. Decision A
is dropped: a filemap-level split is very likely re-merged by the plug
before the device, so on its own it can't show a latency payoff — it
just "disappears".

**The fact the quick test exploits:** in the read-around branch
`ra->size` is
always `ra_pages` (`filemap.c:3399`), so on a cold fault the submitted
IO is always one `ra_pages * PAGE`-byte read — 131072 B here
(`ra_pages = 32`). The block layer can match on that exact size and
split, with no hint from mm.

`do_sync_mmap_readahead()` is **left unchanged** for the quick test
(the `RAROUND` sensor stays). Odd `n`: front half `n/2`, back half
`n - n/2`.

**Premise to verify first** (with the `RASPLIT` probes already in
`src/`): a cold read-around fault must arrive at `blk_mq_submit_bio` as
*one* 131072-byte `REQ_OP_READ | REQ_RAHEAD` bio on `nvme0n1`. If ext4 /
mpage already chunks it (e.g. at an extent boundary), the size match is
unreliable and that is itself a finding.

## 5. Two implementation routes

| | Trigger | Where | Cross-layer plumbing |
|---|---|---|---|
| **Phase 1 — quick test (first)** | IO size `== ra_pages * PAGE` (+ `REQ_OP_READ` + `REQ_RAHEAD` + `nvme0n1`) | `bio_split_rw()` (`blk-merge.c:431`): lift the `max_bytes` arg into a local and, when the predicate matches, cap it at `bio->bi_iter.bi_size / 2`. `bio_split_rw_at` -> `bio_submit_split` then do the cut, `REQ_NOMERGE`, chain and re-submit — all existing, tested code. | none — the fixed size *is* the implicit signal |
| **Phase 2 — proper** | an explicit "this is read-around" tag | mm sets the tag in the read-around branch -> carried by `readahead_control` -> `read_pages` / `->readahead()` -> a bio flag -> `bio_split_rw()` (and the merge gates in `blk-merge.c` / `blk-mq.c`) act on the flag instead of the size | the cross-layer hint the advisor deferred |

Why both: the quick test answers "does halving the read-around IO lower
fault latency" with ~3 new lines and no `filemap.c` change. But a
size-only match is fragile — plain sequential read-ahead also emits
`ra_pages`-sized reads and would be split too (harmless for the current
random-fault workload, wrong under the contention workload). The tag in
Phase 2 removes that, and "upper layer tags / lower layer splits" is
what the assignment's upper-and-lower-layer scope actually asks for.

**Re-entry:** the back half re-enters `bio_split_rw()` at
`bi_size == 65536`, so the `==` match (not `>=`) makes it pass straight
through — no recursion.

## 6. Anticipated traps

- **premise (verify before coding):** the trigger is "a 131072-byte
  `READ | REQ_RAHEAD` bio on `nvme0n1`". Confirm with the `RASPLIT`
  probes that a cold read-around fault actually arrives as exactly one
  such bio — a partly-cached window submits less, and the filesystem may
  chunk it. The cold `mmaptest` workload avoids the cache case.
- **size-match fragility (Phase 1):** plain sequential read-ahead also
  emits `ra_pages`-sized reads, so it gets split too. Fine for the
  current random-fault-only workload; wrong once the sequential
  antagonist runs. This is exactly why Phase 2 needs the tag — note it
  in the write-up.
- **back half not `REQ_NOMERGE`:** `bio_submit_split` marks only the
  front half. The back half can back-merge onto an adjacent request
  under contention. `/sys/block/nvme0n1/queue/nomerges` is the control
  to isolate this.
- **completion accounting:** the front half is chained to the parent via
  `bio_chain`; make sure `mmaptest`'s per-fault timing still sees the
  fault resolve once (not two half-completions) — it measures userspace
  fault-to-touch, so this should be transparent, but check `dmesg` for
  double `RASPLIT/issue` vs a single fault.

## Status

Split boundary moved from filemap (decision A) to the block layer per
advisor guidance (2026-09). Phase 1 = size-triggered cap in
`bio_split_rw()`; Phase 2 = read-around tag.

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

Per-layer `RASPLIT/*` printk probes already applied (uncommitted) in
`src/mm/readahead.c` and `src/block/{bio,blk-merge,blk-mq}.c`, filtered
to `READ | REQ_RAHEAD` on `nvme0n1`.

Next (Phase 1):
- Build + boot the sensor kernel as-is; run `mmaptest --samples 5
  --seed 1`; confirm one 131072-byte bio per fault in `RASPLIT/newrq`
  (the §4 premise). `filemap.c` untouched.
- `src/block/blk-merge.c` `bio_split_rw()`: cap `max_bytes` at half when
  the read-around predicate matches (~3 lines).
- sync -> build -> install -> boot -> `mmaptest` -> dmesg: do two
  64 KiB requests reach the device (`RASPLIT/issue` x2), and does M1
  (p50/p99) move vs the baseline kernel?
- Workload step 2 (sequential antagonist) when measuring M1 under
  contention / when the back-half merge trap matters.

---

# Read-around IO Split — 실험 계획 (한국어)

실험 대상이 되는 행동 하나에 대한 작업 계획: `ra_pages` 페이지
read-around IO 하나를 `n/2 + n/2`로 분리한다. 교수님 조언(2026-09)에
따라 분리는 `do_sync_mmap_readahead()`가 아니라 **block 계층**에서
한다 — §4 참조. 문제 프레이밍과 범위는 [plan.md](plan.md)에 있고, 이
파일은 방법이다.

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
| H3 | 분리 지점이 `page_cache_ra_order`의 `PG_readahead` 마커 설정보다 *아래*(block 계층)라 마커는 건드려지지 않음 → mm은 여전히 정상 `async_size`로 창 하나를 계산하고 후속 `do_async_mmap_readahead()`도 baseline과 동일. (c)는 유발되는 게 아니라 비켜감 | (c), 부작용 회피 |
| H4 | `bio_submit_split` 경로가 앞쪽 절반에 `REQ_NOMERGE`를 붙임(`blk-merge.c:163`) → plug list가 두 절반을 다시 합칠 수 없음 → 분리가 device까지 생존. 미해결 질문은 `REQ_NOMERGE` 없이 제출되는 *뒤쪽* 절반이 경합 시 인접 request에 back-merge되는지 | 관통성 |

## 3. 측정 지표 + baseline

**baseline:** split 로직 없이 `RAROUND` printk만 있는 커널 (= 현재
`7.1.2-rasplit #4`). split 버전은 여기에 분리 코드만 얹어 비교.

| 지표 | 무엇 | 측정법 |
|---|---|---|
| M1 (a) | fault → 페이지 present 지연 p50/p99 | `filemap.c`의 read-around 진입 `ktime_get_ns()` ~ folio uptodate; 또는 트리거 프로그램의 사용자공간 `clock_gettime` (첫 접근 전후) |
| M2 (관통) | 계층별 "IO 1개냐 2개냐" | `src/`에 이미 넣은 `RASPLIT/*` probe: `read_pages` / `ra_order`(mm, 창 1개 기대) → `bio_split`(우리가 자른 지점) → `bmerge`(재병합 여부) → `newrq` / `issue`(device 도달 request 수·크기, 64 KiB 2개 기대) |
| M3 (b) | 읽은 페이지 vs 실제 touch된 페이지 (over-read 비율) | split 전후 동일해야 함 (H2 검증) |
| M4 (c) | `do_async_mmap_readahead()` 횟수·시점, `RASPLIT/ra_order`의 마커 필드(`size`, `async_size`) | split 전후 동일해야 함 — 분리가 mm 계층 아래라 마커가 움직이면 안 됨 (H3 검증) |

**워크로드:** 경합 — 큰 파일(수백 MB) 순차 mmap 순회 스레드 + 다른
파일 무작위 단일 페이지 mmap fault 스레드, 둘 다 `nvme0n1` 위.
`tools/mmaptest.c` 확장 필요.

**선행 작업:** `nvme0n1`이 raw 상태 → 파일시스템 생성·마운트·테스트
파일.

## 4. Split 경계 — block 계층 (교수님 조언 반영, 2026-09)

이전 초안은 결정 A(= filemap 레벨에서 `page_cache_ra_order` 한 번을
두 번으로)였다.

**교수님 조언은 두 갈래였다:**

- **제대로 된 설계:** 상위(mm) 계층이 요청에 "read-around" 태그를 달고,
  block 계층이 그 태그로 실제 split을 한다. 이러면 read-around IO를
  순차 read-ahead와 *독립적으로* 발행·병합하게 된다 — 병합에 관한
  결정이므로 block 계층에 속한다.
- **빠른 테스트:** 계층 간 태그 전달은 후순위로 미룬다. read-around는
  항상 같은 크기 IO를 내므로, block 계층이 그 크기가 보이면 무조건
  2등분한다.

어느 쪽이든 분리는 filemap에서 block 계층으로 옮겨간다. 결정 A는
폐기: filemap 레벨 분리는 device 전에 plug가 다시 합칠 가능성이 매우
커서, 그 자체로는 지연 이득을 못 보여준다 — 그냥 "사라진다".

**빠른 테스트가 이용하는 사실:** read-around 분기에서 `ra->size`는 항상
`ra_pages` (`filemap.c:3399`)라, 콜드 fault 시 제출되는 IO는 항상
`ra_pages * PAGE` 바이트 읽기 하나 — 여기선 131072 B (`ra_pages = 32`).
block 계층이 그 정확한 크기를 매칭해서 자르면 mm의 힌트가 필요 없다.

빠른 테스트에서 `do_sync_mmap_readahead()`는 **손대지 않는다**
(`RAROUND` 센서는 유지). `n`이 홀수면 앞 `n/2`, 뒤 `n − n/2`.

**먼저 검증할 전제** (`src/`에 이미 있는 `RASPLIT` probe로): 콜드
read-around fault가 `blk_mq_submit_bio`에 `nvme0n1` 위 131072바이트
`REQ_OP_READ | REQ_RAHEAD` bio *하나*로 도착해야 한다. ext4 / mpage가
이미 (예: extent 경계에서) 쪼개고 있으면 크기 매칭이 불안정하고, 그
사실 자체가 결과다.

## 5. 구현 두 갈래

| | 트리거 | 어디서 | 계층 간 전달 |
|---|---|---|---|
| **1단계 — 빠른 테스트 (먼저)** | IO 크기 `== ra_pages * PAGE` (+ `REQ_OP_READ` + `REQ_RAHEAD` + `nvme0n1`) | `bio_split_rw()` (`blk-merge.c:431`): `max_bytes` 인자를 지역변수로 빼고, 조건 매칭 시 `bio->bi_iter.bi_size / 2`로 캡. 그 다음 `bio_split_rw_at` → `bio_submit_split`이 자르기·`REQ_NOMERGE`·체인·재제출을 다 함 — 전부 기존 검증된 코드 | 없음 — 고정 크기가 곧 암묵 신호 |
| **2단계 — 제대로** | 명시적 "이건 read-around" 태그 | mm이 read-around 분기에서 태그 설정 → `readahead_control`이 운반 → `read_pages` / `->readahead()` → bio flag → `bio_split_rw()`(및 `blk-merge.c` / `blk-mq.c` 병합 게이트)가 크기 대신 flag로 판단 | 교수님이 미룬 계층 간 힌트 |

둘 다 하는 이유: 빠른 테스트는 "read-around IO를 반으로 줄이면 fault
지연이 내려가나"를 새 코드 ~3줄, `filemap.c` 무수정으로 답한다. 다만
크기만 매칭하면 취약하다 — 순차 read-ahead도 `ra_pages` 크기 읽기를
내므로 같이 잘린다 (현재 무작위 fault 워크로드엔 무해, 경합 워크로드엔
틀림). 2단계 태그가 이걸 없애고, "상위가 태그 / 하위가 split"이 과제의
상·하위 계층 범위가 실제로 요구하는 것이다.

**재진입:** 뒤쪽 절반은 `bi_size == 65536`으로 `bio_split_rw()`에 다시
들어오므로, `>=`가 아닌 `==` 매칭이면 그대로 통과 — 재귀 없음.

## 6. 예상 함정

- **전제 (코딩 전 검증):** 트리거는 "`nvme0n1` 위 131072바이트
  `READ | REQ_RAHEAD` bio". 콜드 read-around fault가 정확히 그런 bio
  하나로 도착하는지 `RASPLIT` probe로 확인 — 일부 캐시된 창은 더 작게
  제출되고, 파일시스템이 쪼갤 수도. 콜드 `mmaptest` 워크로드는 캐시
  경우를 피함.
- **크기 매칭 취약성 (1단계):** 순차 read-ahead도 `ra_pages` 크기
  읽기를 내므로 같이 잘림. 현재 무작위 fault 전용 워크로드엔 무해,
  순차 적대자가 도는 순간 틀림. 2단계 태그가 필요한 이유 그 자체 —
  write-up에 명시.
- **뒤쪽 절반엔 `REQ_NOMERGE` 없음:** `bio_submit_split`은 앞쪽 절반만
  표시. 뒤쪽 절반은 경합 시 인접 request에 back-merge 가능.
  `/sys/block/nvme0n1/queue/nomerges`가 이걸 격리하는 대조 장치.
- **완료 회계:** 앞쪽 절반은 `bio_chain`으로 부모에 체인됨. `mmaptest`의
  fault별 타이밍이 fault를 한 번만 resolve하는 걸로 보는지 확인 (반쪽
  완료 2개 아님) — 사용자공간 fault-to-touch를 재므로 투명해야 정상이나,
  `dmesg`에서 fault 1회당 `RASPLIT/issue`가 2줄인지 대조.

## 진행 상황

교수님 조언(2026-09)으로 분리 경계를 filemap(결정 A)에서 block 계층으로
옮김. 1단계 = `bio_split_rw()`에서 크기 기반 캡; 2단계 = read-around
태그.

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

계층별 `RASPLIT/*` printk probe는 `src/mm/readahead.c`,
`src/block/{bio,blk-merge,blk-mq}.c`에 이미 적용됨(미커밋), `nvme0n1`의
`READ | REQ_RAHEAD`로 필터.

다음 (1단계):
- 센서 커널을 그대로 빌드·부팅; `mmaptest --samples 5 --seed 1` 실행;
  `RASPLIT/newrq`에서 fault당 131072바이트 bio 1개인지 확인 (§4 전제).
  `filemap.c` 무수정.
- `src/block/blk-merge.c` `bio_split_rw()`: read-around 조건 매칭 시
  `max_bytes`를 절반으로 캡 (~3줄).
- sync -> build -> 설치 -> 부팅 -> `mmaptest` -> dmesg: 64 KiB request
  2개가 device에 도달하는가 (`RASPLIT/issue` 2줄), baseline 커널 대비
  M1(p50/p99)이 움직이는가?
- 워크로드 2단계(순차 적대자)는 경합 하 M1 측정 시 / 뒤쪽 절반 병합
  함정이 문제될 때.
