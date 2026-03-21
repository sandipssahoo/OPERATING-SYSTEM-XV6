#include "kernel/types.h"
#include "user/user.h"

int main(){
    int pid=getppid();
    if(pid==-1){
        printf("No parent exists\n");
    }
    
    else{
        printf("PID of parent is %d\n",pid);
    }
    exit(0);
}