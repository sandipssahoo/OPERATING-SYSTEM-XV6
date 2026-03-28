#ifndef FRAMETABLE_H
#define FRAMETABLE_H

#include "types.h"
#define MAXFRAMES 128

struct frame_entry {
  int    used;
  int    pid;
  struct proc *owner;
  uint64 va;
  uint64 pa;
  int    ref_bit;    
  int    swap_slot;   
};

void frametable_init(void);
void frametable_add(uint64 pa, int pid, uint64 va);
void frametable_remove(uint64 pa);
void frametable_set_ref(uint64 pa);
uint64 clock_evict(void);
int frametable_used(void);

#endif