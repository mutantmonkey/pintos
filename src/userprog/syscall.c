#include "userprog/process.h"
#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include <console.h>
#include <devices/input.h>

struct lock sys_file_io;

static void syscall_handler (struct intr_frame *);
static void sys_halt (void);
void sys_exit (int status);
//pid_t sys_exec (const char *file);
//int sys_wait (pid_t);
static bool sys_create (const char *file, unsigned initial_size);
static bool sys_remove (const char *file);
static int sys_open (const char *file);
static unsigned sys_filesize (int fd);
static int sys_read (int fd, void *buffer, unsigned length);
static int sys_write (int fd, const void *buffer, unsigned length);
static void sys_seek (int fd, unsigned position);
static unsigned sys_tell (int fd);
static void sys_close (int fd);

#define FIRST(f) (*(f + 1))
#define SECOND(f) (*(f + 2))
#define THIRD(f) (*(f + 3))

void
syscall_init (void) 
{
  lock_init(&sys_file_io);
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
      sys_exit (FIRST(p));
      break;
    case SYS_EXEC:
      break;
    case SYS_WAIT:
      break;
    case SYS_CREATE:
      f->eax = sys_create ((char *)FIRST(p), SECOND(p));
      break;
    case SYS_REMOVE:
      f->eax = sys_remove ((const char *)FIRST(p));
      break;
    case SYS_OPEN:
      f->eax = sys_open ((const char *)FIRST(p));
      break;
    case SYS_FILESIZE:
      f->eax = sys_filesize (FIRST(p));
      break;
    case SYS_READ:
      f->eax = sys_read (FIRST(p), (void *)SECOND(p), THIRD(p));
      break;
    case SYS_WRITE:
      f->eax = sys_write (FIRST(p), (void *)SECOND(p), THIRD(p));
      break;
    case SYS_SEEK:
      sys_seek (FIRST(p), SECOND(p));
      break;
    case SYS_TELL:
      f->eax = sys_tell (FIRST(p));
      break;
    case SYS_CLOSE:
      sys_close (FIRST(p));
      break;
    default:
      sys_exit (-1);
      break;
  }
}

static void
sys_halt (void)
{
  shutdown_power_off ();
}

void
sys_exit (int status UNUSED)
{
  thread_exit ();
  // TODO: deal with parent processes
}

static bool
sys_create (const char *file, unsigned initial_size)
{
  if ((void *)file >= PHYS_BASE)
    sys_exit (-1);

  lock_acquire(&sys_file_io);
  bool result = filesys_create (file, initial_size);
  lock_release(&sys_file_io);

  return result;
}

static int sys_open (const char *file)
{
  if ((void *)file >= PHYS_BASE)
    sys_exit (-1);

  struct thread *t = thread_current ();
  struct file *f;

  int i = 0;
  for (; i < 128; i++)
    if (t->fd_table[i] == NULL)
      {
	lock_acquire(&sys_file_io);
	f = filesys_open (file);
	lock_release(&sys_file_io);
	if (f == NULL)
	  return -1;

	t->fd_table[i] = f;
	return i + 2;
      }
  // Default, but unreachable
  return -1;
}

static inline bool
is_valid_fd (int fd)
{
  return (fd != 0 && fd != 1 && fd <= 130 && fd >= 2) ? true : false;
}

static struct file *
get_file (int fd, bool remove)
{
  struct thread *t = thread_current ();
  struct file *f = t->fd_table[fd - 2];
  if (remove)
    t->fd_table[fd - 2] = NULL;
  return f;
}

static void
sys_seek (int fd, unsigned position)
{
  if (!is_valid_fd(fd))
    return;

  struct file *f = get_file (fd, false);
  if (f != NULL)
      file_seek (f, position);
}

static void
sys_close (int fd)
{
  if (!is_valid_fd(fd))
    return;

  struct file *f = get_file (fd, false);
  if (f != NULL)
    file_close (f);
}

static unsigned
sys_tell (int fd)
{
  if (!is_valid_fd(fd))
    return 0;

  struct file *f = get_file (fd, false);
  if (f != NULL)
    return file_tell (f);
  return 0;
}

static bool
sys_remove (const char *file)
{
  if ((void *)file >= PHYS_BASE)
    return false;

  if (file != NULL)
    {
      lock_acquire(&sys_file_io);
      bool result = filesys_remove (file);
      lock_release(&sys_file_io);
      return result;
    }
  return false;
}
  
static unsigned 
sys_filesize (int fd)
{
  if (!is_valid_fd(fd))
    return 0;

  struct file *file = get_file (fd, false);
  if (file != NULL)
    return file_length (file);
  return 0;
}

static int
sys_read (int fd, void *buffer, unsigned length)
{
  if (fd == 1)
    return 0;
  if (fd == 0)
    {
      while (length-- > 0)
	*(char *)(buffer)++ = input_getc ();
    }

  struct file *file = get_file (fd, false);
  if (file != NULL)
    {
      lock_acquire(&sys_file_io);
      int result = file_read (file, buffer, length);
      lock_release(&sys_file_io);
      return result;
    }
  return -1;
      *(char *)(buffer++) = input_getc ();
    }

  struct file *file = get_file (fd);
  if (file != NULL)
    return file_read (file, buffer, length);
  return 0;
}

static int
sys_write (int fd, const void *buffer, unsigned length)
{
  if (buffer >= PHYS_BASE)
    sys_exit (-1);
  if (fd == 0)
    return 0;
  if (fd == 1 && length <= 256)
    {
      putbuf (buffer, length);
      return length;
    }

  struct file *file = get_file (fd, false);
  if (file != NULL)
    {
      lock_acquire(&sys_file_io);
      int result = file_write(file, buffer, length);
      lock_release(&sys_file_io);
      return result;
    }
  return 0;
}


// vim:ts=2:sw=2:et:
