#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/* A directory. */
struct dir 
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

/* A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
  return inode_create (sector, entry_cnt * sizeof (struct dir_entry));
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct file *
dir_open (struct inode *inode) 
{
  return file_open (inode);
  /* struct dir *dir = calloc (1, sizeof *dir); */
  /* if (inode != NULL && dir != NULL) */
  /*   { */
  /*     dir->inode = inode; */
  /*     dir->pos = 0; */
  /*     return dir; */
  /*   } */
  /* else */
  /*   { */
  /*     inode_close (inode); */
  /*     free (dir); */
  /*     return NULL;  */
  /*   } */
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct file *
dir_open_root (void)
{
  return file_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct file *
dir_reopen (struct file *dir) 
{
  return file_reopen (dir);
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct file *dir) 
{
  file_close (dir);
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct file *dir) 
{
  return file_get_inode (dir);
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (struct file *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  size_t bytes_read;
  for (ofs = 0; (bytes_read = file_read_at (dir, &e, sizeof e, ofs)) == sizeof e;
       ofs += sizeof e) 
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }

  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (struct file *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  file_lock (dir);
  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;
  file_unlock (dir);

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct file *dir, const char *name, block_sector_t inode_sector)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  file_lock (dir);

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; file_read_at (dir, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = file_write_at (dir, &e, sizeof e, ofs) == sizeof e;

 done:
  file_unlock (dir);
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct file *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  file_lock (dir);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  if (inode_isdir (inode))
    {
      struct file *to_remove = dir_open (inode);
      if (!dir_is_empty (to_remove))
	{
	  dir_close (to_remove);
	  goto done;
	}
    }

  /* Erase directory entry. */
  e.in_use = false;
  if (file_write_at (dir, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  file_unlock (dir);
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct file *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  file_lock (dir);
  while (file_read (dir, &e, sizeof e) == sizeof e) 
    {
      //      dir->pos += sizeof e;
      if (e.in_use && !(strcmp (e.name, ".") == 0) &&
	  !(strcmp (e.name, "..") == 0))
        {
          strlcpy (name, e.name, NAME_MAX + 1);
	  file_unlock (dir);
          return true;
        } 
    }
  file_unlock (dir);
  return false;
}

bool
dir_is_empty (struct file *dir)
{
  struct dir_entry e;

  off_t ofs;
  file_lock (dir);
  for (ofs = 0; file_read_at (dir, &e, sizeof e, ofs) == sizeof(e);
       ofs += sizeof (struct dir_entry))
    {
      if (e.in_use && !((strcmp (e.name, ".") == 0) ||
		       (strcmp (e.name, "..") == 0)))
	{
	  file_unlock (dir);
	  return false;
	}
    }
  file_unlock (dir);
  return true;
}

struct file *
dir_resolve (const char *path_, int *name_off)
{
  char *path = malloc ((strlen(path_) + 1) * sizeof(char));
  char *to_free = path;
  if (path == NULL)
    return NULL;
  strlcpy (path, path_, strlen(path_) + 1);
  if (*(path + strlen(path)) == '/')
    *(path + strlen(path)) = '\0';
  
  int i = strlen(path);
  for (; i >= 0 && *(path + i) != '/'; i--)
    {
      *(path + i) = '\0';
    }
  if (name_off != NULL)
      *name_off = i + 1;
  while (i >= 0 && *(path + --i) == '/')
    {
      *(path + i) = '\0';
    }

  struct file *dir = NULL;
  if (*path == '/')
    {
      dir = dir_open (inode_open (ROOT_DIR_SECTOR));
      path++;
    }
  else
    {
      dir = dir_open (inode_open (thread_current ()->cwd));
    }

  char *token, *save_ptr = NULL;
  struct inode *found = NULL;
  for (token = strtok_r (path, "/", &save_ptr);
       token != NULL;
       token = strtok_r (NULL, "/", &save_ptr))
    {
      dir_lookup (dir, token, &found);
      dir_close (dir);
      if (found == NULL)
	{
	  free (to_free);
	  return NULL;
	}
      if (!inode_isdir (found))
	{
	  free (to_free);
	  return NULL;
	}
      dir = dir_open (found);
    }
  return dir;
}
