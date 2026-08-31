# Plan

*(template — fill in as the design takes shape)*

## Problem

- What specifically breaks down or under-performs in the current
  `do_sync_mmap_readahead()` read-around path?
- Why does splitting it (vs. tuning it, vs. leaving it alone) help?

## Goals

- What does "done" look like for this project?
- What's explicitly out of scope?

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

- Current state.
- Open questions / unresolved decisions.

---

# 계획 (한국어)

*(템플릿 — 설계가 잡히는 대로 채워나갈 것)*

## 문제

- 지금 `do_sync_mmap_readahead()`의 read-around 경로에서 구체적으로 뭐가
  문제이거나 비효율적인가?
- (튜닝이나 현행 유지가 아니라) 분리(split)하는 게 왜 도움이 되는가?

## 목표

- 이 프로젝트에서 "완료"란 어떤 상태인가?
- 명시적으로 범위 밖인 건 무엇인가?

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

- 현재 상태.
- 아직 정하지 못한 질문들.
