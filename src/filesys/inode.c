#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include <stdio.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "devices/block.h"
#include "filesys/cache.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

static void inode_release_blocks (struct inode *);
static void inode_release_indirect (block_sector_t, int);

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    uint32_t mode;
    off_t size;
    block_sector_t direct[123];
    block_sector_t first_indirect;
    block_sector_t doubly_indirect;
    block_sector_t triply_indirect;
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    struct lock mod_lock;               /* Lock to keep the counters from getting screwed up */
    struct lock access;                 /* Someone wants sole access. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    off_t size;                         /* Indicates the size of the data,
					   used to prevent needless disk access. */
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos, bool allocate) 
{
  ASSERT (inode != NULL);
  
  block_sector_t ret_val;
  block_sector_t sec_idx[3] = {-1, -1, -1};
  struct cache_block *indirect = NULL;
  struct cache_block *b = cache_get_block (inode->sector, false);
  block_sector_t *on_disk_block;
  struct inode_disk *dsk = cache_read_block (b);
  uint32_t blocks_in = pos / BLOCK_SECTOR_SIZE;

  if (blocks_in < 123)
    {
      ret_val = dsk->direct[blocks_in];
      if (ret_val == (block_sector_t) -1 && allocate)
	{
	  free_map_allocate (1, &dsk->direct[blocks_in]);
	  ret_val = dsk->direct[blocks_in];
	}
      cache_put_block (b);
      return ret_val;
    }
  blocks_in -= 123;
  uint32_t i = 0;
  while (blocks_in >= 128)
    {
      sec_idx[i++] = blocks_in % 128;
      blocks_in /= 128;
    }
  sec_idx[i++] = blocks_in % 128;

  if (dsk->direct[122 + i] == (block_sector_t) -1 && allocate)
    {
      free_map_allocate (1, &dsk->direct[122 + i]);
      indirect = cache_get_block (dsk->direct[122 + i], true);
      on_disk_block = cache_zero_block (indirect);
      memset (on_disk_block, -1, BLOCK_SECTOR_SIZE);
    } 
  else if (dsk->direct[122 + i] == (block_sector_t) -1 && !allocate)
    goto no_allocate;
  else
    indirect = cache_get_block (dsk->direct[122 + i], true);

  cache_put_block (b);

  uint32_t j;
  for (j = 0; j < i - 1; j++) 
   {
     on_disk_block = cache_read_block (indirect);
     if (*(on_disk_block + sec_idx[j]) == (block_sector_t) -1 && allocate)
       free_map_allocate (1, (on_disk_block + sec_idx[j]));
     ret_val = *(on_disk_block + sec_idx[j]);
     cache_put_block (indirect);
     if (ret_val == (block_sector_t) -1)
       return ret_val;
     indirect = cache_get_block (ret_val, false);
   }
  on_disk_block = cache_read_block (indirect);
  if (*(on_disk_block + sec_idx[i - 1]) == (block_sector_t) -1 && allocate)
    free_map_allocate (1, (on_disk_block + sec_idx[i - 1]));

  ret_val = *(on_disk_block + sec_idx[i - 1]);
  cache_put_block (indirect);
  return ret_val;

 no_allocate:
  cache_put_block (b);
  return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;
static struct lock inodes_lock;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
  lock_init (&inodes_lock);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      //      size_t sectors = bytes_to_sectors (length);
      disk_inode->size = length;
      int i;
      for (i = 0; i < 126; i++)
	{
	  // Abuse our ability to go beyond the array
	  // to set the indirect blocks to -1.
	  // Also preallocate a couple blocks.
	  if (i < 2)
	    {
	      free_map_allocate (1, &disk_inode->direct[i]);
	    }
	  else
	    disk_inode->direct[i] = -1;
	}
      
      struct cache_block *block = cache_get_block (sector, true);
      void *data = cache_zero_block (block);
      memcpy (data, disk_inode, BLOCK_SECTOR_SIZE);
      cache_mark_block_dirty (block);
      cache_put_block (block);
      success = true;
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  lock_acquire (&inodes_lock);
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
	  // Don't allow someone to reopen deleted things.
	  if (!inode->removed)
	    inode_reopen (inode);
	  else
	    inode = NULL;
	  lock_release (&inodes_lock);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    {
      lock_release (&inodes_lock);
      return NULL;
    }

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  lock_init (&inode->mod_lock);
  lock_init (&inode->access);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  
  // Read the relevant data from the on disk inode
  struct cache_block *block = cache_get_block(inode->sector, false);
  struct inode_disk *data = cache_read_block(block);
  inode->size = data->size;
  cache_put_block(block);
  lock_release (&inodes_lock);
  
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    {
      lock_acquire (&inode->mod_lock);
      inode->open_cnt++;
      lock_release (&inode->mod_lock);
    }
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  lock_acquire (&inode->mod_lock);
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      lock_acquire (&inodes_lock);
      list_remove (&inode->elem);
      lock_release (&inodes_lock);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
	  inode_release_blocks (inode);
          free_map_release (inode->sector, 1);
	  //          free_map_release (inode->data.start,
	  //                bytes_to_sectors (inode->data.length)); 
        }

      lock_release (&inode->mod_lock);
      printf("\nFreeing %p for sector %d\n", inode, inode->sector);
      free (inode);
      goto skip_release;
    }
  lock_release (&inode->mod_lock);
 skip_release:
  return;
}

void inode_release_indirect (block_sector_t block_num, int depth)
{
  if (depth == 0)
    {
      free_map_release (block_num, 1);
      return;
    }
  struct cache_block *b = cache_get_block (block_num, true);
  block_sector_t *blocks = cache_read_block (b);
  int i = 0;
  for (; i < 128; i++)
    if (*(blocks + i) != (block_sector_t) -1)
      inode_release_indirect (*(blocks + i), depth - 1);
  cache_zero_block (b);
  cache_put_block (b);
}

void inode_release_blocks (struct inode *inode)
{
  struct cache_block *b = cache_get_block(inode->sector, true);
  struct inode_disk *disk = cache_read_block (b);
  int i;
  for (i = 0; i < 123; i++)
    {
      if (disk->direct[i] != (block_sector_t) -1)
	free_map_release (disk->direct[i], 1);
    }
  for (; i < 126; i++)
    if (disk->direct[i] != (block_sector_t) -1)
      inode_release_indirect (disk->direct[i], i - 122);
  cache_zero_block (b);
  cache_put_block (b);
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  lock_acquire (&inode->mod_lock);
  inode->removed = true;
  lock_release (&inode->mod_lock);
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  //  uint8_t *bounce = NULL;
  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset, false);
      if (sector_idx == (block_sector_t) -1)
	{
	  memset (buffer, 0, size);
	  return offset >= inode_length (inode) ? 0 : size;
	}
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
	  struct cache_block *block = cache_get_block(sector_idx, false);
	  void *data = cache_read_block (block);
	  memcpy (buffer + bytes_read, data, BLOCK_SECTOR_SIZE);
	  cache_put_block(block);
        }
      else 
        {
	  struct cache_block *block = cache_get_block(sector_idx, false);
	  void * data = cache_read_block (block);	  
          memcpy (buffer + bytes_read, data + sector_ofs, chunk_size);
	  cache_put_block(block);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  //  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  if (offset + size > inode_length (inode))
    {
      lock_acquire (&inode->mod_lock);
      inode->size = offset + size;
      lock_release (&inode->mod_lock);
      struct cache_block *b = cache_get_block (inode->sector, true);
      struct inode_disk *dsk = cache_read_block (b);
      dsk->size = offset + size;
      cache_put_block (b);
    }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = 0;
      sector_idx = byte_to_sector (inode, offset, true);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
	  struct cache_block *block = cache_get_block(sector_idx, false);
	  void *data = cache_zero_block (block);
	  memcpy (data, buffer + bytes_written, BLOCK_SECTOR_SIZE);
	  cache_mark_block_dirty (block);
	  cache_put_block (block);
	  //          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
	  struct cache_block *block = cache_get_block (sector_idx, false);
	  void *data = cache_read_block (block);
	  memcpy (data + sector_ofs, buffer + bytes_written, chunk_size);
	  cache_mark_block_dirty (block);
	  cache_put_block (block);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  lock_acquire (&inode->mod_lock);
  inode->deny_write_cnt++;
  lock_release (&inode->mod_lock);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/*
 * Sets the mode flag for the on disk inode.
 */
void inode_set_mode (struct inode *inode, uint32_t mode)
{
  struct cache_block *block = cache_get_block (inode->sector, true);
  struct inode_disk *dsk = cache_read_block (block);
  dsk->mode = mode;
  cache_mark_block_dirty (block);
  cache_put_block (block);
}

/* Returns if this inode corresponds to a directory or not. */
bool inode_isdir (struct inode *inode)
{
  bool isdir = false;
  struct cache_block *block = cache_get_block (inode->sector, false);
  struct inode_disk *dsk = cache_read_block (block);
  if ((dsk->mode & DIRECTORY) != 0)
    isdir = true;
  cache_put_block (block);
  return isdir;
}
/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  lock_acquire (&inode->mod_lock);
  inode->deny_write_cnt--;
  lock_release (&inode->mod_lock);
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->size;
}

void
inode_lock (struct inode *inode)
{
  lock_acquire (&inode->access);
}

void
inode_unlock (struct inode *inode)
{
  lock_release (&inode->access);
}
