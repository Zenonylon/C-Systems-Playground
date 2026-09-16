# Phase 1 — M1 fault latency, baseline vs split (200 samples)

**Date:** 2026-09-16
**Question answered:** does the block-layer read-around split actually lower
mmap fault latency, measured with enough samples to talk about p50/p90/p99?
**Answer:** yes, but the effect is small — p50 −3.4%, p90 −9.0%, p99 −8.2%,
median paired difference −19 µs (95% CI [−27.5, −9.1] µs). This **replaces**
the −13…−58% preliminary figures in
[`phase1-block-split.md`](phase1-block-split.md), which turned out to be a
measurement artifact (see §Confounds).

## Setup

| Piece | Value |
|---|---|
| Kernels | `7.1.2-rasplit #5` (RASPLIT probes, no split) vs `#6` (probes + split) |
| Workload | `taskset -c 1 mmaptest --samples 200 --seed 1` on `/mnt/raround/rnd.bin` (512 MiB) |
| Samples used | 195 (first 5 dropped as warmup) |
| Pairing | same seed → both kernels fault the identical page sequence, verified row-by-row |
| Host cache | **bypassed** — QEMU nvme drive opened `cache=none,aio=native` |
| Console | **quiet** — `dmesg -n 1` on both kernels before the run |
| Background | guest `apt-daily{,-upgrade}.timer` disabled + masked, `APT::Periodic` set to 0 |
| Per run | `drop_caches=3`, `dmesg -C`, 60 s settle after boot |
| Data | [`data/phase1-m1-latency.csv`](data/phase1-m1-latency.csv) |

## Result (M1)

| | baseline `#5` | split `#6` | Δ |
|---|---:|---:|---:|
| p50 | 505,472 ns | 488,458 ns | −3.4% |
| p90 | 693,630 ns | 630,905 ns | −9.0% |
| p95 | 731,379 ns | 672,261 ns | −8.1% |
| p99 | 821,997 ns | 754,930 ns | −8.2% |
| mean | 538,882 ns | 517,097 ns | −4.0% |
| stdev | 99,990 ns | 90,714 ns | −9.3% |
| max | 933,799 ns | 1,108,947 ns | +18.8% |

Paired (same pgoff, same order): split faster in **126 / 195 (64.6%)**.
Median paired difference **−19,132 ns**, mean **−21,784 ns**; bootstrap 95%
CI of the median (2000 resamples) **[−27,548, −9,052] ns**, which excludes 0.

**Reading it.** H1 survives, but weakly. The split helps the tail more than
the middle (p90/p99 ≈ −8…−9% vs p50 −3.4%), which is the shape H1 predicts:
the fault page stops waiting behind the whole 128 KiB transfer. It is not
the dramatic win the n=5 run suggested. The per-sample distributions overlap
heavily — a third of samples are *slower* under split — so on this emulated
device the split is a small, real, tail-weighted improvement, not a
qualitative change.

## Confounds found and removed

Two artifacts had to be eliminated before the comparison meant anything.
Both inflated the apparent benefit of the split in the Phase 1 n=5 run.

**1. Host page cache (the big one).** QEMU was launched with no `cache=`
option on the nvme drive, so `nvme_disk.raw` lived in the *host's* page
cache. The guest's `drop_caches` and the per-sample `POSIX_FADV_DONTNEED`
only clear the *guest* cache — a guest read that misses guest cache can
still be served from host RAM instead of the emulated device. Because every
run uses `--seed 1`, every run touches the same page set, so each run warms
the host cache for the next one, and **whichever kernel is measured last
wins**:

| run | kernel | host cache state | p50 |
|---|---|---|---:|
| 1 | split `#6` | cold | 648,583 ns |
| 2 | split `#6` | warmed by run 1 | 632,897 ns |
| 3 | baseline `#5` | warmed by runs 1–2 | **212,336 ns** |

That is a 3x "baseline wins" produced entirely by run order
([`data/phase1-m1-hostcache-confound.csv`](data/phase1-m1-hostcache-confound.csv)).
Fix: relaunch QEMU with `cache=none,aio=native` on the nvme drive, so guest
IO goes to the image with O_DIRECT. Baseline p50 went 212,336 → 510,816 ns
once the host cache was out of the path.

This also explains the Phase 1 preliminary numbers. Those 5 samples were
taken baseline-first, split-second under the same host-cache-enabled setup,
so the split run read a cache the baseline run had just warmed — exactly the
condition that makes the second kernel look faster.

**2. Serial-console printk, asymmetric between the kernels.** Both kernels
carry the `RASPLIT/*` probes, but the split kernel emits 3 more lines per
fault (two `newrq` + two `issue` + one `bio_split`, vs one each): 1400 vs
800 ring-buffer lines over 200 samples. The guest console is
`ttyS0,115200n8`, and console output is on the measured path. Cost, measured
on `#6` by rerunning with `dmesg -n 1`
([`data/phase1-m1-console-cost.csv`](data/phase1-m1-console-cost.csv)):

| split `#6` | console on | console quiet | Δ |
|---|---:|---:|---:|
| p50 | 513,195 ns | 488,458 ns | −4.8% |
| p90 | 689,457 ns | 630,905 ns | −8.5% |
| p99 | 1,341,860 ns | 754,930 ns | −43.7% |

The tail is dominated by it. Since the handicap falls on the split kernel,
leaving it in would understate the split. Fix: `dmesg -n 1` on both kernels.
With console printk enabled on both, the same comparison reads p50 +0.5% /
p90 −2.1% / paired win rate 51.3% — i.e. indistinguishable from noise, the
split's small gain eaten by its own extra probe output.

**3. Background apt (caught, not quantified).** The guest's
`unattended-upgrade` ran concurrently with the first attempt, installing 36
packages including a kernel. That run was discarded and the timers were
disabled and masked for all subsequent runs.

## Residual limitations

- The split kernel still executes 600 more ring-buffer printks than baseline
  even with the console quiet. Cheaper than console output but not free, and
  it still handicaps the split — the true effect may be marginally larger
  than measured. A probe-free pair of builds would settle it.
- One run per kernel. Run-to-run variance is not characterized; the
  bootstrap CI covers sampling within a run, not between runs.
- QEMU emulated NVMe on a WSL2 host. Queue behaviour and timing are not
  representative of real hardware, and `cache=none` removes host caching but
  not host-side scheduling.
- No contention workload yet. H1 predicts a *larger* effect when a
  sequential read-ahead stream competes for the device — that is the
  condition where splitting should matter most, and it is untested.

## Next

1. Contention run: sequential mmap traversal antagonist + random faults,
   both kernels, quiet console — the workload `experiment-plan.md` §3 calls
   for and the one where H1 should show its largest effect.
2. Phase 1b (optional): move the cut so the fault page lands in the first
   half (136 + 120) and re-measure.
3. Phase 2: replace the 256-sector size match with an explicit read-around
   tag carried mm → bio.

---

# Phase 1 — M1 fault 지연, baseline vs split (200샘플) (한국어)

**날짜:** 2026-09-16
**답한 질문:** block 계층 read-around split이 실제로 mmap fault 지연을
낮추는가? p50/p90/p99를 말할 수 있을 만큼의 표본으로.
**답:** 낮춘다, 다만 효과는 작다 — p50 −3.4%, p90 −9.0%, p99 −8.2%,
쌍별 차이 중앙값 −19 µs (95% CI [−27.5, −9.1] µs).
[`phase1-block-split.md`](phase1-block-split.md)의 −13…−58% 예비 수치는
측정 아티팩트였으므로 이 문서가 **대체**한다 (§교란 요인 참조).

## 셋업

| 항목 | 값 |
|---|---|
| 커널 | `7.1.2-rasplit #5` (프로브만) vs `#6` (프로브 + split) |
| 워크로드 | `taskset -c 1 mmaptest --samples 200 --seed 1`, `/mnt/raround/rnd.bin` (512 MiB) |
| 사용 샘플 | 195개 (앞 5개는 warmup으로 버림) |
| 페어링 | 같은 seed → 두 커널이 동일한 페이지 순서를 fault, 행 단위로 확인 |
| 호스트 캐시 | **우회** — QEMU nvme 드라이브를 `cache=none,aio=native`로 오픈 |
| 콘솔 | **조용히** — 두 커널 모두 측정 전 `dmesg -n 1` |
| 백그라운드 | 게스트 `apt-daily{,-upgrade}.timer` disable + mask, `APT::Periodic` 0 |
| 매 실행 | `drop_caches=3`, `dmesg -C`, 부팅 후 60초 안정화 |
| 데이터 | [`data/phase1-m1-latency.csv`](data/phase1-m1-latency.csv) |

## 결과 (M1)

| | baseline `#5` | split `#6` | Δ |
|---|---:|---:|---:|
| p50 | 505,472 ns | 488,458 ns | −3.4% |
| p90 | 693,630 ns | 630,905 ns | −9.0% |
| p95 | 731,379 ns | 672,261 ns | −8.1% |
| p99 | 821,997 ns | 754,930 ns | −8.2% |
| 평균 | 538,882 ns | 517,097 ns | −4.0% |
| 표준편차 | 99,990 ns | 90,714 ns | −9.3% |
| 최댓값 | 933,799 ns | 1,108,947 ns | +18.8% |

쌍별(같은 pgoff, 같은 순서): **195개 중 126개(64.6%)**에서 split이 빠름.
쌍별 차이 중앙값 **−19,132 ns**, 평균 **−21,784 ns**, 중앙값의 부트스트랩
95% CI(2000회 재표본) **[−27,548, −9,052] ns** — 0을 포함하지 않는다.

**해석.** H1은 살아남았지만 약하다. 가운데(p50 −3.4%)보다 꼬리(p90/p99
≈ −8…−9%)에서 더 크게 줄었는데, 이건 H1이 예측하는 모양이다 — fault
페이지가 128 KiB 전송 전체를 기다리지 않게 되니까. 다만 n=5가 시사했던
극적인 승리는 아니다. 두 분포는 크게 겹치고 샘플의 1/3은 오히려 split이
느리다. 이 에뮬레이션 장치 위에서 split은 **작고, 실재하며, 꼬리에 치우친**
개선이지 질적인 변화가 아니다.

## 찾아내서 제거한 교란 요인

비교가 의미를 가지려면 두 가지 아티팩트를 먼저 없애야 했다. 둘 다 Phase 1
n=5 측정에서 split의 이득을 부풀리는 방향으로 작용했다.

**1. 호스트 페이지 캐시 (결정적).** QEMU를 nvme 드라이브에 `cache=` 옵션
없이 띄웠기 때문에 `nvme_disk.raw`가 *호스트* 페이지 캐시에 올라가 있었다.
게스트의 `drop_caches`와 샘플마다의 `POSIX_FADV_DONTNEED`는 *게스트* 캐시만
비운다 — 게스트 캐시를 미스한 읽기가 에뮬레이션 장치가 아니라 호스트 RAM에서
처리될 수 있다. 모든 실행이 `--seed 1`이라 같은 페이지 집합을 건드리므로 앞
실행이 뒤 실행을 위해 호스트 캐시를 데워주고, 결국 **마지막에 측정한 커널이
이긴다**:

| 실행 | 커널 | 호스트 캐시 상태 | p50 |
|---|---|---|---:|
| 1 | split `#6` | 차가움 | 648,583 ns |
| 2 | split `#6` | 1번이 데움 | 632,897 ns |
| 3 | baseline `#5` | 1~2번이 데움 | **212,336 ns** |

순전히 실행 순서만으로 만들어진 3배짜리 "baseline 승리"다
([`data/phase1-m1-hostcache-confound.csv`](data/phase1-m1-hostcache-confound.csv)).
해결: nvme 드라이브를 `cache=none,aio=native`로 QEMU 재기동 → 게스트 IO가
O_DIRECT로 이미지에 도달. 호스트 캐시를 경로에서 빼자 baseline p50이
212,336 → 510,816 ns로 올라갔다.

Phase 1 예비 수치도 이걸로 설명된다. 그 5개 샘플은 같은(호스트 캐시가 켜진)
환경에서 baseline 먼저, split 나중에 측정됐다. 즉 split 실행은 baseline이
방금 데워놓은 캐시를 읽은 것이고, 이건 두 번째 커널이 빨라 보이게 만드는
바로 그 조건이다.

**2. 시리얼 콘솔 printk — 두 커널에 비대칭.** 두 커널 다 `RASPLIT/*` 프로브를
갖고 있지만, split 커널은 fault당 3줄을 더 뱉는다(`newrq` 2개 + `issue` 2개 +
`bio_split` 1개 vs 각 1개): 200샘플 기준 1400줄 vs 800줄. 게스트 콘솔은
`ttyS0,115200n8`이고 콘솔 출력은 측정 경로 위에 있다. `#6`에서 `dmesg -n 1`로
다시 돌려 측정한 비용
([`data/phase1-m1-console-cost.csv`](data/phase1-m1-console-cost.csv)):

| split `#6` | 콘솔 켬 | 콘솔 끔 | Δ |
|---|---:|---:|---:|
| p50 | 513,195 ns | 488,458 ns | −4.8% |
| p90 | 689,457 ns | 630,905 ns | −8.5% |
| p99 | 1,341,860 ns | 754,930 ns | −43.7% |

꼬리는 사실상 이게 지배한다. 이 핸디캡이 split 쪽에만 얹히므로 그대로 두면
split을 과소평가하게 된다. 해결: 두 커널 모두 `dmesg -n 1`. 참고로 콘솔을
양쪽 다 켠 채로 비교하면 p50 +0.5% / p90 −2.1% / 쌍별 승률 51.3% — 노이즈와
구별되지 않는다. split의 작은 이득이 자기 자신의 추가 프로브 출력에 먹힌 것.

**3. 백그라운드 apt (발견했고, 수치화는 안 함).** 첫 시도와 동시에 게스트의
`unattended-upgrade`가 커널을 포함한 36개 패키지를 설치하고 있었다. 해당
실행은 폐기했고, 이후 모든 실행을 위해 타이머를 disable + mask 했다.

## 남은 한계

- 콘솔을 꺼도 split 커널은 baseline보다 링버퍼 printk를 600줄 더 실행한다.
  콘솔 출력보다야 싸지만 공짜는 아니고, 여전히 split에 불리하다 — 실제 효과는
  측정치보다 조금 더 클 수 있다. 프로브를 뺀 빌드 한 쌍이면 확정할 수 있다.
- 커널당 1회 실행. 실행 간 변동은 파악되지 않았다. 부트스트랩 CI는 한 실행
  안에서의 표본 변동만 다룬다.
- WSL2 호스트 위의 QEMU 에뮬레이션 NVMe. 큐 동작과 타이밍이 실물과 다르고,
  `cache=none`은 호스트 캐싱만 없앨 뿐 호스트 쪽 스케줄링까지 없애진 않는다.
- 아직 경합 워크로드가 없다. H1은 순차 read-ahead 스트림이 장치를 두고 경쟁할
  때 효과가 *더 커진다*고 예측한다. split이 가장 의미 있을 조건인데 미측정.

## 다음

1. 경합 실행: 순차 mmap 순회 적대자 + 무작위 fault, 두 커널, 콘솔 끄고 —
   `experiment-plan.md` §3이 요구하는 워크로드이자 H1의 효과가 가장 커야 할
   조건.
2. Phase 1b (선택): fault 페이지가 앞 절반에 오도록 경계 이동(136 + 120) 후
   재측정.
3. Phase 2: 256섹터 크기 매칭을 mm → bio로 전달되는 명시적 read-around
   태그로 교체.
