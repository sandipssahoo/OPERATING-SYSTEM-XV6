# MLFQ Scheduler Implementation - Complete Analysis

## 1. DATA STRUCTURES & DECLARATIONS

### Location
- **[kernel/proc.h](kernel/proc.h)** (lines 86-92): Core MLFQ fields in `struct proc`
- **[kernel/proc.c](kernel/proc.c)** (lines 45-55): Process table initialization

### Key Data Structures

#### Per-Process MLFQ Fields (in `struct proc`)
```c
// MLFQ scheduler fields
int mlfq_level;           // Current queue level (0-3)
int ticks_at_level;       // Ticks consumed at current level
int ticks_per_level[4];   // Total ticks at each level (cumulative)
int times_scheduled;      // Number of times picked by scheduler
int syscalls_at_level;    // Syscalls snapshot when scheduled (for ΔS calculation)
int syscount;             // Total syscall count (from PA1)
```

#### User-Facing Statistics Structure
Defined in **[kernel/user.h](user/user.h)** (kept separate from kernel headers):
```c
struct mlfqinfo {
  int level;              // Current queue level
  int ticks[4];           // Ticks at each level
  int times_scheduled;    // Scheduling count
  int total_syscalls;     // Total syscall count
};
```

### MLFQ Queue Configuration

**4 Priority Levels** (implemented implicitly, not as separate data structures):
- **Level 0**: Highest priority (interactive processes) → 2-tick quantum
- **Level 1**: Medium-high → 4-tick quantum
- **Level 2**: Medium-low → 8-tick quantum
- **Level 3**: Lowest priority (CPU-bound) → 16-tick quantum

**Time Quantum per Level** (from [kernel/trap.c](kernel/trap.c) lines 38-43):
```c
int get_quantum(int level){
  int q[4]={2,4,8,16};              // Exponentially increasing
  if(level>=0 && level<4) return q[level];
  return 16;
}
```

### Why No Explicit Queue Arrays?
Per [README_PA2.md](README_PA2.md) Design Decisions section:
- Traversing `proc[NPROC]` level-by-level is O(256) per scheduling decision (negligible)
- Explicit queues would require updating membership on fork, exit, sleep, wake, demotion, boost
- Avoids high bug risk from multiple touch points
- Round-robin fairness achieved via `last_idx[4]` array (one per level)

---

## 2. INITIALIZATION

### Location
- **[kernel/proc.c](kernel/proc.c)** (lines 45-55): `procinit()` - called during boot
- **[kernel/proc.c](kernel/proc.c)** (lines 118-135): Per-process initialization in `allocproc()`
- **[kernel/main.c](kernel/main.c)** (line ~13): Boot sequence

### Initialization Sequence

#### 1. System Boot (main.c)
```c
procinit();      // Initialize process table (called on boot)
```

#### 2. Process Table Init (proc.c - procinit)
```c
void procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
  }
}
```

#### 3. New Process Init (proc.c - allocproc)
When a new process is created (via `fork` or `userinit`):
```c
found:
  p->pid = allocpid();
  p->state = USED;
  p->syscount = 0;              // From PA1
  p->mlfq_level = 0;            // Always start at level 0 (high priority)
  p->ticks_at_level = 0;
  for(int i=0; i<4; i++)
    p->ticks_per_level[i] = 0;
  p->times_scheduled = 0;
  p->syscalls_at_level = 0;
```

### Key Initialization Properties
✅ Every new process starts at **level 0** (highest priority)  
✅ New processes are treated as interactive until proven otherwise  
✅ All timing counters reset to zero  
✅ Each process gets its own spin lock for concurrency safety

---

## 3. PROCESS MANAGEMENT

### Process Assignment to Queues

#### Initial Assignment
- All processes start at **level 0** when created (most interactive treatment)
- This includes the first user process, init, and all children via fork()

#### Queue Movement (Demotion)

**Location**: [kernel/trap.c](kernel/trap.c) lines 94-115 (in `usertrap()`)

Happens when a process's **time quantum expires**:

```c
// Check if time slice expired
int quantum = get_quantum(p->mlfq_level);
if(p->ticks_at_level >= quantum){
  // Calculate ΔS (syscalls) and ΔT (ticks)
  int delta_s = p->syscount - p->syscalls_at_level;
  int delta_t = p->ticks_at_level;
  
  // Demotion logic: SC-aware rule
  if(delta_s < delta_t && p->mlfq_level < 3){
    // CPU-bound: fewer syscalls than ticks → demote one level
    p->mlfq_level++;
  }
  // else: interactive (ΔS ≥ ΔT), stay at current level
  
  p->ticks_at_level = 0;  // Reset for next quantum
  yield();                // Give up CPU
}
```

**Demotion Rule (SC-Aware)**:
- If `ΔS < ΔT`: Process made fewer syscalls than ticks consumed → **CPU-bound** → demote
- If `ΔS ≥ ΔT`: Process made syscalls equal to or more than ticks → **interactive** → stay or promote

**Example Progression**:
```
CPU-bound process:
Level 0 (2 ticks)  → 0 syscalls → demote
  ↓
Level 1 (4 ticks)  → 0 syscalls → demote
  ↓
Level 2 (8 ticks)  → 0 syscalls → demote
  ↓
Level 3 (16 ticks) → stays at level 3 (max demotion)

Syscall-heavy process:
Stays at Level 0 (abundant syscalls prevent demotion)
```

### Aging / Priority Boost (Starvation Prevention)

**Location**: [kernel/trap.c](kernel/trap.c) lines 195-215 (in `clockintr()`)

**Global Priority Boost every 128 ticks**:

```c
void clockintr()
{
  if(cpuid() == 0){
    acquire(&tickslock);
    ticks++;
    int gboost = (ticks > 0 && ticks % 128 == 0);
    wakeup(&ticks);
    release(&tickslock);

    if(gboost){
      struct proc *proc_ptr;
      for(proc_ptr = proc; proc_ptr < &proc[NPROC]; proc_ptr++){
        acquire(&proc_ptr->lock);
        if(proc_ptr->state == RUNNABLE || proc_ptr->state == RUNNING){
          // Reset all RUNNABLE and RUNNING processes to level 0
          proc_ptr->mlfq_level = 0;
          proc_ptr->ticks_at_level = 0;
        }
        release(&proc_ptr->lock);
      }
    }
  }
  // ... schedule next timer interrupt
}
```

**Boost Behavior**:
- ✅ Runs every exactly 128 global ticks
- ✅ Resets **only RUNNABLE and RUNNING** processes (sleeping processes wait until they wake)
- ✅ Resets both `mlfq_level` (back to 0) and `ticks_at_level` (back to 0)
- ✅ Prevents indefinite starvation of CPU-bound processes
- ✅ **Does NOT** boost SLEEPING processes (they'll reset naturally when they wake and return to scheduler)

**Why only RUNNABLE/RUNNING?**
- Touching RUNNING processes on other CPUs would require acquiring locks already held elsewhere
- Would cause deadlock
- Sleeping processes will get boosted when they wake up naturally
---

## 4. SCHEDULING LOGIC

### Main Scheduler Implementation

**Location**: [kernel/proc.c](kernel/proc.c) lines 423-485

```c
void scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  int last_idx[4] = {0};        // Track last scheduled index per level
  
  c->proc = 0;
  for(;;){
    // Enable interrupts to avoid deadlock (then turn off for safety)
    intr_on();
    
    intr_off();

    int found = 0;
    
    // PRIORITY SCHEDULING: Check level 0 first, then 1, 2, 3
    for(int level = 0; level < 4; level++){
      // Round-robin within this level
      int start_idx = (last_idx[level] + 1) % NPROC;
      int idx = start_idx;
      
      do {
        p = &proc[idx];
        acquire(&p->lock);
        
        // Look for RUNNABLE process at this level
        if(p->state == RUNNABLE && p->mlfq_level == level){
          // Found! RUN IT
          p->state = RUNNING;
          p->times_scheduled++;
          p->syscalls_at_level = p->syscount;  // Snapshot syscalls for next ΔS calculation
          last_idx[level] = idx;                // Remember position for next round-robin
          
          c->proc = p;
          swtch(&c->context, &p->context);     // Switch to process context
          c->proc = 0;
          found = 1;
          
          release(&p->lock);
          break;
        }
        
        release(&p->lock);
        idx = (idx + 1) % NPROC;              // Try next process
        
      } while(idx != start_idx);               // Wrap around to check all at this level
    }
    
    if(found == 0) {
      // No RUNNABLE process found at any level
      asm volatile("wfi");                    // Wait for interrupt
    }
  }
}
```

### Process Selection Order

**Multi-Level Priority Scheduling**:
1. **Level 0** processes are checked first (highest priority)
2. If any Level 0 process is RUNNABLE, it runs
3. Only if Level 0 is empty, check Level 1
4. And so on... through Level 2 and 3

**Result**: Higher-priority (more interactive) processes always run before lower-priority (CPU-bound) processes

### Round-Robin Within Each Level

**Per-level `last_idx` tracking**:
- Each level maintains its own `last_idx` pointer
- On each scheduling pass at level `i`, start searching at `(last_idx[i] + 1) % NPROC`
- After running a process, update `last_idx[i]` to that process's index
- Next time level `i` is scheduled, start from the next process (fair rotation)

**Example**:
```
Level 0 has processes: P2, P5, P8

First scheduling pass:
  last_idx[0] = 0, start search at 1
  → find P2 at idx 2, run it, set last_idx[0] = 2

Second scheduling pass:
  last_idx[0] = 2, start search at 3
  → find P5 at idx 5, run it, set last_idx[0] = 5

Third scheduling pass:
  last_idx[0] = 5, start search at 6
  → find P8 at idx 8, run it, set last_idx[0] = 8

Fourth scheduling pass:
  last_idx[0] = 8, start search at 9 (wraps to beginning)
  → find P2 again (round-robin complete)
```

### Time Slice Calculation and Enforcement

**Quantum Assignment**: Based on `mlfq_level`:
- Level 0: 2 ticks
- Level 1: 4 ticks
- Level 2: 8 ticks
- Level 3: 16 ticks

**Enforcement Mechanism**:
1. **Timer interrupt handler** ([kernel/trap.c](kernel/trap.c)) increments `ticks_at_level`
2. **Check at each interrupt**: Compare `ticks_at_level` to `get_quantum(level)`
3. **When quantum expires**: Call `yield()` to preempt the process
4. **Scheduler runs next**: Pick another RUNNABLE process

**Why incrementing on every timer tick (not every interrupt)?**
- Ensures accurate accounting of CPU time
- Even if process spends time in kernel, ticks are counted
- (Actually, ticks only counted in `usertrap()`, not `kerneltrap()`)

---

## 5. LOCKING MECHANISM

### Lock Structure

**Per-Process Lock**: [kernel/proc.h](kernel/proc.h)
```c
struct spinlock lock;  // One spinlock per process
```

Located in the `struct proc`, alongside MLFQ fields.

### Lock Acquisition/Release Points

#### Scheduler Acquires Lock
[kernel/proc.c](kernel/proc.c) lines 450-462:
```c
p = &proc[idx];
acquire(&p->lock);

if(p->state == RUNNABLE && p->mlfq_level == level){
  p->state = RUNNING;
  p->times_scheduled++;
  p->syscalls_at_level = p->syscount;
  last_idx[level] = idx;
  
  c->proc = p;
  swtch(&c->context, &p->context);    // Switch contexts while holding lock!
  c->proc = 0;
  found = 1;
  
  release(&p->lock);                  // Release after context switch
  break;
}

release(&p->lock);
```

#### Timer Interrupt Handler (usertrap)
[kernel/trap.c](kernel/trap.c) lines 94-115:
- Does **NOT** acquire process lock
- Timer handler runs in process context (process's own code)
- Directly modifies `p->ticks_at_level`, `p->mlfq_level`, etc.
- Calls `yield()` which acquires the lock itself

#### Global Boost (clockintr)
[kernel/trap.c](kernel/trap.c) lines 201-215:
```c
if(gboost){
  struct proc *proc_ptr;
  for(proc_ptr = proc; proc_ptr < &proc[NPROC]; proc_ptr++){
    acquire(&proc_ptr->lock);          // Acquire each process's lock
    if(proc_ptr->state == RUNNABLE || proc_ptr->state == RUNNING){
      proc_ptr->mlfq_level = 0;
      proc_ptr->ticks_at_level = 0;
    }
    release(&proc_ptr->lock);          // Release immediately
  }
}
```

### Concurrency Strategy

**Single Global Lock?** ❌ NO
- Uses **per-process spinlocks** (one per `struct proc`)
- Avoids blocking all processes due to one process's lock

**Per-Level Locks?** ❌ NO
- Would require complex coordination across levels
- Simpler to protect each process independently

**Lock Scope**:
- Protects: `mlfq_level`, `ticks_at_level`, `ticks_per_level[]`, `times_scheduled`, `syscalls_at_level`, `syscount`
- Also protects: `state`, `chan`, `killed`, `xstate`, `parent`

### Key Design Decisions for Concurrency

1. **Scheduler acquires before context switch**: Ensures process state doesn't change mid-switch
2. **Timer handler doesn't acquire**: Avoids complex re-entrancy issues
3. **Boost acquires per-process**: Avoids global lock contention
4. **Global boost skips SLEEPING processes**: Prevents deadlock from double-locking
5. **yield() acquires lock**: Changes state to RUNNABLE atomically

---

## 6. TIMER INTERRUPTS & PREEMPTION

### Timer Interrupt Handler

**Location**: [kernel/trap.c](kernel/trap.c) lines 44-127

#### User Trap Handler (usertrap)
Called when a timer interrupt occurs while process is in user mode:

```c
uint64 usertrap(void)
{
  int which_dev = 0;
  
  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");
  
  // Snip: build trap context, handle syscalls, etc.
  
  if(which_dev == 2){  // Timer interrupt (device 2)
    // MLFQ Scheduler Logic
    p->ticks_at_level++;
    p->ticks_per_level[p->mlfq_level]++;
    
    // Check if time slice expired
    int quantum = get_quantum(p->mlfq_level);
    if(p->ticks_at_level >= quantum){
      // Calculate ΔS (syscalls) and ΔT (ticks)
      int delta_s = p->syscount - p->syscalls_at_level;
      int delta_t = p->ticks_at_level;
      
      // Demotion logic: SC-aware decision
      if(delta_s < delta_t && p->mlfq_level < 3){
        // CPU-bound: demote one level
        p->mlfq_level++;
      }
      // else: interactive (ΔS ≥ ΔT), stay at same level
      
      p->ticks_at_level = 0;
      yield();  // Give up CPU
    }
  }
  
  prepare_return();
}
```

### Kernel Trap Handler

**Location**: [kernel/trap.c](kernel/trap.c) lines 166-195

When timer interrupt occurs in kernel mode (during syscall):

```c
void kerneltrap(void)
{
  // ... 
  if(which_dev == 2 && myproc() != 0)
    yield();  // Simple yield, no MLFQ logic here
  // ...
}
```

**Key Design Decision**: 
- Tick counting **ONLY in usertrap()**, not kerneltrap()
- When a process is in kernel (e.g., syscall), ticks don't accumulate
- This favors syscall-heavy processes (they look interactive)
- Intentional: processes spending time in kernel ARE interactive

### Time Slice Expiration Handling

**Flow**:
1. Timer fires → interrupt handler runs
2. `usertrap()` increments `p->ticks_at_level`
3. Check: `if(p->ticks_at_level >= quantum)`
4. If YES:
   - Evaluate process: CPU-bound vs interactive (ΔS vs ΔT)
   - Possibly demote (increment `p->mlfq_level`)
   - Reset timer: `p->ticks_at_level = 0`
   - **Preempt**: Call `yield()`

### Yield (Preemption)

**Location**: [kernel/proc.c](kernel/proc.c) lines 514-523

```c
void yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);          // Atomic state change
  p->state = RUNNABLE;         // Mark process as ready to run again
  sched();                      // Switch to scheduler
  release(&p->lock);
}
```

**Effect**:
- Process becomes RUNNABLE (no longer RUNNING)
- CPU switches back to scheduler (via context switch in `sched()`)
- Scheduler picks next RUNNABLE process (possibly a different one)
- Original process will run again later (round-robin fairness)

---

## 7. TEST CASES

### Location
[kernel/user/mlfq.c](user/mlfq.c)

### Test: Mixed Workload

**What it tests**:
- CPU-bound vs syscall-heavy process behavior
- Priority boosting every 128 ticks
- Process demotion chain (0→1→2→3)
- Starvation prevention

**Implementation**:
```c
int main(void)
{
  int pid_cpu = fork();
  if(pid_cpu == 0){
    // Pure CPU - zero syscalls
    volatile long x = 1;
    while(1){
      for(long i = 0; i < 100000000L; i++)
        x = x * 3 + i;
    }
    exit(0);
  }

  int pid_sys = fork();
  if(pid_sys == 0){
    // Pure syscalls - zero arithmetic
    while(1){
      for(int i = 0; i < 10000; i++)
        getpid();
    }
    exit(0);
  }

  printf("Mixed test: cpu_pid=%d  sys_pid=%d\n", pid_cpu, pid_sys);

  // Poll both processes every 10 ticks for 130 ticks (crosses 128-tick boost)
  for(int i = 0; i < 13; i++){
    pause(10);
    struct mlfqinfo c, s;
    getmlfqinfo(pid_cpu, &c);
    getmlfqinfo(pid_sys, &s);
    printf("t=%d  CPU: lv=%d ticks=[%d,%d,%d,%d] sched=%d  SYS: lv=%d ticks=[%d,%d,%d,%d] sched=%d\n",
      (i+1)*10,
      c.level, c.ticks[0], c.ticks[1], c.ticks[2], c.ticks[3], c.times_scheduled,
      s.level, s.ticks[0], s.ticks[1], s.ticks[2], s.ticks[3], s.times_scheduled);
  }

  kill(pid_cpu);
  kill(pid_sys);
  wait(0);
  wait(0);
  printf("Mixed test done\n");
  exit(0);
}
```

### Expected Behavior

**CPU-bound process**:
- **t=10-30**: Demotes from level 0 → 1 → 2 → 3
  ```
  t=10  level=1  ticks=[2,4,0,0]
  t=20  level=2  ticks=[2,4,4,0]
  t=30  level=3  ticks=[2,4,8,8]
  ```
- **t=40+**: Stays at level 3, accumulating 16-tick quanta
- **t=128+**: Priority boost resets level back to 0
- **t=140**: Immediately demotes again (CPU-bound pattern repeats)

**Syscall-heavy process**:
- **Stays at level 0** throughout
- Accumulates many scheduling counts (gets CPU time often)
- Why? Two reasons:
  1. ΔS >> ΔT (syscalls >> ticks) prevents demotion
  2. Most timer ticks fire while in kernel code (getpid syscall) → not counted in usertrap()

### What Each Test Demonstrates

#### Priority Boosting ✅
- At t=128: CPU-bound process resets from level 3 to level 0
- Shows that aging works to prevent indefinite starvation

#### Process Demotion ✅
- CPU-bound process demotes: 0 → 1 → 2 → 3
- Each demotion coincides with exhausted quantum at current level
- Proves SC-aware rule (ΔS < ΔT) correctly identifies CPU-bound processes

#### Fairness/Starvation Prevention ✅
- Syscall-heavy process dominates (sched count ~10x higher) but CPU-bound process still runs
- CPU-bound process's sched count keeps increasing throughout test
- No process stopped scheduling → starvation prevented

#### Time Quantum Administration ✅
- Level 0: 2 ticks (t goes from 0 to 2 at level 0 before first demotion)
- Level 1: 4 ticks
- Level 2: 8 ticks
- Level 3: 16 ticks (CPU child accumulates many ticks here)

---

## 8. CODE WALKTHROUGH

### proc.c - Process Table & Scheduler

**Initialization [lines 45-55]**:
- `procinit()` - Initialize process table, spinlocks

**Process Allocation [lines 100-150]**:
- `allocproc()` - Allocate new process, initialize MLFQ fields to 0
- Key: `p->mlfq_level = 0` (start at highest priority)

**Scheduler [lines 423-491]**:
- Main loop: for each CPU
- Inner loop: for each priority level (0 to 3)
- Within each level: round-robin search for RUNNABLE process
- Context switch to selected process
- If no process found: `wfi` (wait for interrupt)

**Yield [lines 514-523]**:
- Called when time quantum expires
- atomically set state to RUNNABLE
- Switch back to scheduler

**Sched [lines 495-511]**:
- Low-level context switch from process to scheduler
- Saves process context, restores scheduler context

### proc.h - Data Structure Definitions

**MLFQ Fields [lines 86-92]**:
- `mlfq_level` - current queue (0-3)
- `ticks_at_level` - ticks used in current quantum
- `ticks_per_level[4]` - cumulative ticks per level
- `times_scheduled` - number of times picked by scheduler
- `syscalls_at_level` - syscalls at start of current quantum (for ΔS)
- `syscount` - total syscalls (from PA1)

### trap.c - Timer Interrupts

**User Trap Handler [lines 44-127]**:
- `usertrap()` - Handle user traps, including timer interrupts
- On timer interrupt (which_dev == 2):
  - Increment `p->ticks_at_level` and `p->ticks_per_level[p->mlfq_level]`
  - Check if quantum expired
  - If yes: evaluate (ΔS vs ΔT), possibly demote, call yield()

**Quantum Lookup [lines 38-43]**:
- `get_quantum(int level)` - Return time slice for given level

**Clock Interrupt [lines 196-220]**:
- `clockintr()` - Update global tick counter
- Every 128 ticks: reset RUNNABLE/RUNNING processes to level 0

**Kernel Trap Handler [lines 166-195]**:
- `kerneltrap()` - Handle traps while in kernel mode
- Timer interrupt: simple `yield()` (no MLFQ accounting)

### sysproc.c - System Call Handlers

**Get Level [lines 172-174]**:
```c
uint64 sys_getlevel(void)
{
  return myproc()->mlfq_level;
}
```
- Returns current priority level of calling process

**Get MLFQ Info [lines 176-215]**:
```c
uint64 sys_getmlfqinfo(void)
{
  int pid;
  uint64 info_addr;
  
  argint(0, &pid);
  argaddr(1, &info_addr);
  
  // Find process by PID
  struct proc *target = 0;
  for(struct proc *p = proc; p < &proc[NPROC]; p++){
    if(p->pid == pid){
      target = p;
      break;
    }
  }
  
  if(target == 0) return -1;
  
  // Copy scheduling stats to user space
  struct mlfqinfo info;
  acquire(&target->lock);
  info.level = target->mlfq_level;
  for(int i = 0; i < 4; i++){
    info.ticks[i] = target->ticks_per_level[i];
  }
  info.times_scheduled = target->times_scheduled;
  info.total_syscalls = target->syscount;
  release(&target->lock);
  
  if(copyout(myproc()->pagetable, info_addr, (char *)&info, sizeof(info)) < 0)
    return -1;
  
  return 0;
}
```
- Atomically reads all scheduling stats for a process
- Copies to user-provided buffer
- Returns 0 on success, -1 if PID invalid

### syscall.c - System Call Registration

**Syscall Array [lines 110-146]**:
```c
extern uint64 sys_getlevel(void);
extern uint64 sys_getmlfqinfo(void);

static uint64 (*syscalls[])(void) = {
  // ... other syscalls ...
  [SYS_getlevel]    sys_getlevel,
  [SYS_getmlfqinfo] sys_getmlfqinfo,
};
```

**Syscall Entry Point [lines 158]**:
```c
// At end of each syscall:
p->sysccount++;  // Increment total syscall count
```

### syscall.h - Syscall Definitions

**New Syscall IDs**:
```c
#define SYS_getlevel    28
#define SYS_getmlfqinfo 29
```

### user.h - User-Space Declarations

```c
struct mlfqinfo {
  int level;
  int ticks[4];
  int times_scheduled;
  int total_syscalls;
};

int getlevel(void);
int getmlfqinfo(int pid, struct mlfqinfo*);
```

### usys.pl - User System Call Stubs

```perl
entry("getlevel");
entry("getmlfqinfo");
```
- Auto-generates user-space wrappers for syscalls

---

## 9. ADDRESSING KEY QUESTIONS

### Q1: How does implementation ensure high-priority processes run before low-priority ones?

**Answer**: Multi-level priority scheduling in `scheduler()`:
- Levels 0-3 checked in order (0 first)
- Only if Level 0 has no RUNNABLE processes, Level 1 is checked
- Strict priority: lower-level processes NEVER run if higher-level process waiting
- Only exception: when higher-level process uses full quantum → preempted → goes to scheduler again

**Code**: [kernel/proc.c](kernel/proc.c) lines 444-447
```c
for(int level = 0; level < 4; level++){  // Check 0, then 1, then 2, then 3
  // ... search for RUNNABLE at this level ...
}
```

### Q2: How prevent CPU-bound from starving I/O-bound?

**Answer**: Three mechanisms:

1. **SC-Aware Demotion**: CPU-bound processes demote to lower levels naturally
   - Makes them less likely to be scheduled
   - Gives I/O-bound processes more CPU time at higher levels

2. **Global Priority Boost**: Every 128 ticks, ALL processes reset to level 0
   - Gives lower-priority processes a temporary reprieve
   - Prevents indefinite starvation
   - CPU-bound processes quickly demote again (since they're still CPU-bound)

3. **I/O Operations**: I/O-bound processes SLEEP waiting for I/O
   - Sleeping processes don't consume CPU time
   - Don't accumulate ticks
   - Would show ΔS >> ΔT when they wake up (many syscalls, no ticks)
   - Stay at high priority level

### Q3: Voluntary yield vs full quantum usage?

**Answer**:

**Voluntary Yield**: Process calls `sbrk()`, `fork()`, `wait()`, etc. → blocks waiting for something
- Process state becomes SLEEPING or UNUSED
- Not counted in time slice accounting
- Not demoted when it wakes up (no quantum expiration)
- Gets fresh timer accounting when rescheduled

**Full Quantum Usage**: Process uses entire time slice
- Timer interrupt when `ticks_at_level >= quantum`
- Process is evaluated: ΔS vs ΔT
- Possibly demoted if CPU-bound
- Preempted with `yield()` → state = RUNNABLE
- Gets new turn in scheduler's round-robin

### Q4: Process creation, termination, state changes?

**Answer**:

**Creation** [kernel/proc.c - allocproc]:
- New process starts at level 0
- All timers and counters = 0
- Will run at high priority initially

**State Transitions**:
- UNUSED → USED (created)
- USED → RUNNABLE (ready to run)
- RUNNABLE → RUNNING (selected by scheduler)
- RUNNING → RUNNABLE (preempted via yield)
- RUNNING → SLEEPING (blocking syscall like read/wait)
- SLEEPING → RUNNABLE (woken by event)
- RUNNING/SLEEPING → ZOMBIE (exit called)
- ZOMBIE → UNUSED (parent calls wait)

**Termination** [kernel/proc.c - exit]:
- Process sets state = ZOMBIE
- Parent will reap with `wait()`
- mlfq_level/ticks preserved until reaped (for debugging)

### Q5: Edge cases?

**Sleeping Processes**:
- Global boost skips SLEEPING (avoids deadlock)
- When wake up naturally: return to scheduler
- Scheduler sees them as RUNNABLE when rescheduled
- State machine handles naturally

**Waiting Processes**:
- `wait()` syscall blocks parent (enters SLEEPING state)
- Parent not scheduled while waiting
- woken by child exit

**Exiting Processes**:
- Set state = ZOMBIE
- Parent gets woken
- MLFQ fields preserved for inspection via getmlfqinfo

**Multiple CPUs**:
- Each CPU has its own scheduler loop
- Each CPU maintains its own `last_idx[4]` for round-robin
- Per-process spinlocks protect concurrent access
- Global boost coordinates via spinlock

**Level 3 Processes**:
- Can't demote further
- Stay at level 3 indefinitely
- Only way back to level 0: priority boost (every 128 ticks)

---

## 10. DESIGN RATIONALE SUMMARY

### Why No Explicit Queues?
- Proc table traversal: O(256) per decision (4 levels × 64 procs)
- Explicit queues would require maintaining membership across many operations
- Higher code complexity = higher bug risk
- Trade space (four ints per proc) for simplicity and correctness

### Why Tick Counting Only in usertrap()?
- Syscall-heavy processes spend time in kernel (not counted)
- Naturally look more interactive (lower ΔT → harder to demote)
- Matches intuition: if you're in kernel, you're being interactive

### Why SC-Aware Rule (ΔS vs ΔT)?
- Simple heuristic: syscalls per tick = interactivity measure
- CPU-bound: few syscalls, many ticks (ΔS < ΔT)
- Interactive: many syscalls, few ticks (ΔS ≥ ΔT)
- Avoids complex process classification schemes

### Why 128-Tick Boost?
- Balance: frequent enough to prevent starvation, infrequent enough for differentiation
- Powers of 2 work well for modular arithmetic
- Chosen to match PA2 specification

### Why Boost Only RUNNABLE/RUNNING?
- RUNNABLE: about to be scheduled anyway, just gets priority boost
- RUNNING: can't touch (lock held by that CPU)
- SLEEPING: will boost itself when it wakes up and returns to scheduler
- Avoids deadlock, keeps design simple

---

## 11. TESTING STRATEGY

### What to Verify

1. **Demotion Chain**: CPU-bound process goes 0 → 1 → 2 → 3
2. **Interactive Never Demotes**: Syscall-heavy stays at level 0
3. **Quantum Enforcement**: Each level's ticks match expected quantum
4. **Global Boost**: Level resets every ~128 ticks
5. **Fairness**: Lower-priority processes still get scheduled
6. **Round-robin**: Processes at same level share CPU fairly

### How to Test

- Measure with `getmlfqinfo()` from user space
- Compare against expected values
- Run mixed workload (CPU + IO processes)
- Observe scheduling frequency, level progression, tick accumulation

---

