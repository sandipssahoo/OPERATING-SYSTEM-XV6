# Programming Assignment 3


## Implemented Features
### 1. Frame Table (`kernel/frametable.c`, `kernel/frametable.h`)
A flat array of `MAXFRAMES` (256) entries.  Each entry stores:
`used`      – whether the frame is occupied
`pid`       – owning process
`va`        – virtual address mapped to this frame
`pa`        – physical address
`ref_bit`   – Clock algorithm reference bit (set to 1 on allocation/access)
`swap_slot` – swap slot index if page has been evicted (-1 otherwise)
API:
`frametable_init()` – called once at boot from `main.c`
`frametable_add(pa, pid, va)` – register a newly allocated user frame
`frametable_remove(pa)` – remove frame on kfree
`frametable_set_ref(pa)` – mark frame as recently accessed
`clock_evict()` – run Clock, evict victim, return freed PA


### 2. Clock Page Replacement (`clock_evict()` in `frametable.c`)
Standard two-pass Clock algorithm:
Scan the frame table circularly using `clock_hand`.
If `ref_bit == 1`: clear it, advance.
If `ref_bit == 0`: candidate for eviction.
Scheduler-aware tie-breaking:  
Among all frames with `ref_bit == 0`, the one belonging to the process with the highest `mlfq_level` (lowest priority, most CPU-bound) is chosen.  This preserves the working sets of interactive processes.

## Design Decisions
 Consistent with the PA2 scheduler design philosophy: a flat scan of 256 entries is O(256) per eviction decision, negligible in cost. It avoids complex cross-list bookkeeping when processes fork, exit, or sleep.
RISC-V hardware only interprets PTE bits 0–9 when `PTE_V == 1`. Since we set `PTE_V = 0` on eviction, the hardware never looks at the remaining bits. Bit 8 (software-reserved RSW field) is our "is swapped" marker; bits [63:10] carry the slot index. This avoids storing a separate swap-map and keeps the fault handler O(1).
Syscall-heavy processes naturally stay at MLFQ level 0 and thus get lower eviction priority — exactly the desired behavior for scheduler-aware replacement.
256 × 4 KB = 1 MB of user physical frames. This is small enough that a TEST_PAGES = 320 allocation triggers meaningful replacement. 128 swap slots × 4 KB = 512 KB swap area, enough for a representative workload.
Locking discipline
`ftlock` (spinlock) protects the frame table.
`swap_lock` (spinlock) protects the swap area.
`p->lock` is acquired only when updating per-process stats.
`clock_evict()` releases `ftlock` before calling `kfree()` to avoid lock ordering issues.

## Files Changed / Added
`kernel/proc.h`	Add `PTE_SWAPPED`, `PTE_SWAP_SLOT`, PA3 stats fields to `struct proc`
`kernel/proc.c`	Init PA3 fields to 0 in `allocproc()`
`kernel/vm.c`	Replace `vmfault()`, add `kalloc_or_evict()`, fix `uvmunmap()`
`kernel/frametable.h`	New: frame table types and API
`kernel/frametable.c`	New: frame table + Clock eviction
`kernel/swap.h`	New: swap slot types and API
`kernel/swap.c`	New: in-memory swap implementation
`kernel/vmstats.h`	New: `struct vmstats` definition
`kernel/sysproc.c`	Add `sys_getvmstats()`
`kernel/syscall.h`	Add `SYS_getvmstats = 30`
`kernel/syscall.c`	Register `sys_getvmstats` in dispatch table
`kernel/defs.h`	Declare frametable and swap functions
`kernel/main.c`	Call `frametable_init()` and `swap_init()` at boot
`user/user.h`	Add `struct vmstats`, `getvmstats()` prototype
`user/usys.pl`	Add `entry("getvmstats")`
`user/vmtest.c`	New: test program
`Makefile`	Add `frametable.o`, `swap.o` to OBJS; add `_vmtest` to UPROGS
---


