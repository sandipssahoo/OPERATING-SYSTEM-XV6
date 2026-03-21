// #include "kernel/types.h"
// #include "kernel/stat.h"
// #include "user/user.h"

// // CPU-bound process
// void cpu_bound() {
//     for(int i=0;i<16;i++){
//         for(int j=0;j<100000;j++){
//             continue;
//         }
//         pause(1);
//     }
// }

// // Syscall-heavy process
// void syscall_heavy() {
//   for(int i=0;i<16;i++){
//     int pid=getpid();
//     pid+=0;
//     pause(1);
//   }
// }

// int
// main(int argc, char *argv[])
// {
//   printf("MLFQ Scheduler Test\n");
  
//   int pid1 = fork();
//   if(pid1 == 0) {
//     // Child 1: CPU-bound
//     cpu_bound();
//     exit(0);
//   }
  
//   int pid2 = fork();
//   if(pid2 == 0) {
//     // Child 2: Syscall-heavy
//     syscall_heavy();
//     exit(0);
//   }
  
//   // Parent: collect stats
//   pause(16);
  
//   struct mlfqinfo info1, info2;
//   getmlfqinfo(pid1, &info1);
//   getmlfqinfo(pid2, &info2);
  
//   printf("CPU-bound process (PID %d):\n", pid1);
//   printf("  Level: %d\n", info1.level);
//   printf("  Total syscalls: %d\n", info1.total_syscalls);
//   printf("  Ticks per level: [%d, %d, %d, %d]\n", 
//     info1.ticks[0], info1.ticks[1], info1.ticks[2], info1.ticks[3]);
  
//   printf("Syscall-heavy process (PID %d):\n", pid2);
//   printf("  Level: %d\n", info2.level);
//   printf("  Total syscalls: %d\n", info2.total_syscalls);
//   printf("  Ticks per level: [%d, %d, %d, %d]\n", 
//     info2.ticks[0], info2.ticks[1], info2.ticks[2], info2.ticks[3]);
  
//   wait(0);
//   wait(0);
//   exit(0);
// }
#include "kernel/types.h"
#include "user/user.h"

// Mixed workload test.
// Children do NOT print - parent monitors both via getmlfqinfo.
// Demonstrates: CPU child demotes, SYS child stays at 0,
// and boost (every 128 ticks) pulls CPU child back to 0 temporarily.
int main(void)
{
  int pid_cpu = fork();
  if(pid_cpu == 0){
    // Pure CPU - zero syscalls
    volatile long x = 1;
    while(1){
      for(long i = 0; i < 100000000L; i++)
        x = x * 3 + i;
    }
    exit(0);
  }

  int pid_sys = fork();
  if(pid_sys == 0){
    // Pure syscalls - zero arithmetic
    while(1){
      for(int i = 0; i < 10000; i++)
        getpid();
    }
    exit(0);
  }

  printf("Mixed test: cpu_pid=%d  sys_pid=%d\n", pid_cpu, pid_sys);

  for(int i = 0; i < 13; i++){
    pause(10);
    struct mlfqinfo c, s;
    getmlfqinfo(pid_cpu, &c);
    getmlfqinfo(pid_sys, &s);
    printf("t=%d  CPU: lv=%d ticks=[%d,%d,%d,%d] sched=%d  SYS: lv=%d ticks=[%d,%d,%d,%d] sched=%d\n",
      (i+1)*10,
      c.level, c.ticks[0], c.ticks[1], c.ticks[2], c.ticks[3], c.times_scheduled,
      s.level, s.ticks[0], s.ticks[1], s.ticks[2], s.ticks[3], s.times_scheduled);
  }

  kill(pid_cpu);
  kill(pid_sys);
  wait(0);
  wait(0);
  printf("Mixed test done\n");
  exit(0);
}