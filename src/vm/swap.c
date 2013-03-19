//#include "vm/swap.h"
#include "vm/vm.h"
#include "devices/block.h"
#include "threads/vaddr.h"
#include "threads/synch.h"

struct bitmap *swap_space;
struct block *swap_drive;
struct lock swap_lock;

//Initialize the swap space by finding the swap device
//and by creating the bitmap representation
void swap_init(void)
{
  lock_init(&swap_lock);
  swap_drive = block_get_role(BLOCK_SWAP);  

  if(swap_drive == NULL)
    PANIC("Swap drive not found\n"); 

  size_t size_in_pages = (block_size(swap_drive) * BLOCK_SECTOR_SIZE)/PGSIZE;
  swap_space = bitmap_create(size_in_pages);

  bitmap_set_all(swap_space, false);
}

//Insert a frame into swap space
size_t insert_into_swap(void* frame_page)
{
  lock_acquire(&swap_lock);
  size_t swap_pos = bitmap_scan(swap_space, 0, 1, false);
  bitmap_set(swap_space, swap_pos, true);
  lock_release(&swap_lock); 
  
  
  size_t progress_pos = 0;
  for(; progress_pos < PGSIZE/BLOCK_SECTOR_SIZE; progress_pos++)
    block_write (swap_drive, swap_pos * (PGSIZE/BLOCK_SECTOR_SIZE) + progress_pos, frame_page + progress_pos*BLOCK_SECTOR_SIZE);

  return swap_pos;
}
//Remove a frame from swap space by setting the bitmap such
//that the data can be freely overwritten
void clear_swap_entry(size_t swap_pos)
{
  lock_acquire(&swap_lock);
  bitmap_set(swap_space, swap_pos, false);
  lock_release(&swap_lock); 
}

//Read swap data back into main memory
void retrieve_from_swap(size_t swap_pos, void* frame_page)
{

  size_t progress_pos = 0;
  for(; progress_pos < PGSIZE/BLOCK_SECTOR_SIZE; progress_pos++)
    block_read (swap_drive, swap_pos * (PGSIZE/BLOCK_SECTOR_SIZE) + progress_pos, frame_page + progress_pos*BLOCK_SECTOR_SIZE);

  lock_acquire(&swap_lock);
  bitmap_set(swap_space, swap_pos, false); 
  lock_release(&swap_lock); 
}
