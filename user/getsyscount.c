#include "kernel/types.h"
#include "user/user.h"

int main()
{
  int initial_count, after_calls;
    
  initial_count = getsyscount();
  printf("Test 1: Initial syscall count: %d\n", initial_count);
  
  // Make 3 system calls
  getpid();
  getpid();
  getpid();
  
  after_calls = getsyscount();
  printf("Test 2: After 3 getpid() calls: %d (increased by %d)\n", 
         after_calls, after_calls - initial_count);
  
  int final_count = getsyscount();
  
  printf("\nTest 3: Verify counter includes all syscalls\n");
  printf("  Total syscalls made: %d\n", final_count - initial_count);
  
  exit(0);
}