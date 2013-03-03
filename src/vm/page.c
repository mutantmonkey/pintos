#include "vm/page.h"
#include <hash.h>
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "filesys/file.h"
#include <stdio.h>
#include <string.h>
#include "userprog/pagedir.h"

bool insert_entry_to_table(struct hash* table, struct sup_page_table_entry* entry);


bool insert_entry_to_table(struct hash* table, struct sup_page_table_entry* entry)
{
  if(hash_insert(table, &entry->elem) == NULL)
    return true;
  else
    return false;
}

bool create_sup_page_entry(struct file* file, off_t offset, uint8_t* page, uint32_t read, uint32_t zero, bool writable)
{
  struct sup_page_table_entry* entry = (struct sup_page_table_entry*) malloc(sizeof(struct sup_page_table_entry));

  entry->offset = offset;
  entry->f = file;
  entry->addr = page;
  entry->readbytes = read;
  entry->zerobytes = zero;
  entry->writable = writable;  

  if(insert_entry_to_table(&thread_current()->sup_page_table, entry))
    return true;
  else
    return false;
}

struct sup_page_table_entry* get_sup_page_entry(struct hash* table, void* page)
{
  struct sup_page_table_entry entry;
  entry.addr = page;
  struct hash_elem *e = hash_find(table, &entry.elem);
  if(e == NULL)
    return NULL;
  else
    return hash_entry(e, struct sup_page_table_entry, elem);
}

/**static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
**/
bool vm_allocate(struct sup_page_table_entry* entry)
{
  struct thread *cur = thread_current ();
  file_seek (entry->f, entry->offset);
  
  uint8_t *kpage = allocate_frame(PAL_USER);
  if (kpage == NULL)
  {
    return false;
  }
  
  if (file_read (entry->f, kpage, entry->readbytes) != (int) entry->readbytes)
  {
    free_frame(kpage);
    return false; 
  }

  memset (kpage + entry->readbytes, 0, entry->zerobytes);
  if (!pagedir_set_page(cur->pagedir, entry->addr, kpage, entry->writable)) 
  {
    free_frame(kpage);
    return false; 
  }
  //entry->frame = (void *)kpage;
  return true;
}

unsigned sup_table_hash (const struct hash_elem* hashelem, void* aux UNUSED)
{
  struct sup_page_table_entry* entry = hash_entry(hashelem, struct sup_page_table_entry, elem);

  return hash_int( (int) entry->addr); 
}

bool sup_table_less (const struct hash_elem* hashelem1, const struct hash_elem* hashelem2, void* aux UNUSED)
{
  struct sup_page_table_entry* entry1 = hash_entry(hashelem1, struct sup_page_table_entry, elem);

  struct sup_page_table_entry* entry2 = hash_entry(hashelem2, struct sup_page_table_entry, elem);

  return entry1->addr < entry2->addr; 
}
