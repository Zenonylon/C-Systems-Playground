# Stage 진행 근거: 결과로 보는 Stage 0 → 1 → 2 → 3

이 문서는 `results/baseline.csv`(Stage 0), `results/stage1.csv`,
`results/interrupted.csv`, `results/interrupted_capacity.csv`(Stage 1),
`results/adaptive.csv`, `results/mixed.csv`(Stage 2),
`results/read_around.csv`(Stage 3) 실측 데이터를 근거로, 왜 Stage 0에서
Stage 1로, Stage 1에서 Stage 2로, 그리고 Stage 2에서 Stage 3로 넘어가야
하는지를 정리한다.
"다음 단계가 필요해 보여서"가 아니라, 이전 단계의 결과 자체가 다음 단계의 필요성을
숫자로 보여주는지를 검증하는 데 목적이 있다.

---

## Stage 0 → Stage 1

### Stage 0가 측정한 것

`baseline_bench`는 같은 1GB 파일에 대해 page cache 경로(`direct=0`)와 `O_DIRECT`
경로(`direct=1`)의 처리량을 sequential/random 접근 각각에 대해 측정했다.

| pattern    | direct | mean_elapsed_sec | mean_throughput_mb_s |
|------------|--------|-------------------|------------------------|
| sequential | 0 (page cache) | 0.324171 | 3158.82 |
| sequential | 1 (O_DIRECT)   | 27.793102 | 36.84 |
| random     | 0 (page cache) | 0.259085 | 3952.37 |
| random     | 1 (O_DIRECT)   | 41.999106 | 24.38 |

### 근거

- sequential 기준 O_DIRECT는 page cache 대비 **85.7배** 느리다 (3158.82 / 36.84).
- random 기준으로는 **162배** 느리다 (3952.37 / 24.38).
- 이 차이는 캐시가 없을 때 매 요청마다 실제 storage I/O(여기서는 O_DIRECT read)를
  타는 비용이며, README가 애초에 정의한 "cache miss의 비용, 그리고 readahead가
  회수할 수 있는 가치"에 해당하는 숫자다.

즉 Stage 0는 "캐시가 없으면 얼마나 손해인가"를 구체적인 배수로 확정했다. 이 손해가
실제로 크다는 게 확인됐으니, Stage 1에서 그 손해를 얼마나 회복할 수 있는지를
측정할 근거가 생긴 것이다. Stage 1의 벤치마크가 파일을 O_DIRECT로 여는 이유
(`main.c`의 `open(file_path, O_RDONLY | O_DIRECT)`)도 여기서 나온다 — OS page
cache가 개입해서 직접 만든 `buffer_pool`의 효과를 가려버리면 Stage 0에서 측정한
"진짜 cache miss 비용"을 기준으로 비교할 수 없기 때문이다.

---

## Stage 1 → Stage 2

### Stage 1이 측정한 것

`stage1_bench`는 512MB 파일, `buffer_pool` capacity 128 blocks 기준으로 fixed-window
read-ahead의 window(0/4/16/64)를 sequential/random 각각에 대해 스윕했다.

| pattern    | window | mean_elapsed_sec | mean_throughput_mb_s | mean_hit_rate |
|------------|--------|-------------------|------------------------|----------------|
| sequential | 0  | 14.804689 | 34.58    | 0.0000 |
| sequential | 4  | 0.045522  | 11247.24 | 1.0000 |
| sequential | 16 | 0.046689  | 10966.24 | 1.0000 |
| sequential | 64 | 0.045946  | 11143.51 | 1.0000 |
| random     | 0  | 21.998299 | 23.27    | 0.0000 |
| random     | 64 | 22.114406 | 23.15    | 0.0000 |

### 근거 1 — 벤치마크 자체의 sanity check

`window=0`(readahead 비활성)일 때 sequential 처리량은 34.58 MB/s로, Stage 0의
O_DIRECT sequential 값(36.84 MB/s)과 거의 일치한다. 즉 `buffer_pool` 인프라 자체는
readahead 없이 아무 이득도, 손해도 만들지 않는다 — Stage 1에서 보이는 성능 향상은
전부 `readahead_on_access`의 prefetch 로직 덕분이라고 원인을 좁혀서 말할 수 있다.

### 근거 2 — fixed-window가 sequential에서 통하는 이유

`window >= 4`가 되는 순간 hit rate가 100%로 뛰고, 처리량이 34.58 MB/s →
~11000 MB/s로 **약 325배** 상승한다. Stage 0에서 확인한 O_DIRECT 손실을 사실상
전부 회수했고(page cache baseline인 3158.82 MB/s보다도 3.5배 높다 — 실제
페이지캐시 조회보다 이 프로젝트의 in-memory linear-scan lookup이 더 싸기 때문),
"순차 접근에서는 read-ahead가 유효하다"는 가설이 숫자로 뒷받침된다.

### 근거 3 — window을 키워도 더 얻을 게 없다 (이 트레이스 한정)

window 4 → 16 → 64로 늘려도 처리량은 11247 → 10966 → 11143 MB/s로 사실상 평평하다
(오차범위 수준, 2~3% 이내). 이 벤치마크의 트레이스(완전히 균일한 순차 접근, capacity
128 blocks)에서는 window 크기를 아무리 튜닝해도 이미 window=4에서 얻을 수 있는
이득을 다 얻은 상태라는 뜻이다.

### 근거 4 — random에서는 window가 아예 작동하지 않는다

random 패턴에서는 window=0과 window=64가 처리량(23.27 vs 23.15 MB/s)과 hit
rate(둘 다 0.0000) 모두 사실상 동일하다. `readahead.c`의 순차 감지 조건
(`block_num == last_block + 1`)이 random 접근에서는 절대 참이 되지 않으므로,
window 값을 아무리 키워도 prefetch 자체가 걸리지 않는다.

### 종합: 왜 Stage 2(adaptive read-ahead)가 필요한가

근거 2·3·4를 합치면 Stage 1의 window는 **실행 전에 사람이 미리 골라서 고정해두는
전역 상수**라는 게 드러난다:

- 트레이스가 처음부터 끝까지 순수하게 순차적이면, window를 4 이상으로만 맞히면
  그만이고 더 정교하게 튜닝할 필요가 없다 (근거 3).
- 트레이스가 순차적이지 않으면, window는 크든 작든 아무 효과가 없다 — 켜져 있어도
  꺼진 것과 같다 (근거 4).

즉 fixed window는 "정답을 미리 알고 있을 때"만 잘 작동하고, 실행 중에 접근 패턴이
바뀌는 상황(순차 구간과 랜덤 구간이 섞여 있거나, 순차 구간의 길이가 짧게 끊기는
경우)에는 대응할 방법이 없다 — 값을 실행 도중에 늘리거나 줄일 수 없기 때문이다.
README가 정의한 Stage 2("순차 hit이 이어지는 동안 window를 키우고, miss가 나면
줄인다")는 바로 이 지점 — window를 사람이 미리 정하는 대신 관측된 접근 패턴으로부터
실행 중에 유도하는 것 — 을 겨냥한다.

Stage 1의 벤치마크는 "끝까지 완전히 순차적인 트레이스"와 "완전히 랜덤인 트레이스"
두 극단만 테스트했다. 근거 3에서 보였듯, 순수 순차 트레이스에서는 작은 고정
window로도 이미 포화 상태라 adaptive가 더 잘할 여지가 없어 보인다 — 그래서 위
가설을 직접 검증하려면 순차 구간이 중간에 끊기는 혼합 워크로드가 필요했다.

### 근거 5 — `interrupted_bench`: burst 워크로드로 가설 검증

`interrupted_bench.c`(+ `interrupted_bench.sh`)를 새로 만들어, 짧은 순차 구간
(burst) 사이에 랜덤 점프가 끼는 트레이스로 Stage 1의 fixed-window를 그대로 돌렸다.
512MB 파일, capacity 64 blocks(가장 큰 window와 동일하게 타이트하게 설정),
burst_len과 window를 각각 {4,16,64,256} x {0,4,16,64}로 스윕(`results/interrupted.csv`).

| burst_len | window=0 | window=4 | window=16 | window=64 |
|-----------|----------|----------|-----------|-----------|
| 4   | 0.0002 | 0.5002 | 0.5002 | 0.5002 |
| 16  | 0.0001 | 0.8750 | 0.8749 | 0.8751 |
| 64  | 0.0000 | 0.9688 | 0.9688 | 0.9688 |
| 256 | 0.0000 | 0.9921 | 0.9920 | 0.9920 |

(표 값은 `mean_hit_rate`.)

**세웠던 가설은 기각됐다.** 처음 예상은 "window가 burst_len보다 크면, burst가
끝난 뒤에도 남아 쓸모없어진 prefetch 블록들이 FIFO eviction으로 다른 유용한
블록을 밀어내 hit rate가 떨어질 것"이었다. 그런데 실측 결과 hit rate는
**window 값과 무관하게 완전히 평평하다** — window=4든 64든 같은 burst_len에서는
소수점 넷째 자리까지 사실상 같다.

원인은 벤치마크 구조에 있다: `buffer_pool_init`이 burst마다가 아니라 **run
전체에 한 번만** 호출되기 때문에, 한 burst에서 낭비된 prefetch 블록은 그
burst 안에서 바로 문제를 일으키지 않고, 이후 수백~수천 개의 burst를 거치며
서서히 FIFO로 밀려날 뿐이다. 게다가 이 트레이스는 같은 영역을 다시 방문하지
않으므로, 밀려난 블록을 나중에 다시 찾다가 손해 보는 경우 자체가 없다. 즉
"오염"이 관측 가능한 손해로 이어지려면 캐시 재방문(locality)이 있어야 하는데,
지금 워크로드에는 그게 없다.

대신 실측이 실제로 보여준 건 **window와 무관한 고정 하한선**이다: hit rate는
정확히 `(burst_len - 2) / burst_len` 이다 (burst_len=4 → 0.5, 16 → 0.875, 64 →
0.96875, 256 → 0.99219 — 표의 값과 일치). 이유는 `readahead_on_access`의 감지
조건 자체에 있다: 어떤 블록이 "직전 블록 + 1"이어야 순차로 인정되므로, 각
burst의 첫 블록(직전 기록 없음/불일치)과 둘째 블록(이제야 순차로 인정되지만
prefetch는 아직 안 걸려 있음)은 window 크기와 무관하게 항상 miss다. prefetch는
셋째 블록부터만 커버한다.

### 근거 6 — `capacity < window`: 원래 오염 가설이 실제로 맞았던 조건

근거 5까지는 `interrupted_bench`가 `main.c`와 같은 `capacity_blocks >= window`
체크를 그대로 물려받고 있었다. 이 체크는 `buffer_pool.c`/`.h`(코어 로직)에는
없는, harness(CLI 인자 검증) 쪽 방어 코드일 뿐이라는 걸 확인한 뒤(`buffer_pool_init`은
capacity만 알고 window라는 개념 자체를 모른다), `interrupted_bench.c`에서 그
체크만 없애고(`capacity_blocks == 0`만 남김, 코어 로직은 미변경)
`benchmarks/interrupted_capacity_bench.sh`로 `window=64`, `burst_len=256`을
고정한 채 `capacity_blocks`를 8/16/32/64/128로 스윕했다
(`results/interrupted_capacity.csv`).

| capacity_blocks | mean_hit_rate |
|------------------|----------------|
| 8   | 0.0000 |
| 16  | 0.0001 |
| 32  | 0.0000 |
| 64  | 0.9920 |
| 128 | 0.9920 |

`capacity < window`(8/16/32)에서는 hit rate가 **완전히 0으로 붕괴**하고,
`capacity >= window`(64/128)에서는 근거 5의 ceiling(0.9920)으로 곧장 복귀한다 —
중간값 없이 window 지점에서 정확히 끊어지는 절벽이다. 근거 5에서 기각했던 "오염
가설"이 사실은 틀린 게 아니라 **조건이 틀렸던 것**이었다: capacity가 window보다
작아야만(그리고 burst가 그 손해를 눈에 띄게 만들 만큼 길어야만) 드러나는
현상이었다.

#### 왜 완만한 경사가 아니라 절벽인가

직관적으로는 "capacity가 window의 절반이면 hit rate도 절반쯤" 나올 것 같지만,
실제로는 그렇지 않다. 이유는 **FIFO eviction 순서와 "다음에 필요한 순서"가
정확히 같은 순서로, 일정한 간격을 두고 나란히 진행되기 때문**이다.

`prefetch_range`가 `window`개를 연속으로 insert하면(`buffer_pool.c:82-98`), ring이
이미 꽉 차 있는 상태(반복되는 burst 사이에서 거의 항상 그렇다)에서는 마지막
`capacity`개만 살아남는다. `window=64, capacity=32`라면, flood가 `B`부터
시작했을 때 살아남는 건 `B+32 .. B+63`이다. 커서는 아직 `B`에 있으니 당장은
전부 miss고, `buffer_pool_insert`는 꽉 찼을 때 `evict_pos`가 가리키는 **가장
오래전에 넣은 것**부터 지운다(`buffer_pool.c:74-79`). flood 직후 가장 오래전에
넣은 것은 바로 `B+32` — 커서가 `B`를 miss로 처리하며 insert하는 그 순간 evict되는
게 하필 `B+32`다. 커서가 `B+1`을 처리할 때 evict되는 건 `B+33`. 즉 **"평가받는
시점"과 "evict되는 시점" 사이의 간격은 항상 정확히 `window - capacity`번의
접근**이고, 이 값이 0보다 크기만 하면(=`window > capacity`) 커서가 그 블록에
도달하기 *전에* 무조건 먼저 evict된다. 간격이 1칸이어도(`window-capacity=1`)
결과는 같다 — "간발의 차이로 살아남는" 경우가 수식상 존재하지 않는다.

반대로 `capacity >= window`가 되면 이 간격이 0 이하가 되어 경쟁 자체가 사라지고,
flood로 들어온 블록이 evict되기 전에 전부 소비된다 — 그래서 절벽이 생긴다.
"완만한 경사"가 나오려면 eviction 정책이 "곧 쓸 블록"을 우대해야 하는데(LRU 등),
지금 `buffer_pool`은 순수 FIFO라 그런 우대가 없다.

### 종합 (재수정): Stage 2가 실제로 겨냥해야 하는 지점

이제 근거 5·6을 합치면 fixed-window read-ahead가 실패하는 경로가 두 가지로
분리된다:

1. **window가 너무 작을 때 (근거 5)**: burst당 2-miss 하한선(`(burst_len-2)/burst_len`)에
   막힌다 — window를 아무리 키워도(`capacity >= window`인 한) 못 넘는 벽이다.
2. **window가 너무 클 때, capacity 대비 (근거 6)**: FIFO eviction과 순차 진행 속도가
   정확히 보조를 맞추면서 prefetch가 자기 자신을 완전히 지워버려 hit rate가 0으로
   붕괴한다 — 절벽이라 "약간 크게" 잡는 정도로는 괜찮고, 아주 조금만 capacity를
   넘어도 파국이다.

즉 Stage 1의 fixed window는 **정확히 맞히지 못하면 위아래 양쪽 모두에서 실패하는
좁은 유효 구간**을 갖는다 — 작으면 사후 감지 비용에 막히고, 크면(capacity 대비)
FIFO eviction에 자멸한다. 이 유효 구간(대략 `burst_len` 근방, 그리고 반드시
`<= capacity`)은 워크로드가 바뀌면 같이 움직이는데, 사람이 실행 전에 고정값
하나로 골라야 하는 게 Stage 1의 구조다. README의 Stage 2 정의("hit이 이어지면
키우고 miss면 줄인다")는 바로 이 지점 — 유효 구간을 실행 중 관측으로 스스로
찾아가게 만드는 것 — 을 겨냥한다. 다만 grow/shrink만으로 근거 6의 절벽(FIFO
eviction과 capacity의 상호작용)까지 없앨 수 있는지는 별개 문제로 남는다 — window를
capacity 이하로만 유지한다면 절벽 자체를 피할 수 있으니, 상식적으로는 adaptive
window가 "capacity를 넘지 않는 선에서" 커지도록 설계하면 해결되지만, 이건 아직
구현·검증되지 않았다.

### 이 근거의 한계 (정직하게 짚어둘 것)

- 트레이스에 재방문(locality)이 없다는 건 이 벤치마크의 설계 선택이지 워크로드의
  본질적 특성은 아니다 — 재방문이 있는 트레이스에서는 결과가 달라질 수 있다.
- **(갱신, Stage 2 구현 후 검증 완료)** Stage 2("hit 이어지면 키우고 miss면
  줄인다")가 근거 5의 "burst당 2-miss 하한선"과 근거 6의 "capacity 초과 시
  절벽"을 실제로 줄여주는지 `results/adaptive.csv`, `results/mixed.csv`로
  확인했다. 결과는 절반만 기대대로였다:

  - **(a) 2-miss 하한선은 그대로다.** `adaptive_bench`(burst_len x
    max_window 스윕) 결과, burst_len별 hit rate가 `(burst_len-2)/burst_len`과
    소수점 단위까지 거의 일치한다 — burst_len=4→0.5000~0.5008,
    16→0.8750~0.8752, 64→0.9688 고정, 256→0.9920 고정, max_window
    (4/16/64/256) 값과 무관하게 편차는 오차 범위(0.1%p 이내) 수준이다. 원인은
    `adaptive_readahead.c:17`의 순차 감지 조건(`block_num == last_block +
    1`)이 Stage 1과 동일하다는 데 있다 — 각 burst의 첫 두 블록이 무조건
    miss인 건 이 감지 조건 자체의 한계이지 window 크기(고정이든 적응형이든)
    와 무관하므로, Stage 2가 window를 동적으로 키워도 이 부분은 손을 댈 수
    없다. 즉 이 하한선은 Stage 1/2가 공유하는 구조적 한계다.
  - **(b) capacity 초과 절벽 — mixed_bench만으로는 착시였고, 직접 실측하니
    여전히 존재한다 (단, 모양이 다르다).** `mixed_bench`(`results/mixed.csv`)
    에서 `fixed_window=256`(capacity=64 초과)일 때 Stage 1은 hit rate가
    0.9787→0.0000으로 붕괴하지만 Stage 2(`min_window=4, max_window=64`)는
    0.9757을 유지했다. 하지만 `adaptive_readahead.c`/`.h`를 보면 이 알고리즘
    자체는 `capacity`라는 개념을 아예 모른다(`AdaptiveReadahead` 구조체에
    capacity 필드가 없다) — window를 `max_window`로 clamp할 뿐이고,
    `max_window <= capacity`를 지키는 건 호출자(`mixed_bench.c`)의 설정
    책임이었다. `interrupted_capacity_bench.sh`처럼 `max_window(64)`는
    고정하고 `capacity_blocks`를 8/16/32/64/128로 스윕하는
    `adaptive_capacity_bench.sh`(`results/adaptive_capacity.csv`)를 새로
    만들어 `max_window > capacity` 케이스를 직접 실측했다:

    | capacity_blocks | mean_hit_rate |
    |------------------|----------------|
    | 8   | 0.0480 |
    | 16  | 0.1120 |
    | 32  | 0.2400 |
    | 64  | 0.9920 |
    | 128 | 0.9920 |

    절벽은 사라지지 않았다 — `capacity < max_window`(8/16/32)에서 여전히
    낮은 hit rate로 무너진다. 다만 Stage 1의 같은 실험(근거 6,
    `interrupted_capacity.csv`: capacity 8/16/32에서 hit rate가 정확히
    0.0000~0.0001)이 **수직 절벽**이었던 것과 달리, Stage 2는 capacity가
    커질수록 hit rate가 0.048→0.112→0.24로 **완만하게 오르는 경사**다. 이유는
    `adaptive_readahead.c:35`의 window 성장 방식에 있다 — window가
    `min_window(4)`에서 시작해 burst가 이어질 때마다 배수로 커지므로
    (4→8→16→32→64), capacity가 작을수록 window가 아직 capacity를 넘어서기
    전 단계(작은 window)에서 벌어들이는 hit이 일부 남는다. 즉 Stage 2는
    "capacity 초과를 막는다"가 아니라 "capacity를 넘기 전까지의 성장 구간
    만큼만 부분적으로 버틴다" — mixed_bench.sh 결과가 시사했던 "절벽을
    피했다"는 인상은 `max_window`가 처음부터 `capacity` 이하로 설정된
    한 가지 설정에서만 관찰된 것이었고, 근본적인 capacity-초과 실패 모드
    자체는 Stage 2에서도 해소되지 않았다.
- 근거 6의 절벽은 이 프로젝트의 eviction 정책이 순수 FIFO라는 점에 크게 의존한다
  — LRU 등 "곧 쓸 블록"을 우대하는 정책이었다면 결과가 달랐을 것이다. 이건
  `buffer_pool`의 설계 선택이며, 코어 로직을 바꾸는 문제라 이 문서에서는 결과로만
  남겨둔다.

---

## Stage 2 → Stage 3

### Stage 1/2가 남긴 한계 — Stage 3의 동기

`interrupted_bench`/`adaptive_bench`/`mixed_bench`는 전부 "순차 구간 + 랜덤
점프"로 구성된 트레이스만 다뤘다. `readahead`/`adaptive_readahead`의 순차 감지
조건(`block_num == last_block + 1`)은 랜덤 접근에서는 애초에 참이 될 수
없으므로(Stage 1→2 근거 4), 두 전략 모두 "다음 블록을 예측할 수 있는 순차
패턴"에만 적용 가능하다. 그러나 README가 정의한 세 번째 워크로드 부류 —
랜덤이지만 국소적으로 몰린 접근(예: page fault처럼 근처 페이지를 반복
방문하는 패턴) — 에는 두 전략 다 손을 못 댄다. Stage 3(`read_around`)는 이
지점을 겨냥한다: 블록 N에 접근하면 "다음 블록"이 아니라 "그 주변"
(N-radius ~ N+radius)을 채운다.

### 설계 — 독립된 `LruBufferPool`

Stage 1/2는 `BufferPool`(FIFO, `evict_pos` 회전 하나로 eviction)을 공유해서
썼지만, Stage 3는 `LruBufferPool`(`include/lru_buffer_pool.h`,
`src/lru_buffer_pool.c`)이라는 별도 구조체를 새로 만들어 썼다. 이유는
read-around가 겨냥하는 워크로드(같은 지역을 반복 방문)에서는 "가장 오래전에
넣은 것"보다 "가장 최근에 안 쓰인 것"을 밀어내는 게 더 맞는 eviction
정책이기 때문이다 — FIFO는 반복 재방문되는 hot 블록도 그냥 넣은 순서대로
밀어내 버려서, Stage 1→2 근거 6에서 지적한 "LRU였다면 결과가 달랐을 것"이라는
지점을 그대로 남겨두게 된다. `slots[]`와 나란히 `last_used[]`를 두고
eviction 시 최솟값을 선형 탐색하는 방식으로, 기존 `buffer_pool.c`의 "배열 +
선형 탐색" 스타일은 유지했다. Stage 1/2 코드와 벤치마크는 전혀 건드리지
않았다 — `Makefile`의 `SRC`/`INTERRUPTED_SRC`/`ADAPTIVE_SRC`/`MIXED_SRC`는
여전히 `buffer_pool.c`(FIFO)만 링크한다.

### 근거 1 — 첫 워크로드 생성기는 read-around의 전제를 트레이스에 담지 못했다

`read_around_bench.c`의 첫 버전은 좁은 region 안에서 i.i.d. 균등 난수로
블록을 뽑는 `generate_localized_random`을 썼다. `capacity_blocks(512) >=
region_width`로 캐시가 region 전체를 담을 만큼 넉넉했을 때는 `radius=0`
(prefetch 없음)만으로도 hit rate가 이미 0.97~0.99로 포화돼 있었고, radius를
키워도 처리량만 계속 떨어졌다(예: `region_width=256`일 때 radius 0→32에서
736→260 MB/s로 하락, hit rate는 0.9680→0.9995로 거의 안 오름).

`capacity_blocks(32) < region_width(256)`로 캐시를 좁혀 재확인했을 때도
결과는 같았다: radius 0/4/16/64 전부 hit rate가 0.10~0.13 근방에서 거의
그대로였다(radius=64는 오히려 0.1000으로 하락 — capacity보다 큰 prefetch
범위가 자기 자신을 밀어내는, Stage 1→2 근거 6과 같은 현상).

원인은 워크로드 자체에 있었다: i.i.d. 균등 추출은 "좁은 범위 안"이라는
조건은 만족하지만, "방금 접근한 블록의 이웃이 곧 다시 필요하다"는
read-around의 핵심 전제는 전혀 만족하지 않는다 — 연속된 두 접근이 서로
이웃이라는 보장이 없으므로, radius를 아무리 키워도 미리 채운 이웃 블록이
다음 접근과 우연히 맞아떨어질 확률은 균등분포 그대로일 뿐이다.

### 근거 2 — `generate_locality_walk`: 이웃이 실제로 다시 쓰이는 트레이스로 교체

`generate_locality_walk`(`benchmarks/read_around_bench.c`)는 i.i.d. 추출
대신, region 안에서 매 스텝마다 현재 위치를 `[-jitter, +jitter]` 범위로
이동시키는 bounded random walk로 바꿨다. 연속된 접근끼리 공간적으로
가깝다는 게 트레이스 구조 자체로 보장되고, `jitter`가 "다음 접근이 현재에서
얼마나 멀리 튈 수 있는가"의 기준점이 된다.

`capacity_blocks=64 < region_width=512`, `jitter=8`로 고정하고 `radius`를
스윕한 결과(`results/read_around.csv`):

| radius | mean_hit_rate | mean_throughput_mb_s |
|--------|----------------|------------------------|
| 0  | 0.7507 | 109.75  |
| 2  | 0.9409 | 414.73  |
| 4  | 0.9650 | 648.61  |
| 8  | 0.9999 | 3361.34 |
| 16 | 0.9999 | 1799.73 |
| 32 | 0.9999 | 379.59  |

- `radius=0`(순수 LRU, prefetch 없음)에서도 hit rate가 0.75인 건, capacity
  (64)가 region(512)보다 훨씬 작아도 랜덤워크 자체가 국소적이라 재방문이
  자주 일어나기 때문이다 — 그래도 워크가 캐시 범위를 벗어나면 miss가 난다.
- radius가 jitter(8)에 도달하는 순간 hit rate가 0.9999로 포화하고 처리량이
  정점(3361 MB/s)을 찍는다 — "다음 접근은 현재에서 최대 jitter만큼
  떨어진다"는 워크로드의 실제 구조와 radius가 정확히 맞아떨어지는 지점이다.
- radius를 jitter 이상으로 더 키워도(16, 32) hit rate는 이미 포화라 더 오를
  게 없고, 오히려 매 접근마다 더 넓은 범위를 훑는 prefetch 오버헤드 때문에
  처리량만 계속 떨어진다(radius=32는 capacity=64에 육박해 자기 자신을
  밀어내는 압력까지 겹친다 — 근거 1의 radius=64/capacity=32 붕괴와 같은
  방향의 현상이 아직 완전히 붕괴하기 전 단계로 나타난 것).

### 종합 — Stage 3가 보여준 것

Stage 1/2(순차 감지 기반 readahead)와 Stage 3(read-around)는 서로 다른
워크로드 부류에 반응한다 — 하나는 "다음 블록 예측 가능", 하나는 "최근 접근
근처가 다시 쓰임". 그리고 두 벤치마크 세트 모두 같은 모양의 패턴을 보인다:
**전략의 파라미터(window/radius)가 워크로드의 실제 구조(burst_len/jitter)와
맞아떨어지는 지점에서 최대 효과가 나고, 그 지점을 넘어서면 hit rate는
포화된 채 오버헤드(그리고 capacity 대비 너무 크면 self-eviction)만
늘어난다.**

### 이 근거의 한계

- `capacity_blocks(64)`가 `radius`의 최대 스윕값(32)보다 겨우 2배 크다 —
  radius=32에서 관측된 하락이 순수 "훑는 오버헤드" 때문인지 "capacity 대비
  radius가 너무 커서 생기는 self-eviction" 때문인지 이 데이터만으로는 완전히
  분리되지 않는다. Stage 1→2 근거 6의 절벽 실험처럼 `capacity_blocks`를
  별도로 스윕해 두 원인을 분리하는 게 다음 검증 지점이다.
- `jitter`를 고정값(8) 하나만 테스트했다 — jitter가 다른 값일 때도
  "radius == jitter 지점에서 포화"라는 패턴이 재현되는지는 아직 확인되지
  않았다.
- 이 워크로드(bounded random walk)는 page-fault 스타일 locality를 모사한
  것이지, 실제 page fault 트레이스나 커널 `mm/readahead.c`가 다루는 real
  workload와는 다르다 — Stage 5(커널과의 비교)가 이 간극을 다룰 자리다.
