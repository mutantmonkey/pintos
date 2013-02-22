#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
static void sys_halt (void) NO_RETURN;
static int sys_exit (int status);
static pid_t sys_exec (const char *file);
static int sys_wait (pid_t prog);
static bool sys_create (const char *file, unsigned initial_size);
static bool sys_remove (const char *file);
static int sys_open (const char *file);
static int sys_filesize (int fd);
static int sys_read (int fd, void *buffer, unsigned length);
static int sys_write (int fd, const void *buffer, unsigned length);
static void sys_seek (int fd, unsigned position);
static unsigned sys_tell (int fd);
static void sys_close (int fd);

#endif /* userprog/syscall.h */
