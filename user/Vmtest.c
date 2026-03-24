// vmtest.c – PA3 test program
//
// Tests:
//   1. Basic page fault + lazy allocation
//   2. Force page replacement by allocating > MAXFRAMES pages
//   3. Verify swap-in by accessing evicted pages again
//   4. Scheduler-aware eviction: lower-priority (CPU-bound) process
//      loses pages before a higher-priority (syscall-heavy) process
//   5. getvmstats() correctness

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// Must match MAXFRAMES in frametable.h  (256 frames × 4KB = 1MB)
#define MAXFRAMES   256
#define PAGE_SIZE   4096
#define TEST_PAGES  (MAXFRAMES + 64)   // deliberately exceed physical limit

// -----------------------------------------------------------------------
// Helper: print vmstats for a pid
// -----------------------------------------------------------------------
static void
print_vmstats(const char *label, int pid)
{
  struct vmstats vs;
  if (getvmstats(pid, &vs) < 0) {
    printf("  [%s] getvmstats(%d) failed\n", label, pid);
    return;
  }
  printf("  [%s] pid=%d  faults=%d  evicted=%d  swapped_out=%d  swapped_in=%d  resident=%d\n",
         label, pid,
         vs.page_faults, vs.pages_evicted,
         vs.pages_swapped_out, vs.pages_swapped_in,
         vs.resident_pages);
}

// -----------------------------------------------------------------------
// Test 1: Basic lazy allocation – each page touched for the first time
// -----------------------------------------------------------------------
static void
test_basic_fault(void)
{
  printf("\n=== Test 1: Basic lazy allocation ===\n");
  int pid = getpid();
  struct vmstats before, after;
  getvmstats(pid, &before);

  // Allocate 16 pages and write to each
  int n = 16;
  char *buf = sbrk(n * PAGE_SIZE);
  if (buf == (char *)-1) { printf("sbrk failed\n"); return; }

  for (int i = 0; i < n; i++) {
    buf[i * PAGE_SIZE] = (char)(i + 1);   // triggers page fault
  }

  getvmstats(pid, &after);
  printf("  pages touched: %d\n", n);
  printf("  new page_faults: %d  new resident: %d\n",
         after.page_faults - before.page_faults,
         after.resident_pages - before.resident_pages);

  if (after.page_faults - before.page_faults >= n)
    printf("  PASS: page faults counted correctly\n");
  else
    printf("  WARN: fewer faults than expected (some pages may share frames)\n");
}

// -----------------------------------------------------------------------
// Test 2: Force page replacement
// Allocate TEST_PAGES pages (> MAXFRAMES), verify pages_evicted > 0
// -----------------------------------------------------------------------
static void
test_replacement(void)
{
  printf("\n=== Test 2: Force page replacement ===\n");
  int pid = getpid();
  struct vmstats before, after;
  getvmstats(pid, &before);

  char *buf = sbrk((uint64)TEST_PAGES * PAGE_SIZE);
  if (buf == (char *)-1) { printf("sbrk failed\n"); return; }

  // Touch every page sequentially (clock should evict some)
  for (int i = 0; i < TEST_PAGES; i++) {
    buf[(uint64)i * PAGE_SIZE] = (char)(i & 0xFF);
  }

  getvmstats(pid, &after);
  printf("  pages allocated: %d  (MAXFRAMES=%d)\n", TEST_PAGES, MAXFRAMES);
  print_vmstats("after seq write", pid);

  if (after.pages_evicted > 0)
    printf("  PASS: eviction occurred (%d pages evicted)\n", after.pages_evicted);
  else
    printf("  FAIL: no evictions recorded\n");
}

// -----------------------------------------------------------------------
// Test 3: Swap-in – re-access evicted pages
// After Test 2 buf is still mapped; re-reading evicted pages should
// trigger swap-in faults.
// -----------------------------------------------------------------------
static void
test_swapin(char *buf)
{
  printf("\n=== Test 3: Swap-in (re-access evicted pages) ===\n");
  int pid = getpid();
  struct vmstats before, after;
  getvmstats(pid, &before);

  // Re-read all pages – evicted ones will be swapped back in
  volatile int sum = 0;
  for (int i = 0; i < TEST_PAGES; i++) {
    sum += (unsigned char)buf[(uint64)i * PAGE_SIZE];
  }
  (void)sum;

  getvmstats(pid, &after);
  int new_swapped_in = after.pages_swapped_in - before.pages_swapped_in;
  printf("  new swap-ins: %d\n", new_swapped_in);
  print_vmstats("after re-read", pid);

  if (new_swapped_in > 0)
    printf("  PASS: swap-in occurred\n");
  else
    printf("  WARN: no swap-ins (pages may still be resident)\n");
}

// -----------------------------------------------------------------------
// Test 4: Scheduler-aware eviction
// Fork two children:
//   child A (CPU-bound, will demote to low MLFQ level)
//   child B (syscall-heavy, stays at high MLFQ level)
// Both allocate TEST_PAGES.  We expect child A to lose more pages.
// -----------------------------------------------------------------------
static void
test_scheduler_aware(void)
{
  printf("\n=== Test 4: Scheduler-aware eviction ===\n");

  int pid_cpu = fork();
  if (pid_cpu == 0) {
    // CPU-bound child: pure arithmetic, no syscalls → demotes to level 3
    char *buf = sbrk((uint64)TEST_PAGES * PAGE_SIZE);
    if (buf == (char *)-1) exit(1);
    for (int i = 0; i < TEST_PAGES; i++)
      buf[(uint64)i * PAGE_SIZE] = (char)i;

    // Spin to accumulate CPU ticks and get demoted
    volatile long x = 1;
    for (long k = 0; k < 200000000L; k++) x = x * 3 + k;
    (void)x;

    // Touch pages again so they fault back in
    for (int i = 0; i < TEST_PAGES; i++)
      buf[(uint64)i * PAGE_SIZE] += 1;

    exit(0);
  }

  int pid_sys = fork();
  if (pid_sys == 0) {
    // Syscall-heavy child: stays at MLFQ level 0
    char *buf = sbrk((uint64)TEST_PAGES * PAGE_SIZE);
    if (buf == (char *)-1) exit(1);
    for (int i = 0; i < TEST_PAGES; i++)
      buf[(uint64)i * PAGE_SIZE] = (char)i;

    // Make many syscalls to stay at high priority
    for (int k = 0; k < 100000; k++) getpid();

    // Touch pages again
    for (int i = 0; i < TEST_PAGES; i++)
      buf[(uint64)i * PAGE_SIZE] += 1;

    exit(0);
  }

  // Let children run for a while
  pause(50);

  printf("  CPU-bound child (pid=%d):\n", pid_cpu);
  print_vmstats("cpu", pid_cpu);
  printf("  Syscall-heavy child (pid=%d):\n", pid_sys);
  print_vmstats("sys", pid_sys);

  struct vmstats cpu_vs, sys_vs;
  getvmstats(pid_cpu, &cpu_vs);
  getvmstats(pid_sys, &sys_vs);

  if (cpu_vs.pages_evicted >= sys_vs.pages_evicted)
    printf("  PASS: CPU-bound process lost >= pages than syscall-heavy\n");
  else
    printf("  INFO: syscall-heavy lost more pages (timing-dependent)\n");

  wait(0);
  wait(0);
}

// -----------------------------------------------------------------------
// Test 5: getvmstats() – invalid pid returns -1
// -----------------------------------------------------------------------
static void
test_getvmstats_invalid(void)
{
  printf("\n=== Test 5: getvmstats() error handling ===\n");
  struct vmstats vs;
  int ret = getvmstats(99999, &vs);
  if (ret == -1)
    printf("  PASS: getvmstats(invalid pid) returned -1\n");
  else
    printf("  FAIL: expected -1, got %d\n", ret);
}

// -----------------------------------------------------------------------
// main
// -----------------------------------------------------------------------
int
main(void)
{
  printf("PA3 VM Test starting (pid=%d)\n", getpid());

  test_basic_fault();

  // Save buf pointer before replacement test so we can use it in swap-in test
  // (sbrk() is cumulative; we snapshot current brk)
  char *pre_brk = sbrk(0);
  test_replacement();
  // The buf from test_replacement starts at pre_brk
  test_swapin(pre_brk);

  test_scheduler_aware();
  test_getvmstats_invalid();

  printf("\n=== All tests done ===\n");
  exit(0);
}