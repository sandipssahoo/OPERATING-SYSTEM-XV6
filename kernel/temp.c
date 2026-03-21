if(which_dev == 2){
    printf("hi\n");
    // MLFQ Scheduler Logic
    p->ticks_at_level++;
    p->ticks_per_level[p->mlfq_level]++;
    
    // Global priority boost every 128 ticks using global ticks variable
    acquire(&tickslock);
    if(ticks % 128 == 0){
      // Move all RUNNABLE processes to Level 0
      struct proc *proc_ptr;
      for(proc_ptr = proc; proc_ptr < &proc[NPROC]; proc_ptr++){
        acquire(&proc_ptr->lock);
        if(proc_ptr->state == RUNNABLE){
          proc_ptr->mlfq_level = 0;
          proc_ptr->ticks_at_level = 0;
        }
        release(&proc_ptr->lock);
      }
    }
    release(&tickslock);
    
    // Check if time slice expired
    int quantum = get_quantum(p->mlfq_level);
    if(p->ticks_at_level >= quantum){
      // Calculate ΔS (syscalls) and ΔT (ticks)
      int delta_s = p->syscount - p->syscalls_at_level;
      int delta_t = p->ticks_at_level;
      
      // Demotion logic
      if(delta_s < delta_t && p->mlfq_level < 3){
        // CPU-bound: demote one level
        p->mlfq_level++;
      }
      // else: interactive (ΔS ≥ ΔT), stay at same level
      printf("Pid %d done after %d\n",p->pid,p->ticks_at_level);
      p->ticks_at_level = 0;
      yield();
    } 
  }