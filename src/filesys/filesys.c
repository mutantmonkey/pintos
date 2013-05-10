#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/cache.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  buffer_initialize ();
  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
  buffer_shutdown ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;
  int name_offset;
  struct file *dir = dir_resolve (name, &name_offset);
  const char *filename = name + name_offset;
  if (strchr(filename, '/'))
    {
      dir_close (dir);
      return false;
    }

  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size)
                  && dir_add (dir, filename, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
}

bool
filesys_mkdir (const char *name)
{
  // Ignore empty file names.
  if (*name == '\0')
    return false;

  block_sector_t inode_sector = 0, parent = 0;
  int name_offset;
  char *find = NULL;
  struct file *dir = dir_resolve (name, &name_offset);
  const char *filename = name + name_offset;
  if (dir == NULL)
    return false;

  find = calloc (1, sizeof(char) * strlen(filename));
  if (find == NULL)
    {
      dir_close (dir);
      return false;
    }

  if (strchr (filename, '/'))
    strlcpy (find, filename, strlen(filename));
  else
    strlcpy (find, filename, strlen(filename) + 1);

  parent = file_get_inumber(dir);
  // Figure out directory name
  bool success = (dir != NULL
		  && free_map_allocate (1, &inode_sector)
		  && dir_create (inode_sector, 4096)
		  && dir_add (dir, find, inode_sector));
  dir_close (dir);
  if (success == false)
    {
      free (find);
      return success;
    }
  dir = dir_open (inode_open (inode_sector));
  inode_set_mode (file_get_inode (dir), DIRECTORY);
  if (!dir_add (dir, ".", inode_sector) || !dir_add (dir, "..", parent))
    {
      struct file *par = dir_open (inode_open (parent));
      dir_remove (par, find);
      dir_close (par);
      success = false;
    }
  dir_close (dir);

  free (find);
  return success;
}

bool
filesys_chdir (const char *name)
{
  // They want the root, so give it to them!
  // Bit of a kludge, but not especially.
  if (strcmp (name, "/") == 0)
    {
      thread_current ()->cwd = ROOT_DIR_SECTOR;
      return true;
    }
  if (strcmp (name, "..") == 0)
    {
      struct file *dir = dir_open (inode_open (thread_current ()->cwd));
      if (dir == NULL)
	return false;
      struct inode *found;
      dir_lookup (dir, "..", &found);
      dir_close (dir);
      if (found != NULL)
	{
	  thread_current ()->cwd = inode_get_inumber (found);
	  inode_close (found);
	  return true;
	}
      return false;
    }
  if (strcmp (name, ".") == 0)
    return true;

  int name_offset;
  char *find = NULL;
  struct file *dir = dir_resolve (name, &name_offset);
  const char *filename = name + name_offset;
  if (dir == NULL)
    return false;

  find = calloc (1, sizeof(char) * strlen(filename));
  if (find == NULL)
    {
      dir_close (dir);
      return false;
    }

  if (strchr (filename, '/'))
    strlcpy (find, filename, strlen(filename));
  else
    strlcpy (find, filename, strlen(filename) + 1);
      
  // Figure out directory name
  struct inode *found;
  dir_lookup (dir, find, &found);
  if (found == NULL)
    {
      dir_close (dir);
      free (find);
      return false;
    }
  thread_current ()->cwd = inode_get_inumber (found);
  inode_close (found);
  dir_close (dir);
  free (find);
  return true;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  if (strcmp (name, "/") == 0)
    return dir_open_root ();

  int name_offset;
  char *find = NULL;
  struct file *dir = dir_resolve (name, &name_offset);
  const char *filename = name + name_offset;
  if (dir == NULL)
    return false;

  find = calloc (1, sizeof(char) * strlen(filename));
  if (find == NULL)
    {
      dir_close (dir);
      return false;
    }

  if (strchr ((name + name_offset), '/'))
    strlcpy (find, filename, strlen(filename));
  else
    strlcpy (find, filename, strlen(filename) + 1);
  struct inode *inode = NULL;

  dir_lookup (dir, find, &inode);
  dir_close (dir);
  free (find);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  // No, you can't remove this. Period.
  if (strcmp (name, "/") == 0)
    return false;

  int name_offset;
  char *find = NULL;
  struct file *dir = dir_resolve (name, &name_offset);
  const char *filename = name + name_offset;
  if (dir == NULL)
    return false;

  find = calloc (1, sizeof(char) * strlen(filename));
  if (find == NULL)
    {
      dir_close (dir);
      return false;
    }

  if (strchr ((name + name_offset), '/'))
    strlcpy (find, filename, strlen(filename));
  else
    strlcpy (find, filename, strlen(filename) + 1);

  bool success = dir != NULL && dir_remove (dir, find);
  dir_close (dir); 
  free (find);

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 4096))
    PANIC ("root directory creation failed");
  struct file *root = dir_open (inode_open (ROOT_DIR_SECTOR));
  inode_set_mode (file_get_inode(root), DIRECTORY);
  dir_add (root, ".", ROOT_DIR_SECTOR);
  dir_add (root, "..", ROOT_DIR_SECTOR);
  dir_close (root);
  free_map_close ();
  printf ("done.\n");
}
