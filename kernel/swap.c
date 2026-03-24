// swap.c  –  simple in-memory swap area for PA3
//
// swap_init()         : called once at boot from main.c
// swap_out(page,pid,va) : copy 4096 bytes from 'page' into a free slot;
//                         returns slot index on success, -1 if swap is full
// swap_in(slot, buf)  : copy slot contents into 'buf' (caller provides 4096-byte buffer)
// swap_free(slot)     : mark slot as free

#include "types.h"
#include "param.h"
#include "riscv.h"      // ← MUST come before defs.h
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "swap.h"

static struct swap_slot swap_area[MAX_SWAP_SLOTS];
static struct spinlock swap_lock;

void
swap_init(void)
{
  initlock(&swap_lock, "swap");
  for (int i = 0; i < MAX_SWAP_SLOTS; i++) {
    swap_area[i].used = 0;
    swap_area[i].pid  = -1;
    swap_area[i].va   = 0;
  }
}

// Copy page contents into a free swap slot.
// Returns the slot index on success, -1 if swap is full.
int
swap_out(char *page, int pid, uint64 va)
{
  acquire(&swap_lock);
  for (int i = 0; i < MAX_SWAP_SLOTS; i++) {
    if (!swap_area[i].used) {
      swap_area[i].used = 1;
      swap_area[i].pid  = pid;
      swap_area[i].va   = va;
      memmove(swap_area[i].data, page, PGSIZE);
      release(&swap_lock);
      return i;
    }
  }
  release(&swap_lock);
  return -1;  // swap full
}

// Copy slot data into caller-supplied buffer.
int
swap_in(int slot, char *buf)
{
  if (slot < 0 || slot >= MAX_SWAP_SLOTS)
    return -1;
  acquire(&swap_lock);
  if (!swap_area[slot].used) {
    release(&swap_lock);
    return -1;
  }
  memmove(buf, swap_area[slot].data, PGSIZE);
  release(&swap_lock);
  return 0;
}

// Mark a swap slot as free.
void
swap_free(int slot)
{
  if (slot < 0 || slot >= MAX_SWAP_SLOTS)
    return;
  acquire(&swap_lock);
  swap_area[slot].used = 0;
  swap_area[slot].pid  = -1;
  swap_area[slot].va   = 0;
  release(&swap_lock);
}