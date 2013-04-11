#include "cache.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "devices/timer.h"
#include <bitmap.h>
#include <math.h>
#include <list.h>
#include <string.h>
#include <stdio.h>
#include "devices/block.h"

/**
 * This buffer cache uses the Clock with Adaptive Replacement
 * algorithm for cache management described in a whitepaper by Bansal
 * and Modha. Thus, four lists are used in order to keep track of
 * frequent and recent cache entries, along with the histories of each.
 */

struct cache_block {
  
  block_sector_t block_sector_id; // Index of disk sector
  bool dirty; // Dirty bit -- Needs write back
  bool valid; // Valid bit -- Matches on-disk data
  
  uint32_t readers; // # of threads reading this block
  uint32_t writers; // # of threads writing to this block
  uint32_t pending; // # of threads waiting to do I/O on this block
  struct lock IO_lock; // IO concurrency lock
  struct condition announcer; // Signal variable for other threads

  bool reference; // Reference bit for use in cache eviction
  struct list_elem elem;
  // Eviction policy information
  // goes here.

  void *data; // Pointer to this blocks data.
};

/* BEGIN GLOBAL VARIABLES */

struct block *disk;
static bool shutdown = false;
static struct bitmap *cache_free;
//static void *pool[2 * NUM_PAGES];
static void *pool;

// You can thank the authors of the whitepaper for this wonderful
// nomenclature. 
static struct list T1, T2, B1, B2;
static struct lock list_lock, map_lock;
static uint32_t p;

// Cache flushing daemon
tid_t kcached;

/* END GLOBAL VARIABLES */



/* BEGIN FUNCTION PROTOTYPES */

static struct cache_block * contains(struct list *, block_sector_t);
static void replace (void);
static void init_block (struct cache_block *);
void buffer_writeback (void * UNUSED);
static size_t locking_list_size (struct list *);

/* END FUNCTION PROTOTYPES */


struct cache_block *
cache_get_block (block_sector_t sector, bool exclusive)
{
  struct cache_block *hit = NULL;
  if ((hit = contains(&T1, sector)) || (hit = contains(&T2, sector)))
    {
      lock_acquire (&hit->IO_lock);
      // Cache hit
      hit->reference = true;
      
      if (exclusive)
	{
	  // Block needs to write, but there is already a writer or readers
	  hit->pending++;
	  while (hit->writers == 1 || hit->readers > 0)
	    cond_wait (&hit->announcer, &hit->IO_lock);
	  hit->pending--;
	  hit->writers = 1;
	}
      else
	{
	  // Block needs to read, but there may be a writer.
	  hit->pending++;
	  while (hit->writers == 1)
	    cond_wait (&hit->announcer, &hit->IO_lock);
	  hit->pending--;
	  hit->readers++;
	}
      lock_release (&hit->IO_lock);
      return hit;
    }
  else
    {
      if (locking_list_size (&T1) + locking_list_size (&T2) == NUM_BLOCK)
	{
	  // Cache full, replace a page from the cache
	  replace();
	  if (!list_empty (&B1) && !(contains(&B1, sector) || contains(&B2, sector)) &&
	      (locking_list_size (&T1) + locking_list_size (&B1) == NUM_BLOCK))
	    {
	      // Discard the LRU page in B1
	      lock_acquire (&list_lock);
	      struct list_elem *e = list_begin (&B2);
	      list_remove(e);
	      struct cache_block * remove = list_entry(e, struct cache_block, elem);
	      lock_release (&list_lock);

	      // Write it back if necessary
	      if (!remove->valid || remove->dirty)
		block_write(disk, remove->block_sector_id, remove->data);

	      // Reflect the change in the free map
	      size_t bit = (remove->data - pool) / BLOCK_SECTOR_SIZE;
	      lock_acquire (&map_lock);
	      ASSERT (bitmap_test (cache_free, true) == true);
	      bitmap_set (cache_free, bit, false);
	      lock_release (&map_lock);

	      hit = remove;
	    }
	  else if ((locking_list_size (&T1) + locking_list_size (&T2)
		    + locking_list_size (&B1) + locking_list_size (&B2) 
		    == 2 * NUM_BLOCK) &&
		   !(contains(&B1, sector) || contains(&B2, sector))
		   && !list_empty (&B2))
	    {
	      // Discard the LRU page in B2
	      lock_acquire (&list_lock);
	      struct list_elem *e = list_begin (&B2);
	      list_remove(e);
	      struct cache_block * remove = list_entry(e, struct cache_block, elem);
	      lock_release (&list_lock);

	      // Write it back if necessary
	      if (!remove->valid || remove->dirty)
		block_write (disk, remove->block_sector_id, remove->data);

	      // Reflect the change in the free map
	      lock_acquire (&map_lock);
	      size_t bit = (remove->data - pool) / BLOCK_SECTOR_SIZE;
	      ASSERT (bitmap_test (cache_free, true) == true);
	      bitmap_set (cache_free, bit, false);
	      lock_release (&map_lock);

	      hit = remove;
	    }
	}

      // Cache directory miss
      if (!(contains(&B1, sector) || contains(&B2, sector)))
	{
	  // Insert x at the tail of T1. Set the page reference bit to
	  // 0.
	  if (hit == NULL)
	    {
	      hit = malloc(sizeof(struct cache_block));
	      if (hit == NULL)
		PANIC("NO MEMORY!");
	    }
	  
	  init_block(hit);
	  if (exclusive)
	    hit->writers = 1;
	  else
	    hit->readers = 1;

	  hit->block_sector_id = sector;

	  // Find a free page
	  lock_acquire (&map_lock);
	  size_t bit = bitmap_scan(cache_free, 0, 1, false);
	  if (bit == BITMAP_ERROR)
	    PANIC("Could not find free page!");
	  
	  bitmap_set(cache_free, bit, true);
	  lock_release (&map_lock);
	  
	  // Assign cache memory
	  hit->data = pool + bit * BLOCK_SECTOR_SIZE;
	  block_read (disk, hit->block_sector_id, hit->data);
	  
	  lock_acquire (&list_lock);
      	  list_push_back(&T1, &hit->elem);
	  lock_release (&list_lock);
	 
	}
      // Cache directory hit
      else if ((hit = contains(&B1, sector)))
	{
	  // Adapt: Increase the target size for the list T1 as: p =
	  // min{p + max{1, |B2|/|B1|}, c}
	  size_t b1 = locking_list_size (&B1);
	  size_t b2 = locking_list_size (&B2);
	  uint32_t prop = b2 / b1;
	  
	  p = min (p + max (1, prop), NUM_BLOCK);
	  // Move x at the tail of T2. Set the page reference bit of x
	  // to 0.
	  lock_acquire (&list_lock);
	  list_remove (&hit->elem);
	  list_push_back(&T2, &hit->elem);
	  lock_release (&list_lock);
	  hit->reference = false;
	}
      // Cache directory hit
      else if ((hit = contains (&B2, sector))) // x must be in B2
	{
	  // Adapt: Decrease the target size for the list T1 as: p =
	  // max{p - max{1, |B1|/|B2|}, 0}
	  size_t b1 = locking_list_size (&B1);
	  size_t b2 = locking_list_size (&B2);
	  uint32_t prop = b1 / b2;

	  p = max (p - max (1, prop), 0);
	  // Move x at the tail of T2. Set the page reference bit of x
	  // to 0.
	  lock_acquire (&list_lock);
	  list_remove (&hit->elem);
	  list_push_back(&T2, &hit->elem);
	  lock_release (&list_lock);
	  hit->reference = false;
	}
    }
  return hit;
}

/**
 * Responsible for choosing a page to replace and then storing that page
 * in the history lists.
 */
static void 
replace ()
{
  bool found = false;
  struct cache_block *block;
  while (!found)
    {
      if (locking_list_size (&T1) >= (unsigned)max(1, p)) //T1 is too full
	{
	  lock_acquire (&list_lock);
	  block = list_entry(list_pop_front(&T1), struct cache_block, elem);
	  if (block->reference == false) // If reference bit of head page of T1 is 0
	    {
	      // We found a winner.
	      found = true;
	      list_push_front(&B1, &block->elem);
	    }
	  else
	    {
	      // Too bad, keep going.
	      block->reference = false;
	      list_push_back(&T2, &block->elem);
	    }
	  lock_release (&list_lock);
	}
      else
	{
	  lock_acquire (&list_lock);
	  block = list_entry(list_pop_front(&T2), struct cache_block, elem);
	  if (block->reference == false) // If reference bit of head page of T2 is 0
	    {
	      // We found a winner.
	      found = true;
	      list_push_front(&B2, &block->elem);
	    }
	  else
	    {
	      // Too bad, keep going.
	      block->reference = false;
	      list_push_back(&T2, &block->elem);
	    }
	  lock_release (&list_lock); 
	}
    }

  // We've just removed a page from T1 or T2 so we can add one now.
}

void
cache_put_block (struct cache_block *b)
{
  lock_acquire (&b->IO_lock);
  if (b->writers == 1)
    b->writers = 0;
  else if (b->readers > 0)
    b->readers--;
  cond_signal (&b->announcer, &b->IO_lock);
  lock_release (&b->IO_lock);
}

void *
cache_read_block (struct cache_block *b)
{
  b->reference = true;
  return b->data;
}

void *
cache_zero_block (struct cache_block *b)
{
  b->reference = true;
  b->valid = false;
  b->dirty = true;
  memset (b->data, 0, BLOCK_SECTOR_SIZE);
  return b->data;
}

void
cache_mark_block_dirty (struct cache_block *b)
{
  b->valid = false;
  b->dirty = true;
}

/*
static void readahead (void)
{
  PANIC("readahead not implemented.");
}
*/

/**
 * Initializes a cache_block structure with some default information.
 * Does not initialize the data pointer, however.
 */
void init_block (struct cache_block *block)
{
  block->dirty = false;
  block->valid = true;
  block->readers = 0;
  block->writers = 0;
  block->pending = 0;
  lock_init(&block->IO_lock);
  cond_init(&block->announcer);
  block->reference = false;
  block->elem = (struct list_elem) {NULL, NULL};
  block->data = NULL;
}

/**
 * Initializes the buffer cache for use by the file system.
 */
void buffer_initialize ()
{
  disk = block_get_role(BLOCK_FILESYS);

  lock_init(&list_lock);
  lock_init(&map_lock);

  // Set up the free map
  cache_free = bitmap_create (2 * NUM_BLOCK);
  bitmap_set_all (cache_free, false);

  p = 0;

  list_init(&T1);
  list_init(&T2);
  list_init(&B1);
  list_init(&B2);

  // Allocate space for the data
  pool = palloc_get_multiple (0, 2 * NUM_PAGES);
  /*
    int i = 0;
    for (; i < 2 * NUM_PAGES; i++)
    {
    pool[i] = palloc_get_page(0);
    }
  */

  /*  
      for (i = 0; i < 2 * NUM_BLOCK; i++)
      {
      if ((sizeof(struct cache_block) * (i - last_page) + 
      sizeof(struct cache_block)) > 4096)
      {
      // Allocate more space for cache_block information since 
      // we've run out
      last_page = i;
      blocks[last_page] = palloc_get_page(0);
      }
      else
      {
      // Set the address of block[i] to be within the memory space
      // of the last allocated page.
      blocks[i] = blocks[last_page] + 
      sizeof(struct cache_block) * (i - last_page);
      }

      // Set the data field of a block to a memory address in the pool.
      blocks[i]->data = pool[i/8] + ((i % 8) * 512);

      // Initialize block information
      blocks[i]->block_sector_id = 0;
      blocks[i]->dirty = false;
      blocks[i]->valid = true;
      blocks[i]->readers = 0;
      blocks[i]->writers = 0;
      blocks[i]->pending = 0;
      lock_init(&blocks[i]->IO_lock);
      cond_init(&blocks[i]->announcer);
      }
  */
  
  //   kcached = thread_create ("KCacheD", 40, buffer_writeback, NULL);
}

/**
 * Responsible for shutting down the buffer cache and making sure everything
 * gets written back to disk properly.
 */
void buffer_shutdown (void)
{

  // Write everything back
  buffer_writeback (NULL);
  shutdown = true;

  int i = 0;
  // Deallocate data space
  for (; i < NUM_PAGES; i++)
    {
      palloc_free_page(pool + i * PGSIZE);
    }
  
  /*
  // Deallocate space for the metadata
  int pages = sizeof(struct cache_block) * NUM_BLOCK / 4096;
  int last_page = 0;
  for (i = 0; i < NUM_BLOCK; i++)
    {
      if ((sizeof(struct cache_block) * (i - last_page) + 
	   sizeof(struct cache_block)) > 4096)
	{
	  // Deallocate metadata space
	  last_page = i;
	  palloc_free_page(blocks[last_page]);
	}
    }
  */
}

/**
 * Periodically writes back all of the information in the cache to disk.
 */
void buffer_writeback (void *kcached_started_ UNUSED)
{
  while (!shutdown)
    {
      struct list_elem *e;
      struct cache_block *b;
      lock_acquire (&list_lock);
      for (e = list_begin (&T1); e != list_end (&T1); e = list_next (e))
	{
	  b = list_entry (e, struct cache_block, elem);

	  lock_acquire (&b->IO_lock);
	  while (b->writers == 1 || b->readers > 0)
	    cond_wait (&b->announcer, &b->IO_lock);

	  block_write (disk, b->block_sector_id, b->data);
	  b->dirty = false;
	  b->valid = true;
	  cond_signal (&b->announcer, &b->IO_lock);
	  lock_release (&b->IO_lock);
	}
      for (e = list_begin (&T2); e != list_end (&T2); e = list_next (e))
	{
	  b = list_entry (e, struct cache_block, elem);

	  lock_acquire (&b->IO_lock);
	  while (b->writers == 1 || b->readers > 0)
	    cond_wait (&b->announcer, &b->IO_lock);
	  
	  block_write (disk, b->block_sector_id, b->data);
	  b->dirty = false;
	  b->valid = true;
	  cond_signal (&b->announcer, &b->IO_lock);
	  lock_release (&b->IO_lock);
	}
      lock_release (&list_lock);
      timer_msleep(1000 * 30);
    }
}

/**
 * Returns whether or not a given list contains the given
 * sector. Required in order to implement CAR.
 */
static struct cache_block *
contains (struct list *list, block_sector_t sector)
{

  lock_acquire (&list_lock);
  struct list_elem *e = list_begin(list);
  for (; e != list_end(list); e = list_next(e))
    {
      struct cache_block *cur;
      cur = list_entry(e, struct cache_block, elem);

      if (cur->block_sector_id == sector)
	{
	  lock_release (&list_lock);
	  return cur;
	}
    }
  lock_release (&list_lock);
  return NULL;
}

static size_t 
locking_list_size (struct list *list)
{
  lock_acquire (&list_lock);
  size_t ret = list_size (list);
  lock_release (&list_lock);
  return ret;
}
