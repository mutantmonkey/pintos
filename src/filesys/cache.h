#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include <stdbool.h>

#define NUM_BLOCK 32
#define NUM_PAGES 8

struct cache_block;

/** 
 * Reserve a block in the buffer cache dedicated to hold this sector,
 * possibly evicting some other unused sector.
 * Either grants exclusive or shared access. 
 */
struct cache_block *cache_get_block (block_sector_t sector, bool exclusive);

/* Release access to cache block. */
void cache_put_block(struct cache_block *b);

/**
 * Read cache block from disk, returns pointer to data.
 */
void *cache_read_block (struct cache_block *b);

/**
 * Fill cache block with zeros, returns pointer to zeroed data.
 */
void *cache_zero_block (struct cache_block *b);

/**
 * Mark cache block dirty (must write data to disk).
 */
void cache_mark_block_dirty (struct cache_block *b);

/**
 * Initializes the buffer cache.
 */
void buffer_initialize (void);

/**
 * Prefetches a block to exploit spatial locality.
 */
// TODO: INSERT cache_readahead!

/**
 * Shuts down the buffer cache. Writes back dirty memory to disk and
 * frees all allocated physical memory.
 */
void buffer_shutdown (void);

void buffer_writeback (void *);
#endif
