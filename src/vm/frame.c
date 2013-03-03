#include "threads/synch.h"
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include "threads/pte.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include <stdio.h>
#include "threads/palloc.h"
#include "vm/frame.h"

void remove_frame_entry(void *frame);
bool create_frame_entry(void* frame);

//Initialize the frame table
void 
frame_table_init(void)
{
  list_init(&frame_table);
  lock_init(&frame_table_lock);
}

//Allocates a frame. Wrapper for palloc_get_page
void*
allocate_frame(enum palloc_flags flags)
{
  void* frame = palloc_get_page(flags);
  if(frame != NULL)
    create_frame_entry(frame);
  else
    thread_exit();

  return frame;
}

//Frees a memory frame. Wrapper for palloc_free_page
void 
free_frame(void* frame)
{
  remove_frame_entry(frame);
  palloc_free_page(frame);  
}

//Creates a frame entry and adds it to the frame table list
bool create_frame_entry(void* frame)
{
  //struct frame_table_entry* entry = (struct frame_table_entry*)malloc(sizeof(struct frame_table_entry));
  struct frame_table_entry* entry;
  entry = calloc(1,sizeof(entry));
  if(entry == NULL)
    return false;
  
  entry->thread = thread_current();
  entry->frame_page = frame;
  lock_acquire(&frame_table_lock);
  list_push_back(&frame_table, &entry->frame_table_elem); 
  lock_release(&frame_table_lock);
  return true;
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
