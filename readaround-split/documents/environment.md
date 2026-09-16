# Environment

## Layout

This directory (`readaround-split/`) holds the **tracked, editable copy**
of the kernel source files this project touches:

```
src/mm/filemap.c       do_sync_mmap_readahead() / do_async_mmap_readahead()
src/mm/readahead.c      page_cache_ra_order() and friends
src/block/bio.c
src/block/blk-merge.c
src/block/blk-mq.c
```

They started as an unmodified checkout of Linux v7.1.2
(`https://mirrors.edge.kernel.org/pub/linux/kernel/v7.x/linux-7.1.2.tar.xz`),
verified byte-identical against the pristine tarball before any edits.

These files are **not** buildable on their own — they're excerpts, not a
full source tree. Building and booting happens in a separate full checkout
at `~/linux-7.1.2` (large, untracked, host-only). `sync.sh` bridges the two.

## Setup

| Piece | Where |
|---|---|
| Full kernel source (buildable) | `~/linux-7.1.2` |
| QEMU boot command | `~/qemu-lab` (`ubuntu.qcow2` root disk + `nvme_disk.raw` emulated NVMe) |
| Guest root fs | ext4 on LVM (`ubuntu-vg/ubuntu-lv`), boots via GRUB — no `-kernel` fast-boot, GRUB entry selection is required because of LVM |
| Guest SSH | `ssh -p 2222 -i ~/.ssh/id_ed25519_qemu lenoa@localhost` (key-based, no password) |
| Guest sudo | passwordless (`/etc/sudoers.d/90-lenoa-nopasswd`, guest-only) |
| mmap trigger program | `tools/mmaptest.c` (this repo), compiled inside the guest |

The guest also has the original DPAS-patched 5.18 kernel installed
(`5.18.0-dpas`) from earlier NVMe-polling work; that's unrelated to this
project and still selectable in GRUB if needed.

## The observation loop

Every experiment is: **edit here -> sync -> build -> install -> boot -> trigger -> read dmesg.**

1. Edit the relevant file(s) in `src/` (`src/mm/*.c`, `src/block/*.c`).

2. Sync into the buildable tree:
   ```
   ./sync.sh                      # defaults to ~/linux-7.1.2
   ```

3. Build a `.deb` kernel package on the host:
   ```
   cd ~/linux-7.1.2
   make -j8 bindeb-pkg LOCALVERSION=-rasplit
   ```
   Produces `~/linux-image-7.1.2-rasplit_*.deb` and
   `~/linux-headers-7.1.2-rasplit_*.deb`.

4. Install into the guest and reboot:
   ```
   scp -P 2222 -i ~/.ssh/id_ed25519_qemu \
     ~/linux-image-7.1.2-rasplit_*.deb ~/linux-headers-7.1.2-rasplit_*.deb \
     lenoa@localhost:/tmp/
   ssh -p 2222 -i ~/.ssh/id_ed25519_qemu lenoa@localhost \
     "sudo dpkg -i /tmp/linux-image-7.1.2-rasplit_*.deb /tmp/linux-headers-7.1.2-rasplit_*.deb && sudo reboot"
   ```
   `dpkg` regenerates the initramfs and GRUB config automatically; the
   newest kernel becomes the default boot entry.

5. Wait for the guest to come back (`uname -r` should print
   `7.1.2-rasplit`). The first time, copy over and build the trigger
   program:
   ```
   scp -P 2222 -i ~/.ssh/id_ed25519_qemu tools/mmaptest.c lenoa@localhost:/tmp/
   ssh -p 2222 -i ~/.ssh/id_ed25519_qemu lenoa@localhost "gcc /tmp/mmaptest.c -o /tmp/mmaptest"
   ```
   Then trigger the read-around path with a cold cache:
   ```
   sudo dmesg -C
   sync && echo 3 | sudo tee /proc/sys/vm/drop_caches
   /tmp/mmaptest /path/to/some/file
   sudo dmesg | grep RAROUND
   ```

   `drop_caches` clears the page cache system-wide, so expect `RAROUND`
   lines from other processes' mmap faults too, not just the target file —
   filter by watching for the specific `pgoff` your test file should hit
   (e.g. `pgoff=0` for a first-page read), or use a dedicated/otherwise-idle
   guest for cleaner signal.

## Sensor

A single diagnostic line is currently in the "mmap read-around" branch of
`do_sync_mmap_readahead()` (`src/mm/filemap.c`), confirming the path fires
and showing the window it computes:

```c
printk(KERN_INFO "RAROUND: pgoff=%lu ra_pages=%u size=%u\n",
       vmf->pgoff, ra->ra_pages, ra->size);
```

This is instrumentation only — establishing that the loop above works
end-to-end — not the split implementation itself.

## Measurement hygiene (added 2026-09-16, after M1)

Three things silently corrupt latency measurements in this setup. All were
hit for real; see [`../results/phase1-m1-latency.md`](../results/phase1-m1-latency.md)
for the evidence.

1. **Launch QEMU with `cache=none` on the nvme drive.** Without it the host
   page cache serves guest reads out of host RAM, and the guest's
   `drop_caches` cannot clear it. With a fixed `--seed` every run reads the
   same pages, so each run warms the cache for the next and the
   last-measured kernel wins by ~3x. The launch line is:
   ```
   cd ~/qemu-lab && qemu-system-x86_64 -m 4G -smp 4 -enable-kvm \
     -drive file=ubuntu.qcow2,if=virtio,format=qcow2 \
     -drive file=nvme_disk.raw,id=nvm,if=none,format=raw,cache=none,aio=native \
     -device nvme,id=nvme0,serial=deadbeef \
     -device nvme-ns,drive=nvm,bus=nvme0,nsid=1,logical_block_size=512,physical_block_size=512 \
     -net nic -net user,hostfwd=tcp::2222-:22 -nographic
   ```
2. **`sudo dmesg -n 1` before every timed run.** The console is
   `ttyS0,115200n8` and console printk sits on the measured path. It is also
   *asymmetric*: the split kernel emits 3 more probe lines per fault than
   baseline, so leaving the console on handicaps the split (p99 −43.7% when
   silenced).
3. **Background apt is disabled in the guest** (`apt-daily.timer` and
   `apt-daily-upgrade.timer` disabled + masked, `APT::Periodic::*` set to 0
   in `/etc/apt/apt.conf.d/20auto-upgrades`). It otherwise fires on boot and
   installs packages underneath the measurement. Undo with `systemctl unmask
   --now` + `enable` and restoring that file to `1` if the guest ever needs
   real updates again.

Also note **`/tmp` is cleared on guest reboot** — `mmaptest` and any `.deb`
staged there must be re-copied after every reboot. Build it the same way
each time (`gcc -O2 -o /tmp/mmaptest /tmp/mmaptest.c`) so the binary is not
a variable between kernels.

## Gotchas hit while setting this up

- Host needs `flex bison libssl-dev libelf-dev bc dwarves zstd` to build,
  plus `debhelper libdw-dev` specifically for `bindeb-pkg`.
- Killing a stray duplicate `make` mid-build with `SIGKILL` corrupted the
  build tree (`ar: mm/util.o: No such file or directory`) — a clean rebuild
  fixed it. Don't run two `make` invocations against the same tree at once.
- The guest's `unattended-upgrade` can hold the dpkg lock; wait it out
  rather than removing the lock file. Match on it with `pgrep -f
  /usr/bin/unattended-upgrade`, not `pgrep -x unattended-upgrade` — the
  kernel truncates `comm` to 15 chars so `-x` never matches and a wait loop
  built on it returns immediately.
- Because root lives on an LVM logical volume, `-kernel bzImage` direct
  boot (bypassing GRUB) isn't viable without a custom initramfs with LVM
  activation baked in; installing via `.deb` + GRUB is simpler and already
  proven to work.

---

# 환경 (한국어)

## 구성

이 디렉토리(`readaround-split/`)에는 이 프로젝트가 건드리는 커널 소스
파일의 **버전 관리되는, 직접 수정하는 사본**이 들어 있다:

```
src/mm/filemap.c       do_sync_mmap_readahead() / do_async_mmap_readahead()
src/mm/readahead.c      page_cache_ra_order() 등
src/block/bio.c
src/block/blk-merge.c
src/block/blk-mq.c
```

이 파일들은 Linux v7.1.2
(`https://mirrors.edge.kernel.org/pub/linux/kernel/v7.x/linux-7.1.2.tar.xz`)
원본을 그대로 받아 시작했고, 수정을 가하기 전 원본 tarball과 바이트 단위로
동일함을 확인했다.

이 파일들만으로는 **빌드가 안 된다** — 전체 소스 트리가 아니라 발췌본이기
때문이다. 실제 빌드와 부팅은 별도의 전체 체크아웃인 `~/linux-7.1.2`
(용량이 크고, git 추적 안 됨, 호스트 전용)에서 이뤄진다. `sync.sh`가 이
둘을 이어준다.

## 셋업

| 구성 요소 | 위치 |
|---|---|
| 전체 커널 소스 (빌드 대상) | `~/linux-7.1.2` |
| QEMU 부팅 커맨드 | `~/qemu-lab` (`ubuntu.qcow2` 루트 디스크 + `nvme_disk.raw` 에뮬레이션 NVMe) |
| 게스트 루트 파일시스템 | LVM(`ubuntu-vg/ubuntu-lv`) 위 ext4, GRUB로 부팅 — LVM 때문에 `-kernel` 방식의 빠른 직접 부팅은 불가능, GRUB 엔트리 선택이 필요함 |
| 게스트 SSH | `ssh -p 2222 -i ~/.ssh/id_ed25519_qemu lenoa@localhost` (키 인증, 비밀번호 불필요) |
| 게스트 sudo | 비밀번호 없음 (`/etc/sudoers.d/90-lenoa-nopasswd`, 이 게스트 안에서만 적용) |
| mmap 트리거 프로그램 | `tools/mmaptest.c` (이 저장소 안), 게스트 안에서 컴파일 |

게스트에는 이전 NVMe 폴링 작업에서 쓰던 DPAS 패치 5.18 커널
(`5.18.0-dpas`)도 그대로 설치돼 있다. 이 프로젝트와는 무관하지만 필요하면
GRUB에서 여전히 선택 가능하다.

## 관측 루프 (observation loop)

모든 실험은 다음 순서를 따른다: **여기서 수정 -> 동기화 -> 빌드 -> 설치
-> 부팅 -> 트리거 -> dmesg 확인.**

1. `src/`(`src/mm/*.c`, `src/block/*.c`)에서 해당 파일을 수정한다.

2. 빌드 가능한 트리로 동기화한다:
   ```
   ./sync.sh                      # 기본값은 ~/linux-7.1.2
   ```

3. 호스트에서 `.deb` 커널 패키지를 빌드한다:
   ```
   cd ~/linux-7.1.2
   make -j8 bindeb-pkg LOCALVERSION=-rasplit
   ```
   `~/linux-image-7.1.2-rasplit_*.deb`와
   `~/linux-headers-7.1.2-rasplit_*.deb`가 생성된다.

4. 게스트에 설치하고 재부팅한다:
   ```
   scp -P 2222 -i ~/.ssh/id_ed25519_qemu \
     ~/linux-image-7.1.2-rasplit_*.deb ~/linux-headers-7.1.2-rasplit_*.deb \
     lenoa@localhost:/tmp/
   ssh -p 2222 -i ~/.ssh/id_ed25519_qemu lenoa@localhost \
     "sudo dpkg -i /tmp/linux-image-7.1.2-rasplit_*.deb /tmp/linux-headers-7.1.2-rasplit_*.deb && sudo reboot"
   ```
   `dpkg`가 initramfs와 GRUB 설정을 자동으로 갱신하며, 가장 최신 커널이
   기본 부팅 엔트리가 된다.

5. 게스트가 다시 올라오길 기다린다(`uname -r`이 `7.1.2-rasplit`을
   출력해야 함). 처음 한 번은 트리거 프로그램을 옮기고 빌드한다:
   ```
   scp -P 2222 -i ~/.ssh/id_ed25519_qemu tools/mmaptest.c lenoa@localhost:/tmp/
   ssh -p 2222 -i ~/.ssh/id_ed25519_qemu lenoa@localhost "gcc /tmp/mmaptest.c -o /tmp/mmaptest"
   ```
   그 다음 콜드 캐시 상태에서 read-around 경로를 트리거한다:
   ```
   sudo dmesg -C
   sync && echo 3 | sudo tee /proc/sys/vm/drop_caches
   /tmp/mmaptest /path/to/some/file
   sudo dmesg | grep RAROUND
   ```

   `drop_caches`는 시스템 전역의 페이지 캐시를 지우기 때문에, 대상 파일
   뿐만 아니라 다른 프로세스들의 mmap fault에서도 `RAROUND` 로그가 찍힐
   수 있다 — 테스트 파일이 찍을 것으로 예상되는 특정 `pgoff`(예: 첫
   페이지를 읽는다면 `pgoff=0`)로 필터링하거나, 다른 작업이 없는 전용
   게스트를 쓰면 신호가 더 깔끔하다.

## 센서

현재 `do_sync_mmap_readahead()`(`src/mm/filemap.c`)의 "mmap read-around"
분기에 진단용 printk 한 줄이 들어가 있다. 이 경로가 실제로 실행되는지,
그리고 계산되는 윈도우 값이 무엇인지 확인하기 위한 것이다:

```c
printk(KERN_INFO "RAROUND: pgoff=%lu ra_pages=%u size=%u\n",
       vmf->pgoff, ra->ra_pages, ra->size);
```

이건 순수 계측용이다 — 위 루프가 처음부터 끝까지 제대로 도는지 확인하기
위한 것이지, split 구현 자체가 아니다.

## 측정 위생 (2026-09-16 추가, M1 이후)

이 셋업에서 지연 측정을 조용히 망가뜨리는 것이 세 가지 있다. 전부 실제로
당했다. 근거는
[`../results/phase1-m1-latency.md`](../results/phase1-m1-latency.md) 참조.

1. **QEMU를 nvme 드라이브에 `cache=none`으로 띄울 것.** 안 그러면 호스트
   페이지 캐시가 게스트 읽기를 호스트 RAM에서 처리해버리고, 게스트의
   `drop_caches`로는 그걸 못 비운다. `--seed`를 고정하면 매 실행이 같은
   페이지를 읽으므로 앞 실행이 뒤 실행을 위해 캐시를 데워주고, 결국 마지막에
   측정한 커널이 3배쯤 이긴다. 기동 커맨드:
   ```
   cd ~/qemu-lab && qemu-system-x86_64 -m 4G -smp 4 -enable-kvm \
     -drive file=ubuntu.qcow2,if=virtio,format=qcow2 \
     -drive file=nvme_disk.raw,id=nvm,if=none,format=raw,cache=none,aio=native \
     -device nvme,id=nvme0,serial=deadbeef \
     -device nvme-ns,drive=nvm,bus=nvme0,nsid=1,logical_block_size=512,physical_block_size=512 \
     -net nic -net user,hostfwd=tcp::2222-:22 -nographic
   ```
2. **타이밍 측정 전 매번 `sudo dmesg -n 1`.** 콘솔이 `ttyS0,115200n8`이고
   콘솔 printk는 측정 경로 위에 있다. 게다가 *비대칭*이다 — split 커널이
   fault당 프로브 3줄을 더 뱉으므로, 콘솔을 켜두면 split에 핸디캡이 붙는다
   (끄면 p99 −43.7%).
3. **게스트의 백그라운드 apt는 꺼져 있다** (`apt-daily.timer`,
   `apt-daily-upgrade.timer` disable + mask, `/etc/apt/apt.conf.d/20auto-upgrades`의
   `APT::Periodic::*`를 0으로). 안 끄면 부팅 때 떠서 측정 밑에서 패키지를
   설치한다. 게스트에 실제 업데이트가 다시 필요해지면 `systemctl unmask
   --now` + `enable`과 그 파일을 `1`로 되돌리면 된다.

그리고 **게스트 재부팅 시 `/tmp`이 비워진다** — 거기 올려둔 `mmaptest`와
`.deb`는 재부팅마다 다시 복사해야 한다. 커널 간 비교에서 바이너리가 변수가
되지 않도록 매번 같은 방식으로 빌드할 것
(`gcc -O2 -o /tmp/mmaptest /tmp/mmaptest.c`).

## 환경 구축 중 겪은 문제들

- 호스트에는 빌드에 `flex bison libssl-dev libelf-dev bc dwarves zstd`가
  필요하고, `bindeb-pkg`에는 별도로 `debhelper libdw-dev`가 필요하다.
- 빌드 도중 중복 실행된 `make`를 `SIGKILL`로 강제 종료했더니 빌드 트리가
  깨졌다(`ar: mm/util.o: No such file or directory`) — 다시 빌드하니
  해결됐다. 같은 트리에 대해 `make`를 동시에 두 번 돌리지 말 것.
- 게스트의 `unattended-upgrade`가 dpkg 락을 잡고 있을 수 있다. 락 파일을
  지우지 말고 끝날 때까지 기다릴 것. `pgrep -x unattended-upgrade`가
  아니라 `pgrep -f /usr/bin/unattended-upgrade`로 매칭해야 한다 — 커널이
  `comm`을 15자로 자르기 때문에 `-x`는 절대 매칭되지 않고, 이걸 기반으로
  만든 대기 루프는 즉시 (거짓으로) 종료돼버린다.
- 루트가 LVM 논리 볼륨 위에 있기 때문에, LVM 활성화가 포함된 커스텀
  initramfs 없이는 GRUB를 건너뛰는 `-kernel bzImage` 직접 부팅이 불가능
  하다. `.deb` + GRUB 설치 방식이 더 간단하고 이미 검증됐다.
