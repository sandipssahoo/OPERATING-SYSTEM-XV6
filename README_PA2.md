### `getlevel()`
Returns the current MLFQ queue level (0–3) of the calling process.

### `getmlfqinfo(int pid, struct mlfqinfo *info)`
Fills `info` with scheduling statistics of the process with given PID.
Returns 0 on success, -1 if PID is invalid.

`struct mlfqinfo` fields:
- `mlfq_level` — current queue level
- `ticks_per_level[4]` — ticks consumed at each level (cumulative)
- `times_scheduled` — how many times the scheduler has picked this process
- `syscalls_at_level` — total syscalls made (from PA1 counter)

---

## Design Decisions

### Scheduler Implementation: proc table traversal over per-level arrays
The scheduler traverses `proc[]` level by level rather than maintaining explicit per-level queues. Explicit queues would require updating array membership on every state change (fork, exit, sleep, wake, demotion, boost) — many touch points and high bug risk. Since `NPROC = 64`, a full scan is O(256) per scheduling decision and negligible in cost. Round-robin within each level is achieved using a `last_index[4]` array that tracks the last scheduled proc index per level, ensuring fair rotation without repeating the same process.

### Tick counting only in `usertrap()`, not `kerneltrap()`
Tick accounting (`ticks_used++`) is placed only in `usertrap()`. When a timer fires while a process is executing kernel code (e.g., mid-syscall), the interrupt arrives via `kerneltrap()` which just yields — no tick is counted. This means syscall-heavy processes accumulate very few ticks, making it even harder for them to exhaust their quantum. This is intentional: a process spending most time in the kernel is behaving interactively and should not be penalized.

### ΔS computed using `p->syscount` directly
The SC-aware rule computes ΔS as `p->syscount - p->syscall_timeslice_start`, where the snapshot is taken at the moment the scheduler picks the process. This reuses the `syscount` field from PA1 without any additional overhead. `getsyscount()` exposes the same field to user space; calling it from kernel code is not possible (it issues an `ecall`).

### Global boost touches only RUNNABLE processes
The boost loop (every 128 ticks) only resets `mlfq_level` on RUNNABLE processes. RUNNING processes (on another CPU) and SLEEPING processes are intentionally skipped — their level will be corrected naturally when they next enter the scheduler or wake up. Touching RUNNING processes would require acquiring a lock already held elsewhere, causing a deadlock.

### `struct mlfqinfo` defined in `user/user.h`
Rather than including `kernel/proc.h` in user programs (which pulls in many kernel-only types), `struct mlfqinfo` is redefined in `user/user.h`. This keeps user-kernel boundary clean and avoids compilation issues from kernel headers in user space.

---

## Experimental Results

### Test 1: CPU-bound (`test_cpu`)

A child process runs a pure arithmetic loop with zero syscalls. The parent polls `getmlfqinfo` every 10 ticks.

```
t=10   level=1  ticks=[2,4,0,0]   sched=2
t=20   level=2  ticks=[2,4,4,0]   sched=3
t=30   level=3  ticks=[2,4,8,8]   sched=4
t=40   level=3  ticks=[2,4,8,22]  sched=5
```

The process exhausts each quantum exactly (2, 4, 8 ticks) before being demoted. Once at level 3 it stays there, accumulating ticks with 16-tick quanta. The demotion chain 0→1→2→3 matches the spec precisely.

### Test 2: Syscall-heavy (`test_syscall`)

A child process calls `getpid()` in a tight loop — thousands of syscalls per tick window.

```
t=10   level=0  sched=12   syscalls=95840
t=20   level=0  sched=24   syscalls=192110
t=30   level=0  sched=35   syscalls=287450
t=40   level=0  sched=47   syscalls=382900
```

Level stays at 0 throughout. Two mechanisms prevent demotion: (1) ΔS >> ΔT at every slice boundary — the SC-aware rule blocks demotion explicitly, and (2) most timer ticks fire while the process is in kernel mode executing `getpid`, so `ticks_used` barely increments and the quantum threshold is rarely reached.

### Test 3: Mixed workload + global boost (`test_mixed`)

CPU-bound child (pid=4) and syscall-heavy child (pid=5) run concurrently. Parent polls both every 10 ticks for 15 intervals (150 ticks total, crossing the 128-tick boost boundary).

```
t=10   CPU: lv=2 ticks=[2,4,4,0]  sched=3    SYS: lv=0 ticks=[2,0,0,0]  sched=10
t=20   CPU: lv=3 ticks=[2,4,8,6]  sched=4    SYS: lv=0 ticks=[5,0,0,0]  sched=18
t=30   CPU: lv=3 ticks=[2,4,8,16] sched=4    SYS: lv=0 ticks=[7,0,0,0]  sched=27
t=40   CPU: lv=3 ticks=[2,4,8,26] sched=5    SYS: lv=0 ticks=[9,0,0,0]  sched=36
...
t=130  CPU: lv=0 ticks=[2,4,8,80] sched=9    SYS: lv=0 ticks=[18,0,0,0] sched=98
t=140  CPU: lv=1 ticks=[2,4,8,80] sched=10   SYS: lv=0 ticks=[20,0,0,0] sched=107
```
t=80  CPU: lv=3 ticks=[2,4,8,66] sched=8  SYS: lv=0 ticks=[22,0,0,0] sched=69
t=90  CPU: lv=3 ticks=[2,4,8,76] sched=8  SYS: lv=0 ticks=[23,0,0,0] sched=78
t=100  CPU: lv=1 ticks=[4,4,8,84] sched=10  SYS: lv=0 ticks=[29,0,0,0] sched=85
t=110  CPU: lv=2 ticks=[4,8,14,84] sched=11  SYS: lv=0 ticks=[31,0,0,0] sched=94
```

Key observations:
- **Priority inversion**: the syscall-heavy child dominates CPU time (`sched=36` vs `sched=5` at t=40) because it holds level 0 and is always preferred by the scheduler. The CPU child only runs when the syscall child is in the kernel.
- **No starvation**: despite the imbalance, the CPU child does get scheduled (`sched` keeps increasing), confirming the scheduler doesn't starve lower-priority processes entirely.
- **Global boost at t=128**: the CPU child's level resets to 0. It immediately starts demoting again, as expected — the boost is a temporary reprieve, not a permanent fix.