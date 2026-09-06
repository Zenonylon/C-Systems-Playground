# Plan

*(Design/Evaluation still template — filled in as the design takes shape.)*

## Problem

Read-around is a bet: on a mmap fault for a page that isn't cached,
`do_sync_mmap_readahead()` doesn't know what will be needed next, so it
reads `ra_pages` pages (the fault page plus a speculative window on both
sides) as **one IO**. Three costs follow from that bet, and they are
inherent to the "read a window around the fault" idea, not bugs:

- **(a) latency coupling — time axis.** The urgent fault page can't
  complete until the whole `ra_pages`-page IO completes, so it waits on
  the speculative pages it was bundled with. Under contention (a large
  sequential read-ahead stream in flight at the same time) this shows up
  as inflated fault latency.
- **(b) over-read — amount axis.** If only a few of the `ra_pages` pages
  are ever touched, the rest is wasted bandwidth and page cache. Total
  bytes read, not timing.
- **(c) async-marker coupling — side effect.** Read-around sets
  `ra->async_size` so a later fault in the trailing region re-triggers
  readahead (`do_async_mmap_readahead()`). If the IO is split, it's
  ambiguous which half carries that marker, so the follow-on readahead
  can fire twice or not at all.

**The one action under test:** split the single `ra_pages`-page
read-around IO into `n/2 + n/2`. How that action bears on each cost is
what differs:

- it is the **remedy** for (a) — the first half returns before the
  second, so the fault page arrives sooner;
- it is **not** a remedy for (b) — two halves still fetch the same
  `ra_pages` pages; only cancelling the second half would;
- it **can introduce** (c), but only if the split is done in the mm
  layer where the marker is chosen. Doing the split at the block layer —
  below `page_cache_ra_order`, which sets the marker — leaves the marker
  where mm put it, so (c) is sidestepped. Confirming that is a check,
  not a goal.

So (a) is the reason to do the split; (b) and (c) are lenses for reading
the result, not goals to solve.

**Problem statement.** Read-around bundles `n` pages into one IO to serve
a single fault page, so the fault page waits on the speculative
remainder. Splitting that IO into `n/2 + n/2` asks two things: (1) does
the fault page's latency drop, and (2) does the split actually propagate
through the kernel — `mm/filemap.c` -> `mm/readahead.c` -> `block/bio.c`
-> `block/blk-merge.c` -> `block/blk-mq.c` -> device — or does some layer
re-merge / collapse it? Question (2) is the primary deliverable
(observing where the split holds and where it breaks across layers);
(1) is the "is there a payoff" check.

## Goals

"Done" looks like:

- The `ra_pages`-page read-around IO in `do_sync_mmap_readahead()` is
  issued as two IOs (`n/2 + n/2`) instead of one.
- Instrumentation at each layer (`filemap.c`, `readahead.c`, `bio.c`,
  `blk-merge.c`, `blk-mq.c`) shows, on a real booted kernel, where the
  two-IO shape survives down to the emulated NVMe device and where (if
  anywhere) a layer merges it back into one request.
- A contention workload (large sequential mmap traversal + random
  single-page mmap faults on a second file) run against the split kernel
  and the baseline, with fault-page latency compared between them —
  evidence for or against (a).
- The result is written up through all three lenses: does the split help
  (a), does over-read stay unchanged as predicted (b), does the async
  marker stay untouched because the split is below the mm layer (c).

Explicitly out of scope:

- THP-forced and `VM_EXEC` readahead paths in
  `do_sync_mmap_readahead()` — only the plain read-around `else` branch
  is touched.
- Redesigning async readahead. (c) is observed and reported, not fixed.
- Actually reducing over-read, e.g. by cancelling the second half — that
  would fix (b) but it is a different action than "split".
- Upstreamable quality: this is a study/observation exercise, not a
  patch submission.

## Design

- Where exactly is the split boundary — which step gets separated from
  which?
- What new interfaces / data structures does that require?
- How does it interact with the existing merge logic in
  `block/blk-merge.c`, `block/bio.c`, `block/blk-mq.c`?

## Evaluation

- What workload(s) will make the effect (or lack of one) visible?
- What metric(s) count as evidence it worked?
- What's the baseline to compare against?

## Status

- Environment loop (edit -> sync -> build -> install -> boot -> trigger
  -> dmesg) is built and verified. Guest runs `7.1.2-rasplit` (#4); the
  `RAROUND` sensor in the read-around branch fires on ordinary system
  activity (hundreds of lines in guest `dmesg`), so the path is
  confirmed reachable end-to-end.
- Problem and goals defined (this document). Design and evaluation not
  started.
- Split boundary decided (per advisor, 2026-09): the **block layer**.
  The advice was two-part — proper design: mm tags the request as
  "read-around" and the block layer splits on the tag; quick test: skip
  the tag and have the block layer match the fixed read-around IO size
  and split unconditionally. Phase 1 = the quick test (detect
  `ra_pages * PAGE` in `bio_split_rw()`, cap to half); Phase 2 = the
  tag. See `experiment-plan.md` §4-§5.
- Done: filesystem + test files on `nvme0n1`; `tools/mmaptest.c`
  (single-fault timer). Open: extending it into the contention
  workload; the Phase 1 cap; the Phase 2 tag plumbing.

---

# 계획 (한국어)

*(설계·평가 절은 아직 템플릿 — 설계가 잡히는 대로 채운다.)*

## 문제

read-around는 도박이다. 캐시에 없는 페이지에 mmap fault가 나면,
`do_sync_mmap_readahead()`는 다음에 뭐가 필요할지 모르니까 `ra_pages`
페이지(= fault 페이지 + 양옆의 speculative 윈도우)를 **하나의 IO로**
읽는다. 그 도박에서 세 가지 대가가 따라 나오며, 이건 버그가 아니라
"fault 주변 윈도우를 읽는다"는 아이디어 자체에 내재한 것이다:

- **(a) 지연 결합 — 시간 축.** 급한 fault 페이지는 `ra_pages` 페이지
  IO 전체가 끝나야 완료되므로, 같이 묶인 speculative 페이지들을 기다린다.
  경합 상황(동시에 큰 순차 read-ahead 스트림이 떠 있을 때)에서 fault
  지연 증가로 나타난다.
- **(b) over-read — 양 축.** `ra_pages` 중 몇 페이지만 실제로 쓰이면
  나머지는 대역폭·페이지 캐시 낭비다. 타이밍이 아니라 총 읽은 바이트.
- **(c) async 마커 결합 — 부작용.** read-around는 `ra->async_size`를
  설정해서, 나중에 뒷쪽 구간에 fault가 나면 readahead를 다시 트리거하게
  한다(`do_async_mmap_readahead()`). IO를 쪼개면 그 마커가 어느 절반에
  실려야 하는지 모호해져서, 후속 readahead가 두 번 뜨거나 안 뜬다.

**실험 대상이 되는 행동 하나:** `ra_pages` 페이지짜리 read-around IO
하나를 `n/2 + n/2`로 분리한다. 이 행동이 각 대가에 어떻게 작용하는지가
서로 다르다:

- (a)에는 **약**이다 — 첫 절반이 두 번째보다 먼저 돌아오니 fault
  페이지가 더 빨리 도착한다;
- (b)에는 **약이 아니다** — 두 절반이 여전히 같은 `ra_pages` 페이지를
  가져온다; 두 번째 절반을 아예 취소해야만 준다;
- (c)를 **만들 수 있다** — 단, 마커를 정하는 mm 계층에서 쪼갤 때만.
  마커를 설정하는 `page_cache_ra_order`보다 아래인 block 계층에서
  쪼개면 마커는 mm이 둔 자리에 그대로 남아 (c)를 비켜간다. 이건
  확인할 사항이지 목표가 아니다.

즉 (a)가 분리를 하는 이유이고, (b)·(c)는 결과를 읽는 렌즈지 해결할
목표가 아니다.

**문제 문장.** read-around는 fault 페이지 하나를 위해 `n` 페이지를 한
IO로 묶기 때문에, fault 페이지가 speculative한 나머지의 완료를
기다린다. 이 IO를 `n/2 + n/2`로 분리하면 두 가지를 묻게 된다: (1)
fault 페이지의 지연이 줄어드는가, (2) 그 분리가 커널을 실제로
관통하는가 — `mm/filemap.c` -> `mm/readahead.c` -> `block/bio.c` ->
`block/blk-merge.c` -> `block/blk-mq.c` -> device — 아니면 어느 계층이
다시 병합/축약해버리는가. (2)가 핵심 산출물이며(분리가 계층별로 어디서
유지되고 어디서 깨지는지 관측), (1)은 "이득이 있는가" 확인이다.

## 목표

"완료" 상태:

- `do_sync_mmap_readahead()`의 `ra_pages` 페이지 read-around IO가 하나가
  아니라 두 개(`n/2 + n/2`)로 발행된다.
- 각 계층(`filemap.c`, `readahead.c`, `bio.c`, `blk-merge.c`,
  `blk-mq.c`)의 계측이, 실제로 부팅된 커널에서, 두 IO 형태가 에뮬레이션
  NVMe 장치까지 어디서 유지되고 (있다면) 어느 계층이 다시 하나의
  request로 병합하는지를 보여준다.
- 경합 워크로드(큰 순차 mmap 순회 + 다른 파일에 무작위 단일 페이지
  mmap fault)를 split 커널과 baseline에서 각각 돌려, fault 페이지 지연을
  비교 — (a)에 대한 근거.
- 결과를 세 렌즈로 모두 서술: 분리가 (a)를 개선하는가, over-read는
  예측대로 (b) 그대로인가, 분리가 mm 계층 아래라 async 마커가 (c)
  그대로 유지되는가.

명시적으로 범위 밖:

- `do_sync_mmap_readahead()`의 THP 강제 경로와 `VM_EXEC` 경로 — 순수
  read-around `else` 분기만 건드린다.
- async readahead 재설계. (c)는 관측·보고만 하고 고치지 않는다.
- over-read를 실제로 줄이는 것(예: 두 번째 절반 취소) — (b)를 고치겠지만
  "분리"와는 다른 행동이다.
- 업스트림 수준의 완성도: 패치 제출이 아니라 학습·관측 과제다.

## 설계

- 정확히 어디를 경계로 나눌 것인가 — 어떤 단계를 어떤 단계로부터 분리하는가?
- 그러기 위해 어떤 새 인터페이스/자료구조가 필요한가?
- `block/blk-merge.c`, `block/bio.c`, `block/blk-mq.c`의 기존 병합 로직과
  어떻게 상호작용하는가?

## 평가

- 어떤 워크로드에서 효과(혹은 무효과)가 드러나는가?
- 무엇을 근거로 "성공했다"고 판단할 것인가?
- 비교 기준(baseline)은 무엇인가?

## 진행 상황

- 관측 루프(수정 -> sync -> 빌드 -> 설치 -> 부팅 -> 트리거 -> dmesg)
  구축·검증 완료. 게스트는 `7.1.2-rasplit`(#4) 부팅 중이고, read-around
  분기의 `RAROUND` 센서가 일반 시스템 활동에서 계속 찍힌다(게스트
  `dmesg`에 수백 줄). 경로가 end-to-end로 도달 가능함을 확인.
- 문제·목표 정의 완료(이 문서). 설계·평가 미착수.
- 분리 경계 결정(교수님 조언, 2026-09): **block 계층**. 조언은 두
  갈래 — 제대로 된 설계: mm이 요청에 "read-around" 태그를 달고 block
  계층이 태그로 split; 빠른 테스트: 태그를 건너뛰고 block 계층이 고정
  read-around IO 크기를 매칭해 무조건 2등분. 1단계 = 빠른 테스트
  (`bio_split_rw()`에서 `ra_pages * PAGE` 감지, 절반으로 캡); 2단계 =
  태그. `experiment-plan.md` §4-§5 참조.
- 완료: `nvme0n1`에 파일시스템 + 테스트 파일; `tools/mmaptest.c`(단일
  fault 타이머). 미결: 경합 워크로드로 확장; 1단계 캡; 2단계 태그
  전달.
