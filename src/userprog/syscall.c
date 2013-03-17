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
#include "vm/vm.h"
struct lock sys_file_io;

static void syscall_handler (struct intr_frame *);
static void sys_halt (void);
//static int sys_exit (int status);
static pid_t sys_exec (const char *cmd_line);
static int sys_wait (pid_t pid);
static bool sys_create (const char *file, unsigned initial_size);
static bool sys_remove (const char *file);
static int sys_open (const char *file);
static unsigned sys_filesize (int fd);
static int sys_read (int fd, void *buffer, unsigned length);
static int sys_write (int fd, const void *buffer, unsigned length);
static void sys_seek (int fd, unsigned position);
static unsigned sys_tell (int fd);
static void sys_close (int fd);
static int mmap(int fd, void* addr, struct intr_frame*);
static void munmap(int mapping);

#define FIRST(f) (*(f + 1))
#define SECOND(f) (*(f + 2))
#define THIRD(f) (*(f + 3))

void
syscall_init (void) 
{
  lock_init(&sys_file_io);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static inline 
bool valid_ptr (void *ptr) {
  return (ptr < PHYS_BASE && ptr != NULL) ? true : false;
}

static void
syscall_handler (struct intr_frame *f) 
{
  int *p = f->esp;

  if (!is_user_vaddr (p) || !is_user_vaddr (p + 1) ||
      !is_user_vaddr (p + 2) || !is_user_vaddr (p + 3) ||
      !valid_ptr(p))
    sys_exit (-1);

  switch (*p) {
    case SYS_HALT:
      sys_halt ();
      break;
    case SYS_EXIT:
      f->eax = sys_exit (FIRST(p));
      break;
    case SYS_EXEC:
      f->eax = sys_exec ((char *)FIRST(p));
      break;
    case SYS_WAIT:
      f->eax = sys_wait (FIRST(p));
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
    case SYS_MMAP:
      f->eax = mmap(FIRST(p), (void *) *(p + 2), f);
      break;
    case SYS_MUNMAP:
      munmap (FIRST(p));
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

int
sys_exit (int status)
{
  *thread_current ()->exit_status = status;
  printf("%s: exit(%d)\n", thread_current ()->name, status);
  thread_exit ();

  return 0;
}

static pid_t
sys_exec (const char *cmd_line)
{
  if (!valid_ptr((void *)cmd_line))
    sys_exit (-1);
  pid_t result = process_execute (cmd_line);
  return result;
}

static int
sys_wait (pid_t pid)
{
  return process_wait (pid);
}

static bool
sys_create (const char *file, unsigned initial_size)
{
  if (!valid_ptr((void *)file))
    sys_exit (-1);

  lock_acquire(&sys_file_io);
  bool result = filesys_create (file, initial_size);
  lock_release(&sys_file_io);

  return result;
}

static int sys_open (const char *file)
{
  if (!valid_ptr((void *)file))
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

static int
mmap(int fd, void* addr, struct intr_frame *f UNUSED)
{
  //Don't allow the mmapped file descriptor to be equal to stdin or stdout
  if(!is_valid_fd(fd))
    return -1;

  //Disallow null addresses
  if(addr == NULL) 
    return -1;
 
  if(get_sup_page_entry(&thread_current()->sup_page_table, pg_round_down(addr)) != NULL)
  {
    return -1;
  }
 
  //if(addr <= (void*)(0x8048000))
    //return -1;
  //if(addr >= (void*)(PHYS_BASE - 4096))
    //return -1; 
  
  //All addresses should be page aligned
  if(pg_ofs(addr) != 0)
    return -1;

  struct file *file = get_file (fd, false);
  if(file == NULL)
    return -1;
  if(file_length(file) <= 0)
    return -1;
  struct file *reopened_file = file_reopen(file);
  int mapid_t = insert_mmap_entry(reopened_file, file_length(reopened_file), addr); 

  return mapid_t;
}

static void
munmap(int mapping)
{
  struct thread* thread = thread_current();
  lock_acquire(&thread->mmap_lock);
  lock_acquire(&sys_file_io);
  struct mmap_table_entry* entry;
  struct list_elem* e;
  for (e = list_begin(&thread->mmap_list); e != list_end(&thread->mmap_list); e = list_next(e))
  {
    entry = list_entry(e, struct mmap_table_entry, listelem);
    if(entry->mapid_t == mapping)
    {
      mmap_write_back(entry);
      hash_delete(&thread->mmap_table, &entry->elem);
    }
  }

  lock_release(&thread->mmap_lock); 
  lock_release(&sys_file_io);
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

  struct file *f = get_file (fd, true);
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
  if (!valid_ptr((void *)file))
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
  if (!valid_ptr((void *)buffer))
    sys_exit (-1);
  if (fd == 1)
    return 0;
  if (fd == 0)
    {
      while (length-- > 0)
	*(char *)(buffer)++ = input_getc ();
    }
  if (!is_valid_fd(fd))
    return -1;

  struct file *file = get_file (fd, false);
  if (file != NULL)
    {
      lock_acquire(&sys_file_io);
      int result = file_read (file, buffer, length);
      lock_release(&sys_file_io);
      return result;
    }
  return -1;
}

static int
sys_write (int fd, const void *buffer, unsigned length)
{
  if (!valid_ptr((void *)buffer))
    sys_exit (-1);
  if (fd == 0)
    return 0;
  if (fd == 1 && length <= 256)
    {
      putbuf (buffer, length);
      return length;
    }
  if (!is_valid_fd(fd))
    return -1;

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
