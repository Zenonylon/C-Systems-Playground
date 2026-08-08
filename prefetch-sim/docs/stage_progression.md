# Stage 진행 근거: 결과로 보는 Stage 0 → 1 → 2

이 문서는 `results/baseline.csv`(Stage 0), `results/stage1.csv`,
`results/interrupted.csv`, `results/interrupted_capacity.csv`(Stage 1) 실측
데이터를 근거로, 왜 Stage 0에서 Stage 1로, Stage 1에서 Stage 2로 넘어가야
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
- Stage 2("hit 이어지면 키우고 miss면 줄인다")가 근거 5의 "burst당 2-miss
  하한선"과 근거 6의 "capacity 초과 시 절벽"을 실제로 줄여주는지는 아직
  검증되지 않았다 — Stage 2 구현 후 같은 `interrupted_bench` /
  `interrupted_capacity_bench` 스윕을 다시 돌려서 (a) hit rate가
  `(burst_len-2)/burst_len`보다 높아지는지, (b) window가 capacity를 넘어가도
  절벽 없이 완만하게(또는 아예 안 넘어가게) 동작하는지 확인하는 게 다음 검증
  지점이다.
- 근거 6의 절벽은 이 프로젝트의 eviction 정책이 순수 FIFO라는 점에 크게 의존한다
  — LRU 등 "곧 쓸 블록"을 우대하는 정책이었다면 결과가 달랐을 것이다. 이건
  `buffer_pool`의 설계 선택이며, 코어 로직을 바꾸는 문제라 이 문서에서는 결과로만
  남겨둔다.
