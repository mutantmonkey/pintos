#include "threads/synch.h"
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include "threads/pte.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include <stdio.h>
#include "threads/palloc.h"
#include "vm/vm.h"

//void remove_frame_entry(void *frame);
struct frame_table_entry* get_frame_entry(void *frame);
struct frame_table_entry* create_frame_entry(void* frame, struct sup_page_table_entry* entry);
void evict_frame(struct frame_table_entry* entry);
struct frame_table_entry* clock_evict(void);

//struct lock evict;
//struct lock allocate;
//Initialize the frame table
void 
frame_table_init(void)
{
  list_init(&frame_table);
  lock_init(&frame_table_lock);
  //lock_init(&evict);
  //lock_init(&allocate);
  swap_init();
}

//Allocates a frame. Wrapper for palloc_get_page
struct frame_table_entry*
allocate_frame(enum palloc_flags flags, struct sup_page_table_entry* entry)
{
  //lock_acquire(&allocate);
  void* frame = palloc_get_page(flags);
  
  struct frame_table_entry* frame_entry;
  if(frame != NULL)
    frame_entry = create_frame_entry(frame, entry);
  else
  {
    //PANIC("SWAP\n");
    evict_frame(clock_evict()); 
    void *frame = palloc_get_page(flags);
    if(frame == NULL)
      PANIC("Frame NULL \n");
    frame_entry = create_frame_entry(frame, entry);
    
    //thread_exit();
  }
  //lock_release(&allocate);
  return frame_entry;
}

//Frees a memory frame. Wrapper for palloc_free_page
void 
free_frame(void* frame)
{
  struct frame_table_entry* frame_entry = get_frame_entry(frame);
  if(frame_entry == NULL)
    return;
    //PANIC("NOT FOUND!\n");
  //remove_frame_entry(frame);
  //pagedir_clear_page(frame_entry->thread->pagedir, pg_round_down(frame_entry->page->addr));
  palloc_free_page(pg_round_down(frame)); 
  lock_acquire(&frame_table_lock);
  list_remove(&frame_entry->frame_table_elem);
  lock_release(&frame_table_lock);
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

//Removes a frame entry from the frame table list and frees it
void remove_frame_entry(void *frame)
{
  struct frame_table_entry* entry;
  struct list_elem* e;
  lock_acquire(&frame_table_lock);
  for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))
  {
    entry = list_entry(e, struct frame_table_entry, frame_table_elem);
    if(entry->frame_page == frame)
    {
      list_remove(e);
      free(entry);
      break;
    }
  }
  lock_release(&frame_table_lock);
}

struct frame_table_entry* get_frame_entry(void *frame)
{
  struct frame_table_entry* entry;
  struct frame_table_entry* res = NULL;
  struct list_elem* e;
  lock_acquire(&frame_table_lock);
  for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))
  {
    entry = list_entry(e, struct frame_table_entry, frame_table_elem);
    if(entry->frame_page == frame)
    {
      res = entry;
      break;
    }
  }
  lock_release(&frame_table_lock);
  return res;
}


struct frame_table_entry* clock_evict()
{
  struct frame_table_entry* evictee;
  
  lock_acquire(&frame_table_lock);
  struct list_elem* elem = list_end(&frame_table); 
  while(elem != list_head(&frame_table))
  {
    struct frame_table_entry* frame = list_entry(elem, struct frame_table_entry, frame_table_elem);

    if(frame->page != NULL)
    {
      evictee = frame;
      lock_release(&frame_table_lock);
      return evictee;
    }
    elem = list_prev(elem);
  }
  PANIC("No not nulls found\n");
  evictee = list_entry(list_end(&frame_table), struct frame_table_entry, frame_table_elem);
  lock_release(&frame_table_lock);
  return evictee;
}

void evict_frame(struct frame_table_entry* entry)
{
  //lock_acquire(&evict);
  size_t swap_pos = insert_into_swap(entry->frame_page);
  //insert_into_swap(entry->frame_page);
  entry->page->swap_table_index = swap_pos;
  entry->page->swapped = true;
  free_frame(entry->frame_page);
  //lock_release(&evict);
}


bool bring_from_swap(struct sup_page_table_entry* entry)
{
  //PANIC("BRING FROM SWAP\n");
  struct frame_table_entry* frame_entry = clock_evict();
  evict_frame(frame_entry);

  void* frame = palloc_get_page(PAL_USER);

  if(frame == NULL)
    return false;

  retrieve_from_swap(entry->swap_table_index,frame);
  if(!create_frame_entry(entry, frame))
    return false;;

  if(!pagedir_set_page(thread_current()->pagedir, entry->addr, frame, entry->writable))
    return false;

  return true;
} 
