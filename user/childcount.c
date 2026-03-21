#include "kernel/types.h"
#include "user/user.h"

int main()
{
  int child_pid = fork();
  
  if(child_pid == 0) {
    int count1 = getsyscount();
    printf("Child: Initial syscall count: %d\n", count1);
    
    getpid();
    getpid();
    getpid();
    
    int count2 = getsyscount();
    printf("Child: After 3 getpid() calls: %d\n", count2);
    
    exit(0);
  } else {
    pause(10); // child has time to make syscalls before parent checks count
    
    printf("Test 1: Parent reading child's syscall count\n");
    int child_count = getchildsyscount(child_pid);
    printf("  Child PID %d has made %d syscalls\n", child_pid, child_count);
    
    printf("\nTest 2: Reading non-existent child (PID 9999)\n");
    int invalid = getchildsyscount(9999);
    printf("  Result: %d\n", invalid);
    if(invalid == -1) {
      printf("  Correctly returned -1 for invalid PID\n");
    } else {
      printf("  ERROR: Should return -1!\n");
    }
    
    printf("\nTest 3: Parent reading own PID as child (should fail)\n");
    int parent_pid = getpid();
    int self = getchildsyscount(parent_pid);
    printf("  Result: %d\n", self);
    if(self == -1) {
      printf("  Correctly returned -1 (own process is not a child)\n");
    }
    
    wait(0);
    printf("\nTest 4: Child process exited\n");
  }
  
  exit(0);
}