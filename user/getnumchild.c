#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  printf("Children before fork: %d\n", getnumchild());
  
  if(fork() == 0) {
    // Child process
    pause(9);
    exit(0);
  }
  if(fork() ==0) {
    pause(10);
    exit(0);
  }
  printf("Number of Children after 2 forks: %d\n",getnumchild());
  wait(0); 
  printf("Number of Children after 1 wait: %d\n",getnumchild());
  wait(0); 
  printf("Number of Children after 2 waits: %d\n",getnumchild());
  exit(0);
}