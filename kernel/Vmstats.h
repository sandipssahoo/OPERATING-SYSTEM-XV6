// vmstats.h – virtual memory statistics structure (PA3)
//
// Include in:
//   kernel/sysproc.c  (for sys_getvmstats)
//   user/user.h        (redeclared for user-space programs)

#ifndef VMSTATS_H
#define VMSTATS_H

struct vmstats {
  int page_faults;      // total page faults handled for this process
  int pages_evicted;    // pages evicted FROM this process by clock algorithm
  int pages_swapped_in; // pages restored FROM swap for this process
  int pages_swapped_out;// pages written TO swap for this process
  int resident_pages;   // current physical frames held by this process
};

#endif // VMSTATS_H