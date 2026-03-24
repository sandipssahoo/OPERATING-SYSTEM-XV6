#ifndef FRAMETABLE_H
#define FRAMETABLE_H

#include "types.h"

// -----------------------------------------------------------------------
// Physical frame limit
// Keep MAXFRAMES small enough that the system exhausts memory quickly
// and page replacement is exercised.  64 MB / 4KB = 16384 frames total;
// we cap user frames at 256 so tests trigger replacement easily.
// -----------------------------------------------------------------------
#define MAXFRAMES 256

// One entry in the global frame table.
struct frame_entry {
  int    used;        // 1 = frame is occupied by a user page
  int    pid;         // which process owns this frame
  uint64 va;          // virtual address mapped to this physical frame
  uint64 pa;          // physical address of the frame
  int    ref_bit;     // Clock algorithm reference bit (set to 1 on access)
  int    swap_slot;   // if page is swapped out: slot index; -1 otherwise
};

// -----------------------------------------------------------------------
// Public API (implemented in frametable.c / vm.c / kalloc.c)
// -----------------------------------------------------------------------
void frametable_init(void);

// Register a newly-allocated physical frame in the frame table.
void frametable_add(uint64 pa, int pid, uint64 va);

// Remove a frame entry (called on kfree / process exit).
void frametable_remove(uint64 pa);

// Update the reference bit to 1 (called from page-fault handler on access).
void frametable_set_ref(uint64 pa);

// Clock: find victim frame, evict it, return its physical address
// for reuse.  Returns 0 on failure.
uint64 clock_evict(void);

// How many user frames are currently occupied?
int frametable_used(void);

#endif // FRAMETABLE_H