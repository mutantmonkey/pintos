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
#include <hash.h>

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
  entry->loaded = false;
  if(insert_entry_to_table(&thread_current()->sup_page_table, entry))
    return true;
  else
    return false;
}

//Insert a mmap entry into the mmap list/hash as demanded by the mmap syscall
int
insert_mmap_entry (struct file *file, int file_length, uint8_t *upage) 
{
  ASSERT (pg_ofs (upage) == 0);
  struct thread *thread = thread_current();
  int mapid_t = thread->latest_mapid_t;
  thread->latest_mapid_t++;
  int remaining_length = file_length; 
  int offset = 0;
   
  while(remaining_length > 0)
  {
    size_t page_read_bytes;
    size_t page_zero_bytes;
    if(remaining_length > PGSIZE)
    {
      page_read_bytes = PGSIZE;
      page_zero_bytes = 0;
      remaining_length -= PGSIZE;
    }
    else
    {
      page_read_bytes = remaining_length;
      page_zero_bytes = PGSIZE - page_read_bytes;
      remaining_length = 0;
    }
    
    if(create_mmap_entry(file, offset, upage, page_read_bytes, page_zero_bytes, true, mapid_t) == false)
      return -1;
    offset += page_read_bytes;
    upage += PGSIZE;
  }
  return mapid_t;
}




//Create a memory-mapped file entry. Based off of the create supplemental page entry
bool create_mmap_entry(struct file* file, off_t offset, uint8_t* page, uint32_t read, uint32_t zero, bool writable, int mapid)
{
  struct mmap_table_entry* entry = (struct mmap_table_entry*) malloc(sizeof(struct mmap_table_entry));

  entry->offset = offset;
  entry->f = file;
  entry->addr = page;
  entry->readbytes = read;
  entry->zerobytes = zero;
  entry->writable = writable;
  entry->mapid_t = mapid;
  lock_acquire(&thread_current()->mmap_lock);
  void* result = hash_insert(&thread_current()->mmap_table, &entry->elem);
  if(result == NULL)
    list_push_back(&thread_current()->mmap_list, &entry->listelem);
  lock_release(&thread_current()->mmap_lock);
  if(result == NULL)
    return true;
  else
    return false;
}

//Free the supplemental hash table
void free_sup_page_table(struct hash* table)
{
  hash_destroy(table, free_sup_page_entry);
}
//Free a suplemental page entry. Made sure to clear the pagedir's 
//page for the entry as soon as the entry is removed.
void free_sup_page_entry(struct hash_elem *e, void* aux UNUSED)
{
  struct sup_page_table_entry* entry = hash_entry(e, struct sup_page_table_entry, elem);
  if(entry->frame != NULL && entry->swapped != true)
  {
    lock_acquire(&frame_table_lock);
    list_remove(&entry->frame->frame_table_elem);
    pagedir_clear_page(thread_current()->pagedir, entry->addr);
    lock_release(&frame_table_lock);
    free_frame(entry->frame);
  }
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
//Find a memory-mapped page, from the mapped address, used to find the 
//mapped page in the hash.
struct mmap_table_entry* get_mmap_entry(struct hash* table, void* page)
{
  struct mmap_table_entry entry;
  entry.addr = page;
  lock_acquire(&thread_current()->mmap_lock);
  struct hash_elem *e = hash_find(table, &entry.elem);
  
  if(e == NULL)
  {
    lock_release(&thread_current()->mmap_lock);
    return NULL;
  }
  struct mmap_table_entry* result = hash_entry(e, struct mmap_table_entry, elem);
  lock_release(&thread_current()->mmap_lock);
  return result;
}
//Allocate memory for a memory-mapped address
bool mmap_allocate(struct mmap_table_entry* entry)
{ 
  file_seek (entry->f, entry->offset);
  
  void* page = palloc_get_page(PAL_USER);
  if (page == NULL)
  {
    PANIC("MMAP ALLOCATE FAILED\n");
  }
  
  entry->page = page; 
  if (file_read (entry->f, page, entry->readbytes) != (int) entry->readbytes)
  {
    PANIC("FAIL\n");
    return false; 
  }
  memset (page + entry->readbytes, 0, entry->zerobytes);
  if (!pagedir_set_page(thread_current()->pagedir, entry->addr, page, entry->writable)) 
  {
    PANIC("FAIL\n");
    return false; 
  }
  return true;
}
//Write back changes made to a memory-mapped file
bool mmap_write_back(struct mmap_table_entry* entry)
{
  file_seek (entry->f, entry->offset);
  if (entry->page == NULL)
  {
    return false;
  }
  //If the page isn't dirty, don't waste time writing back the 
  //same data back to the file
  if(!pagedir_is_dirty(thread_current()->pagedir, entry->addr))
  { 
    pagedir_clear_page(thread_current()->pagedir, entry->addr);
    return true;
  }
  if (file_write (entry->f, entry->page, entry->readbytes) != (int) entry->readbytes)
  {
    PANIC("FAIL\n");
    return false; 
  }
  pagedir_clear_page(thread_current()->pagedir, entry->addr);
  return true;

}
//Tear down the mmap system by removing all of the mmapped files from the 
//mmap list and hash and by writing back the changes, if any occurred.
void mmap_exit()
{
  struct thread* thread = thread_current();
  lock_acquire(&thread->mmap_lock);
  struct mmap_table_entry* entry;
  struct list_elem* e;
  for (e = list_begin(&thread->mmap_list); e != list_end(&thread->mmap_list); e = list_remove(e))
  {
    entry = list_entry(e, struct mmap_table_entry, listelem);
    mmap_write_back(entry);
    hash_delete(&thread->mmap_table, &entry->elem);
  }

  lock_release(&thread->mmap_lock); 
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
  //Allocate a physical page, evicting if necessary
  struct frame_table_entry* frame = allocate_frame(PAL_USER, entry);
  
  entry->frame = frame;
  uint8_t *page = frame->frame_page;
  if (page == NULL)
  {
    return false;
  }
  //Read the file's contents into the page
  if (file_read (entry->f, page, entry->readbytes) != (int) entry->readbytes)
  {
    PANIC("FAIL\n");
    return false; 
  }
  //Set any remaining bits to 0 and set the page in the pagedir
  memset (page + entry->readbytes, 0, entry->zerobytes);
  if (!pagedir_set_page(thread_current()->pagedir, entry->addr, page, entry->writable)) 
  {
    PANIC("FAIL\n");
    return false; 
  }
  return true;
}
//Grow the stack by getting a physical page to map to the given address
void grow_stack(void* ptr)
{
  uint8_t* page = palloc_get_page(PAL_USER | PAL_ZERO); 
  if (page != NULL)
  {
    if(!pagedir_set_page(thread_current()->pagedir, ptr, page, true))
    {
      PANIC("FAIL\n");
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

//Hash function used to add elements to the hash table
unsigned mmap_table_hash (const struct hash_elem* hashelem, void* aux UNUSED)
{
  struct mmap_table_entry* entry = hash_entry(hashelem, struct mmap_table_entry, elem);

  return hash_int((int) entry->addr); 
}
//Hash comparison function
bool mmap_table_less (const struct hash_elem* hashelem1, const struct hash_elem* hashelem2, void* aux UNUSED)
{
  struct mmap_table_entry* entry1 = hash_entry(hashelem1, struct mmap_table_entry, elem);

  struct mmap_table_entry* entry2 = hash_entry(hashelem2, struct mmap_table_entry, elem);

  return entry1->addr < entry2->addr; 
}

