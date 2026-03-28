#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "frametable.h"
#include "swap.h"

static struct frame_entry ftable[MAXFRAMES];
static struct spinlock     ftlock;
static int                 clock_hand = 0;


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

// frametable_init – call once from main() while booting
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

// frametable_add – register a newly allocated physical frame
void
frametable_add(uint64 pa, int pid, uint64 va)
{
  acquire(&ftlock);
  for(int i = 0; i < MAXFRAMES; i++){
    if(!ftable[i].used){
      ftable[i].used      = 1;
      ftable[i].pid       = pid;
      // ftable[i].owner     = p; 
      ftable[i].va        = va;
      ftable[i].pa        = pa;
      ftable[i].ref_bit   = 1;
      ftable[i].swap_slot = -1;
      release(&ftlock);
      return;
    }
  }
  // table full — silently return eviction should have freed a slot
  release(&ftlock);
}

// frametable_remove – called on kfree() for a user frame
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
}

// frametable_set_ref – mark a physical frame as recently accessed

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
// frametable_used – count occupied entries

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
// mlfq_level_of – return the MLFQ level of a process, 0 if not found.
// Higher level number = lower priority (more CPU-bound).
static int
mlfq_level_of(int pid)
{
  struct proc *p = find_proc(pid);
  if (p) return p->mlfq_level;
  return 0;
}
uint64
clock_evict(void)
{
  acquire(&ftlock);
  int victim_idx   = -1;
  int victim_level = -1; 

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

    int lvl = mlfq_level_of(ftable[i].pid);
    if (lvl > victim_level) {
      victim_idx   = i;
      victim_level = lvl;
    }

    if (victim_idx >= 0 && pass >= MAXFRAMES)
      break;
  }

  if (victim_idx < 0) {
    for (int i = 0; i < MAXFRAMES; i++) {
      if (ftable[i].used) {
        victim_idx = i;
        break;
      }
    }
    if (victim_idx < 0) {
      release(&ftlock);
      return 0;
    }
  }

  int saved_pid = ftable[victim_idx].pid;
  uint64 saved_va = ftable[victim_idx].va;
  uint64 saved_pa = ftable[victim_idx].pa;
  ftable[victim_idx].used    = 0;
  ftable[victim_idx].pid     = -1;
  ftable[victim_idx].va      = 0;
  ftable[victim_idx].pa      = 0;
  ftable[victim_idx].ref_bit = 0;
  ftable[victim_idx].swap_slot = -1;

  release(&ftlock);

  int slot = swap_out((char *)saved_pa, saved_pid, saved_va);
  if (slot < 0) {
    // swap full – critical failure
    panic("clock_evict: swap full");
  }
  struct proc *owner = find_proc(saved_pid);
  if (owner) {
    pte_t *pte = walk(owner->pagetable, saved_va, 0);
    // printf("evict: pid=%d va=%lx pte=%p valid=%d\n",
    //      owner->pid, saved_va, pte,
    //      pte ? (*pte & PTE_V) : -1);  // TEMP
    if (pte && (*pte & PTE_V)) {
      *pte = (((uint64)slot) << 10) | PTE_SWAPPED;
    }
    // Update per-process statistics
    owner->pages_evicted++;
    owner->pages_swapped_out++;
    owner->resident_pages--;
  }

  kfree((void *)saved_pa);

  return saved_pa;
}