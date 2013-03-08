#include <hash.h>
#include "vm/frame.h" 
#include "threads/vaddr.h"
#include "filesys/file.h"
enum page_type
{
  MMAPPED_FILE,
  FILE_TYPE,
  SWAP
};

struct sup_page_table_entry
{
  off_t offset;
  uint32_t readbytes;
  uint32_t zerobytes;
  struct hash_elem elem;
  enum page_type type;
  uint8_t* addr;
  struct file* f;
  struct frame_table_entry* frame;
  bool writable;
};

void free_sup_page_table(struct hash* table);

bool vm_allocate(struct sup_page_table_entry*);

struct sup_page_table_entry* get_sup_page_entry(struct hash*, void*);

bool create_sup_page_entry(struct file*, off_t, uint8_t*, uint32_t, uint32_t, bool);

void grow_stack(void* ptr);

unsigned sup_table_hash(const struct hash_elem*, void* aux UNUSED);

bool sup_table_less(const struct hash_elem*, const struct hash_elem*, void* aux UNUSED);
