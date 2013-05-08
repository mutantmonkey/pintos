#include "threads/synch.h"
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include "threads/pte.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include <stdio.h>
#include "threads/palloc.h"
#include "vm/vm.h"

struct list frame_table;
struct lock frame_table_lock;

//void remove_frame_entry(void *frame);
struct frame_table_entry* get_frame_entry(void *frame);
struct frame_table_entry* create_frame_entry(void* frame, struct sup_page_table_entry* entry);
void evict_frame(struct frame_table_entry* entry);
struct frame_table_entry* clock_evict(void);

//Initialize the frame table
void 
frame_table_init(void)
{
  list_init(&frame_table);
  lock_init(&frame_table_lock);
  swap_init();
}

//Allocates a frame. Wrapper for palloc_get_page
struct frame_table_entry*
allocate_frame(enum palloc_flags flags, struct sup_page_table_entry* entry)
{
  void* frame = palloc_get_page(flags);
  
  struct frame_table_entry* frame_entry;
  if(frame != NULL)
    frame_entry = create_frame_entry(frame, entry);
  else
  {
    struct frame_table_entry* evictee = clock_evict();
    if(pagedir_is_dirty(evictee->thread->pagedir, evictee->page))
      evict_frame(evictee);

    void *frame = palloc_get_page(flags);
    if(frame == NULL)
      PANIC("Frame NULL \n");
    frame_entry = create_frame_entry(frame, entry);
    
  }
  return frame_entry;
}

//Frees a memory frame. Wrapper for palloc_free_page
void 
free_frame(struct frame_table_entry* frame_entry)
{
  palloc_free_page(frame_entry->frame_page); 
  free(frame_entry);
}

//Creates a frame entry and adds it to the frame table list
struct frame_table_entry* create_frame_entry(void* frame, struct sup_page_table_entry* e)
{
  struct frame_table_entry* entry = (struct frame_table_entry*)malloc(sizeof(struct frame_table_entry));
  
  if(entry == NULL)
    return NULL;
 
  entry->thread = thread_current();
  entry->frame_page = frame;
  entry->page = e;
  
  lock_acquire(&frame_table_lock);
  list_push_back(&frame_table, &entry->frame_table_elem); 
  lock_release(&frame_table_lock);
  return entry;
}
//Evict the first entry that has not been accessed. If all have been 
//accessed, remove the first entry in the frame table.
struct frame_table_entry* clock_evict()
{
  struct frame_table_entry* evictee;
  lock_acquire(&frame_table_lock);
  struct list_elem* e;
  for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))
  {
    struct frame_table_entry* frame = list_entry(e, struct frame_table_entry, frame_table_elem);
    bool accessed = pagedir_is_accessed(frame->thread->pagedir, frame->page->addr);
    if(!accessed)
    {
      pagedir_clear_page(frame->thread->pagedir, frame->page->addr);
      evictee = frame;//list_entry(e, struct frame_table_entry, frame_table_elem);
      list_remove(e);
      lock_release(&frame_table_lock);
      return evictee;
    }
    if (e == list_end (&frame_table))
      e = list_begin (&frame_table);
  }
  evictee = list_entry(list_pop_front(&frame_table), struct frame_table_entry, frame_table_elem);
  pagedir_clear_page(evictee->thread->pagedir, evictee->page->addr);
  lock_release(&frame_table_lock);
  return evictee;
}
//Evict a frame by writting it to swap space and by 
//making note of this transition in the corresponding
//supplemental page table entry
void evict_frame(struct frame_table_entry* entry)
{
  size_t swap_pos = insert_into_swap(entry->frame_page);
  entry->page->swap_table_index = swap_pos;
  entry->page->swapped = true;
  free_frame(entry);
}

//Bring a frame back from swap space.
bool bring_from_swap(struct sup_page_table_entry* entry)
{
  struct frame_table_entry* frame_entry = clock_evict();
  evict_frame(frame_entry);

  void* frame = palloc_get_page(PAL_USER);

  if(frame == NULL)
    return false;

  retrieve_from_swap(entry->swap_table_index,frame);
  if(!create_frame_entry(frame, entry))
    return false;;
  if(!pagedir_set_page(thread_current()->pagedir, entry->addr, frame, entry->writable))
    return false;

  return true;
} 
