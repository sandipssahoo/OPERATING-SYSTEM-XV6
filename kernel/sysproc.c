#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"
#include "Vmstats.h"
extern struct proc proc[NPROC];

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if(t == SBRK_EAGER || n < 0) {
    if(growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if(addr + n < addr)
      return -1;
    if(addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
uint64
sys_hello(void){
  printf("Hello from the kernel!\n");
  return 0;
}
uint64
sys_getpid2(void){
  return myproc()->pid;
}

uint64
sys_getppid(void)
{
  struct proc *parent=myproc()->parent;
  if(parent!=0){
    return parent->pid;
  }
  
  return -1;
}

uint64
sys_getnumchild(void){
  struct proc *cur=myproc();
  struct proc *p;
  int cnt=0;

  for(p=proc;p<&proc[NPROC];p++){
    acquire(&p->lock);
    if(p->parent==cur && p->state!=ZOMBIE && p->state!=UNUSED){
      cnt++;
    }
    release(&p->lock);
  }
  return cnt;
}

uint64
sys_getsyscount(void){
  struct proc *cur=myproc();
  return cur->syscount;
}

uint64
sys_getchildsyscount(void){
  int pid;
  argint(0,&pid);
  struct proc *cur=myproc();
  struct proc *p;
  for(p=proc;p<&proc[NPROC];p++){
    acquire(&p->lock);
    if(p->parent==cur && p->pid==pid){
      int count=p->syscount;
      release(&p->lock);
      return count;
    }
    release(&p->lock);
  }
  return -1;
}
uint64
sys_getlevel(void)
{
  return myproc()->mlfq_level;
}

uint64
sys_getmlfqinfo(void)
{
  int pid;
  uint64 info_addr;
  
  argint(0, &pid);
  argaddr(1, &info_addr);  
  struct proc *target = 0;
  for(struct proc *p = proc; p < &proc[NPROC]; p++){
    if(p->pid == pid){
      target = p;
      break;
    }
  }
  
  if(target == 0) return -1;
  
  struct mlfqinfo {
    int level;
    int ticks[4];
    int times_scheduled;
    int total_syscalls;
  } info;
  
  acquire(&target->lock);
  info.level = target->mlfq_level;
  for(int i = 0; i < 4; i++){
    info.ticks[i] = target->ticks_per_level[i];
  }
  info.times_scheduled = target->times_scheduled;
  info.total_syscalls = target->syscount;
  release(&target->lock);
  
  if(copyout(myproc()->pagetable, info_addr, (char *)&info, sizeof(info)) < 0)
    return -1;
  
  return 0;
}

uint64
sys_getvmstats(void)
{
  int pid;
  uint64 info_addr;
 
  argint(0, &pid);
  argaddr(1, &info_addr);
 
  // Find target process
  struct proc *target = 0;
  extern struct proc proc[NPROC];
  for (struct proc *p = proc; p < &proc[NPROC]; p++) {
    if (p->pid == pid) {
      target = p;
      break;
    }
  }
  if (target == 0)
    return -1;
 
  // Build vmstats struct
  struct vmstats info;
  acquire(&target->lock);
  info.page_faults     = target->page_faults;
  info.pages_evicted   = target->pages_evicted;
  info.pages_swapped_in  = target->pages_swapped_in;
  info.pages_swapped_out = target->pages_swapped_out;
  info.resident_pages  = target->resident_pages;
  release(&target->lock);
 
  // Copy to user space
  if (copyout(myproc()->pagetable, info_addr,
              (char *)&info, sizeof(info)) < 0)
    return -1;
 
  return 0;
}
