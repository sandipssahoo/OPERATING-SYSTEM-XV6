#include "kernel/types.h"
#include "user/user.h"

int main(){
    int pid=getpid2();
    printf("Current PID is %d\n",pid);

    exit(0);
}