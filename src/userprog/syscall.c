#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"

static void syscall_handler (struct intr_frame *);

void sys_halt (void);
int sys_exit (int status);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  int *p = f->esp;
  if (!is_user_vaddr (p))
    sys_exit (-1);

  switch (*p) {
    case SYS_HALT:
      sys_halt ();
      break;
    case SYS_EXIT:
      sys_exit (-1);
      break;
  }

  printf ("system call!\n");
  thread_exit ();
}

void
sys_halt (void)
{
  shutdown_power_off ();
}

int
sys_exit (int status UNUSED)
{
  thread_exit ();
  // TODO: deal with parent processes
}

// vim:ts=2:sw=2:et:
