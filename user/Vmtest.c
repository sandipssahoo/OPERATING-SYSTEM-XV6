#include "kernel/types.h"
#include "user/user.h"

#define PGSIZE  4096
#define NFRAMES 128

int pass=0,fail=0;

void check(const char *name, int cond) {
    if (cond){
        printf("  [PASS] %s\n", name); pass++; 
    }
    else{ 
        printf("  [FAIL] %s\n", name); fail++; 
    }
}

void get_stats(struct vmstats *s) {
    getvmstats(getpid(), s);
}

int main(void) {
    printf("=== PA3 Test ===\n");
    struct vmstats s;

    // ── Test 1: Basic lazy allocation + page faults
    printf("\n[1] Basic: %d pages, no eviction\n", NFRAMES / 2);
    int n = NFRAMES / 2;
    char *b = sbrklazy(n * PGSIZE);
    for (int i = 0; i < n; i++) b[i * PGSIZE] = (char)i;
    get_stats(&s);
    check("page_faults == n",        s.page_faults == n);
    check("no evictions yet",        s.pages_evicted == 0);
    check("resident == n",           s.resident_pages == n);
    sbrk(-(n * PGSIZE));

    // ── Test 2: Eviction triggered, data integrity 
    printf("\n[2] Eviction + data integrity: %d pages\n", NFRAMES + 50);
    n = NFRAMES + 100;
    b = sbrklazy(n * PGSIZE);
    for (int i = 0; i < n; i++) b[i * PGSIZE] = (char)(i & 0xff);

    int errs = 0;
    for (int i = 0; i < n; i++)
        if (b[i * PGSIZE] != (char)(i & 0xff)) errs++;

    get_stats(&s);
    printf("  faults=%d evicted=%d swapped_out=%d swapped_in=%d resident=%d\n",
           s.page_faults, s.pages_evicted,
           s.pages_swapped_out, s.pages_swapped_in, s.resident_pages);
    check("data intact after eviction",      errs == 0);
    check("evictions happened",              s.pages_evicted > 0);
    check("swap_out == evicted",             s.pages_evicted == s.pages_swapped_out);
    check("swap_in > 0",                     s.pages_swapped_in > 0);
    check("resident never exceeds NFRAMES",  s.resident_pages <= NFRAMES);
    sbrk(-(n * PGSIZE));

    // ── Test 3: Swap slot reuse (multiple cycles)
    printf("\n[3] Swap slot reuse across cycles\n");
    int all_ok = 1;
    for (int c = 0; c < 5; c++) {
        char *cb = sbrklazy((NFRAMES + 20) * PGSIZE);
        for (int i = 0; i < NFRAMES + 20; i++) cb[i * PGSIZE] = (char)(c + i);
        for (int i = 0; i < NFRAMES + 20; i++)
            if (cb[i * PGSIZE] != (char)(c + i)) { all_ok = 0; break; }
        sbrk(-((NFRAMES + 20) * PGSIZE));
    }
    check("data correct across 5 swap cycles", all_ok);

    // ── Test 4: getvmstats invalid PID
    printf("\n[4] getvmstats invalid PID\n");
    check("returns -1 for PID 99999", getvmstats(99999, &s) == -1);

    // ── Test 5: Scheduler-aware — CPU-bound loses more pages
    printf("\n[5] Scheduler-aware eviction\n");
    int pid_cpu = fork();
    if (pid_cpu == 0) {
        int npages = NFRAMES + 80;
        char *mb = sbrklazy((uint64)npages * PGSIZE);
        for (int j = 0; j < npages; j++) {
            mb[j * PGSIZE] = 1;
        }
        volatile long x = 0;
        for (long i = 0; i < 100L; i++) x += i;
        for (int iter = 0; iter < 10; iter++) {
            for (int j = 0; j < npages; j++) {
                volatile char c = mb[j * PGSIZE];
                c++;
            }
            for (long i = 0; i < 2000000L; i++) x += i;
        }
        pause(500); exit(0);
    }
    
    int pid_sys = fork();
    if (pid_sys == 0) {
        int npages = NFRAMES + 80;
        char *mb = sbrklazy((uint64)npages * PGSIZE);
        for (int j = 0; j < npages; j++) {
            mb[j * PGSIZE] = 2;
        }
        for (int iter = 0; iter < 20; iter++) {
            for (int j = 0; j < npages; j++) {
                volatile char c = mb[j * PGSIZE];
                c++;
                for (int k = 0; k < 1000; k++) getpid();
            }
        }
        pause(500); exit(0);
    }
    pause(150);

    struct vmstats sc, ss;
    getvmstats(pid_cpu, &sc);
    getvmstats(pid_sys, &ss);
    printf("  cpu_evicted=%d  sys_evicted=%d\n", sc.pages_evicted, ss.pages_evicted);
    check("cpu-bound evicted >= syscall-heavy", sc.pages_evicted >= ss.pages_evicted);

    kill(pid_cpu); kill(pid_sys);
    wait(0); wait(0);
    printf("\n=== Results: %d passed, %d failed ===\n", pass, fail);
    exit(0);
}