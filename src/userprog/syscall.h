#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

typedef int pid_t;
#define PID_ERROR ((pid_t) -1)

void syscall_init (void);
int sys_exit (int);

#endif /* userprog/syscall.h */
