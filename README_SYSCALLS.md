A. Hello System call---------
First a simple hello system call was added this system call does not take any argument and only prints a message from the kernel it helped in understanding the complete flow of a system call from user space to kernel and back.

Implementation------------
1.
In user.h which is a user interface i have added hello system call.

2.
usys.pl which will help us to generate assembly code which acts like a bridge between user and kernel.

3.
In syscall.h i have defined a unique number for the perticular system call.

4.
now in syscall.c it acts like a dispatcher handle numbers associated with the function to handler.

5.
Finally in sysproc.c actually implemented the kernel functionality.

6.
Add system call name to the makefile to execute the system call properly

----------------------------------------------------------------------------
------------------------------**************--------------------------------
----------------------------------------------------------------------------

B. getpid2,getppid and getnumchild

Additional system calls like getpid2,getppid and getnumchild were implemented to return process related information these system calls access the process structure using myproc() and return values such as process id, parent process id and number of active child processes.


----------------------------------------------------------------------------
------------------------------**************--------------------------------
----------------------------------------------------------------------------

C. getsyscount and childcount

### I have taken the syscall getchildsyscount() as childcount() because of word limit.

For system call accounting, a syscall counter was added to the process structure. This counter keeps track of how many system calls a process has made since it was created. The counter is initialized when a process is created and is incremented inside the syscall dispatcher so that it increases for every system call.

The getsyscount system call returns the total number of system calls made by the calling process. This includes all system calls such as read, write, fork, printf, etc.

The getchildsyscount system call takes a process id as argument and returns the system call count of that process only if it is a child of the calling process. If the given pid is not a child or is invalid, the system call returns -1. Proper locking is used while scanning the process table to avoid race conditions.

