// frametable.c  –  Global frame table and Clock page replacement
//
// Design
// ------
// * A flat array of MAXFRAMES entries tracks every user physical frame.
// * frametable_add()    : called whenever kalloc() succeeds for a user page.
// * frametable_remove() : called whenever kfree() releases a user frame.
// * frametable_set_ref(): sets the Clock reference bit; called by the
//                         page-fault handler when a page is accessed.
// * clock_evict()       : runs the Clock algorithm, picks a victim, writes
//                         it to swap, clears the PTE, calls kfree(), and
//                         returns the now-free physical address.
//
// Scheduler-aware tie-breaking (PA2 integration)
// -----------------------------------------------
// When the Clock scan finds MULTIPLE frames with ref_bit == 0 it prefers
// the one belonging to the lowest-priority (highest mlfq_level) process,
// so that interactive processes retain their pages longer.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "frametable.h"
#include "swap.h"

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static struct frame_entry ftable[MAXFRAMES];
static struct spinlock     ftlock;
static int                 clock_hand = 0;   // current Clock position

// ---------------------------------------------------------------------------
// Helper: look up a process by pid (scan proc table)
// ---------------------------------------------------------------------------
extern struct proc proc[NPROC];

static struct proc *
find_proc(int pid)
{
  for (struct proc *p = proc; p < &proc[NPROC]; p++) {
    if (p->pid == pid)
      return p;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// frametable_init – call once from main()
// ---------------------------------------------------------------------------
void
frametable_init(void)
{
  initlock(&ftlock, "frametable");
  for (int i = 0; i < MAXFRAMES; i++) {
    ftable[i].used      = 0;
    ftable[i].pid       = -1;
    ftable[i].va        = 0;
    ftable[i].pa        = 0;
    ftable[i].ref_bit   = 0;
    ftable[i].swap_slot = -1;
  }
  clock_hand = 0;
}

// ---------------------------------------------------------------------------
// frametable_add – register a newly allocated physical frame
// ---------------------------------------------------------------------------
void
frametable_add(uint64 pa, int pid, uint64 va)
{
  acquire(&ftlock);
  for (int i = 0; i < MAXFRAMES; i++) {
    if (!ftable[i].used) {
      ftable[i].used      = 1;
      ftable[i].pid       = pid;
      ftable[i].va        = va;
      ftable[i].pa        = pa;
      ftable[i].ref_bit   = 1;   // freshly allocated → recently used
      ftable[i].swap_slot = -1;
      release(&ftlock);
      return;
    }
  }
  // Frame table overflow – should not happen if MAXFRAMES == physical limit
  release(&ftlock);
  panic("frametable_add: table full");
}

// ---------------------------------------------------------------------------
// frametable_remove – called on kfree() for a user frame
// ---------------------------------------------------------------------------
void
frametable_remove(uint64 pa)
{
  acquire(&ftlock);
  for (int i = 0; i < MAXFRAMES; i++) {
    if (ftable[i].used && ftable[i].pa == pa) {
      ftable[i].used    = 0;
      ftable[i].pid     = -1;
      ftable[i].va      = 0;
      ftable[i].pa      = 0;
      ftable[i].ref_bit = 0;
      // Note: swap_slot should already be freed by caller
      ftable[i].swap_slot = -1;
      release(&ftlock);
      return;
    }
  }
  release(&ftlock);
  // Not every kfree() is for a user frame (kernel pages not registered),
  // so a miss here is normal.
}

// ---------------------------------------------------------------------------
// frametable_set_ref – mark a physical frame as recently accessed
// ---------------------------------------------------------------------------
void
frametable_set_ref(uint64 pa)
{
  acquire(&ftlock);
  for (int i = 0; i < MAXFRAMES; i++) {
    if (ftable[i].used && ftable[i].pa == pa) {
      ftable[i].ref_bit = 1;
      break;
    }
  }
  release(&ftlock);
}

// ---------------------------------------------------------------------------
// frametable_used – count occupied entries
// ---------------------------------------------------------------------------
int
frametable_used(void)
{
  int count = 0;
  acquire(&ftlock);
  for (int i = 0; i < MAXFRAMES; i++)
    if (ftable[i].used) count++;
  release(&ftlock);
  return count;
}

// ---------------------------------------------------------------------------
// mlfq_level_of – return the MLFQ level of a process, 0 if not found.
// Higher level number = lower priority (more CPU-bound).
// ---------------------------------------------------------------------------
static int
mlfq_level_of(int pid)
{
  struct proc *p = find_proc(pid);
  if (p) return p->mlfq_level;
  return 0;
}

// ---------------------------------------------------------------------------
// clock_evict – select a victim using the Clock algorithm with
//               scheduler-aware tie-breaking, evict it, return its PA.
//
// Steps:
//   1. Scan the circular frame table.
//   2. If ref_bit == 1 → clear it and advance.
//   3. If ref_bit == 0 → candidate; pick worst MLFQ level among all
//      ref_bit==0 candidates found in one complete pass.
//   4. Write the victim page to swap.
//   5. Clear the PTE (V bit) and set a custom SW bit so the fault handler
//      knows to swap in on next access.
//   6. kfree() the frame and remove from frame table.
//   7. Return the freed PA.
//
// Caller must NOT hold ftlock.
// ---------------------------------------------------------------------------
uint64
clock_evict(void)
{
  acquire(&ftlock);

  // We may need up to 2 full passes: first pass clears ref bits,
  // second pass finds a victim.
  int victim_idx   = -1;
  int victim_level = -1;   // mlfq level of chosen victim (higher = better to evict)

  for (int pass = 0; pass < 2 * MAXFRAMES; pass++) {
    int i = clock_hand % MAXFRAMES;
    clock_hand = (clock_hand + 1) % MAXFRAMES;

    if (!ftable[i].used)
      continue;

    if (ftable[i].ref_bit == 1) {
      // Recently accessed: give it a second chance
      ftable[i].ref_bit = 0;
      continue;
    }

    // ref_bit == 0: candidate
    int lvl = mlfq_level_of(ftable[i].pid);
    if (lvl > victim_level) {
      victim_idx   = i;
      victim_level = lvl;
    }

    // After finding at least one candidate, continue one more full scan
    // to find a lower-priority one if any, then stop.
    if (victim_idx >= 0 && pass >= MAXFRAMES)
      break;
  }

  if (victim_idx < 0) {
    // All frames had ref_bit set – just evict the one at clock_hand
    for (int i = 0; i < MAXFRAMES; i++) {
      if (ftable[i].used) {
        victim_idx = i;
        break;
      }
    }
    if (victim_idx < 0) {
      release(&ftlock);
      return 0;  // no frames at all
    }
  }

  struct frame_entry victim = ftable[victim_idx];
  // Mark as free in table NOW (before releasing lock)
  ftable[victim_idx].used    = 0;
  ftable[victim_idx].pid     = -1;
  ftable[victim_idx].va      = 0;
  ftable[victim_idx].pa      = 0;
  ftable[victim_idx].ref_bit = 0;
  ftable[victim_idx].swap_slot = -1;

  release(&ftlock);

  // ------------------------------------------------------------------
  // Write victim page contents to swap
  // ------------------------------------------------------------------
  int slot = swap_out((char *)victim.pa, victim.pid, victim.va);
  if (slot < 0) {
    // swap full – critical failure
    panic("clock_evict: swap full");
  }

  // ------------------------------------------------------------------
  // Find the owner process and fix its page table
  // ------------------------------------------------------------------
  struct proc *owner = find_proc(victim.pid);
  if (owner) {
    pte_t *pte = walk(owner->pagetable, victim.va, 0);
    if (pte && (*pte & PTE_V)) {
      // Clear the valid bit so the next access causes a page fault
      // Encode swap slot in bits [63:10] (upper bits of PTE not used by hw).
      // We use a custom convention:
      //   bit 0  (PTE_V) = 0   → invalid → will trap
      //   bits [63:10]         → swap slot index (shifted)
      // This lets vmfault() detect it as "swapped out" vs "never mapped".
      *pte = (((uint64)slot) << 10) | PTE_SWAPPED;
    }
    // Update per-process statistics
    acquire(&owner->lock);
    owner->pages_evicted++;
    owner->pages_swapped_out++;
    owner->resident_pages--;
    release(&owner->lock);
  }

  // ------------------------------------------------------------------
  // Free the physical frame
  // ------------------------------------------------------------------
  kfree((void *)victim.pa);

  return victim.pa;
}