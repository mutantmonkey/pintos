//#include "vm/page.h"
#include "vm/vm.h"
#include <hash.h>
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "filesys/file.h"
#include <stdio.h>
#include <string.h>
#include "userprog/pagedir.h"

bool insert_entry_to_table(struct hash* table, struct sup_page_table_entry* entry);

void free_sup_page_entry(struct hash_elem *e, void* aux UNUSED);


//Insert an entry into the given hash table
bool insert_entry_to_table(struct hash* table, struct sup_page_table_entry* entry)
{
  lock_acquire(&thread_current()->hash_lock);
  void* result = hash_insert(table, &entry->elem);
  lock_release(&thread_current()->hash_lock);
  if(result == NULL)
    return true;
  else
    return false;
}

//Create a supplemental page table entry and pass to it the given parameters. Then insert the entry into the thread's
//page table.
bool create_sup_page_entry(struct file* file, off_t offset, uint8_t* page, uint32_t read, uint32_t zero, bool writable)
{
  struct sup_page_table_entry* entry = (struct sup_page_table_entry*) malloc(sizeof(struct sup_page_table_entry));

  entry->offset = offset;
  entry->f = file;
  entry->addr = page;
  entry->readbytes = read;
  entry->zerobytes = zero;
  entry->writable = writable;  
  entry->frame = NULL;
  entry->swapped = false;
  if(insert_entry_to_table(&thread_current()->sup_page_table, entry))
    return true;
  else
    return false;
}

void free_sup_page_table(struct hash* table)
{
  lock_acquire(&thread_current()->hash_lock);

  hash_destroy(table, free_sup_page_entry);

  lock_release(&thread_current()->hash_lock);
}

void free_sup_page_entry(struct hash_elem *e, void* aux UNUSED)
{
  struct sup_page_table_entry* entry = hash_entry(e, struct sup_page_table_entry, elem);
  if(entry->frame != NULL && entry->swapped != true)
  {
    pagedir_clear_page(thread_current()->pagedir, entry->addr);
    free_frame(entry->frame->frame_page);
  }
    //free_frame(entry->frame->frame_page);
  free(entry);
}

//Get a supplemental page table entry from the page table using a pointer to the virtual memory address
//as the hash table index.
struct sup_page_table_entry* get_sup_page_entry(struct hash* table, void* page)
{
  struct sup_page_table_entry entry;
  entry.addr = page;
  lock_acquire(&thread_current()->hash_lock);
  struct hash_elem *e = hash_find(table, &entry.elem);
  
  if(e == NULL)
  {
    lock_release(&thread_current()->hash_lock);
    return NULL;
  }
  struct sup_page_table_entry* result = hash_entry(e, struct sup_page_table_entry, elem);
  lock_release(&thread_current()->hash_lock);
  return result;
}


//Allocates a physical frame to back the virtual memory address. Based on the old load_segment 
//function from process.c
bool vm_allocate(struct sup_page_table_entry* entry)
{
  if(entry->swapped == true)
  {
    return bring_from_swap(entry);
  }
  
  file_seek (entry->f, entry->offset);
  
  struct frame_table_entry* frame = allocate_frame(PAL_USER, entry);
  
  uint8_t *page = frame->frame_page;
  if (page == NULL)
  {
    return false;
  }
  
  if (file_read (entry->f, page, entry->readbytes) != (int) entry->readbytes)
  {
    PANIC("FAIL\n");
    free_frame(page);
    return false; 
  }
  memset (page + entry->readbytes, 0, entry->zerobytes);
  if (!pagedir_set_page(thread_current()->pagedir, entry->addr, page, entry->writable)) 
  {
    PANIC("FAIL\n");
    free_frame(page);
    return false; 
  }
  entry->frame = frame;
  return true;
}

void grow_stack(void* ptr)
{
  uint8_t* page = palloc_get_page(PAL_USER | PAL_ZERO); 
  //uint8_t *page = allocate_frame(PAL_USER | PAL_ZERO, NULL)->frame_page;
  if (page != NULL)
  {
    if(!pagedir_set_page(thread_current()->pagedir, ptr, page, true))
    {
      PANIC("FAIL\n");
      free_frame(page);
    }
  }
}

//Hash function used to add elements to the hash table
unsigned sup_table_hash (const struct hash_elem* hashelem, void* aux UNUSED)
{
  struct sup_page_table_entry* entry = hash_entry(hashelem, struct sup_page_table_entry, elem);

  return hash_int( (int) entry->addr); 
}
//Hash comparison function
bool sup_table_less (const struct hash_elem* hashelem1, const struct hash_elem* hashelem2, void* aux UNUSED)
{
  struct sup_page_table_entry* entry1 = hash_entry(hashelem1, struct sup_page_table_entry, elem);

  struct sup_page_table_entry* entry2 = hash_entry(hashelem2, struct sup_page_table_entry, elem);

  return entry1->addr < entry2->addr; 
}
