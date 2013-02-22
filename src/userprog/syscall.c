#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include <hash.h>

static void syscall_handler (struct intr_frame *);

static void sys_halt (void);
static int sys_exit (int status);
//pid_t sys_exec (const char *file);
//int sys_wait (pid_t);
static bool sys_create (const char *file, unsigned initial_size);
static bool sys_remove (const char *file);
static int sys_open (const char *file);
static unsigned sys_filesize (int fd);
static int sys_read (int fd, void *buffer, unsigned length);
static int sys_write (int fd, const void *buffer, unsigned length);
//void sys_seek (int fd, unsigned position);
//unsigned sys_tell (int fd);
//void sys_close (int fd);

#define FIRST(f) (*(f + 1))
#define SECOND(f) (*(f + 2))
#define THIRD(f) (*(f + 3))

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
      sys_create ((char *)FIRST(p), SECOND(p));
      break;
    case SYS_REMOVE:
      break;
    case SYS_OPEN:
      break;
    case SYS_FILESIZE:
      sys_filesize (FIRST(p));
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

static void
sys_halt (void)
{
  shutdown_power_off ();
}

static int
sys_exit (int status UNUSED)
{
  thread_exit ();
  // TODO: deal with parent processes
}

static bool
sys_create (const char *file, unsigned initial_size)
{
  if ((void *)file >= PHYS_BASE)
    return false;
  return filesys_create (file, initial_size);
}

static inline bool
is_valid_fd (int fd)
{
  return (fd != 0 && fd != 1) ? true : false;
}

static struct file *
get_file (int fd) {
  struct thread *t = thread_current ();
  struct fd_entry temp_entry = {fd, {{0, 0}}, NULL};
  struct hash_elem *entry = hash_find (&t->fd_table,
				       &temp_entry.elem);
  if (entry == NULL)
    return NULL;

  struct fd_entry *f = hash_entry(entry, struct fd_entry, elem);
  return f->file;
}
  

static unsigned 
sys_filesize (int fd) {
  if (!is_valid_fd(fd))
    return 0;

  struct file *file = get_file (fd);
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
  if (fd == 0)
    return 0;
  if (fd == 1 && length <= 512)
    {
      putbuf (buffer, length);
    }

  struct file *file = get_file (fd);
  if (file != NULL)
    return file_write (file, buffer, length);
  return 0;
}


// vim:ts=2:sw=2:et:
