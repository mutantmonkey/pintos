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
  if (!is_user_vaddr (p) || !is_user_vaddr (p + 1))
    sys_exit (-1);

  switch (*p) {
    case SYS_HALT:
      sys_halt ();
      break;
    case SYS_EXIT:
      sys_exit (*(p + 1));
      break;
    case SYS_EXEC:
      break;
    case SYS_WAIT:
      break;
    case SYS_CREATE:
      break;
    case SYS_REMOVE:
      break;
    case SYS_OPEN:
      break;
    case SYS_FILESIZE:
      break;
    case SYS_READ:
      break;
    case SYS_WRITE:
      break;
    case SYS_SEEK:
      break;
    case SYS_TELL:
      break;
    case SYS_CLOSE:
      break;
    default:
      sys_exit (-1);
      break;
  }
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
