#ifndef SWAP_H
#define SWAP_H

#include "types.h"

// Maximum number of swap slots.
// Each slot holds one 4096-byte page.
#define MAX_SWAP_SLOTS 128

// A swap slot descriptor.
struct swap_slot {
  int   used;        // 1 if this slot is occupied
  int   pid;         // owning process pid (for debugging)
  uint64 va;         // virtual address that was swapped out
  char  data[4096];  // the actual page contents
};

void swap_init(void);
int  swap_out(char *page, int pid, uint64 va);  // returns slot index, -1 on fail
int  swap_in(int slot, char *buf);              // copies data into buf, returns 0 ok
void swap_free(int slot);

#endif // SWAP_H