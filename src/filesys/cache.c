#include "cache.c"
#include "threads/synch.h"

struct cache_block {

  disk_sector_t block_sector_id; // Index of disk sector
  bool dirty; // Dirty bit -- Needs write back
  bool valid; // Valid bit -- Matches on-disk data
  uint32_t readers; // # of threads reading this block
  uint32_t writers; // # of threads writing to this block
  uint32_t pending; // # of threads waiting to do I/O on this block
  struct lock IO_lock; // IO concurrency lock
  struct condition announcer; // Signal variable for other threads
  // Eviction policy information
  // goes here.
  void *data; // Pointer to this blocks data.
};

struct bitmap *cache_free;

struct cache_block *
cache_get_block (disk_sector_t sector, bool exclusive)
{
  PANIC("cache_get_block not implemented.");
  return NULL;
}

void buffer_initialize ()
{
  cache_free = bitmap_create (NUM_BLOCK);
}
