#include <hash.h>
#include "threads/vaddr.h"
#include "filesys/file.h"
#include "threads/synch.h"
#include <list.h>
#include "threads/palloc.h"
#include <bitmap.h>

struct list frame_table;
struct lock frame_table_lock;

/**enum page_type
{
  MMAPPED_FILE,
  FILE_TYPE,
  SWAP
};**/

struct frame_table_entry
{
  struct thread* thread;
  struct list_elem frame_table_elem;
  void* frame_page;
  struct sup_page_table_entry* page;
};


struct sup_page_table_entry
{
  off_t offset;
  uint32_t readbytes;
  uint32_t zerobytes;
  struct hash_elem elem;
  //enum page_type type;
  uint8_t* addr;
  struct file* f;
  struct frame_table_entry* frame;
  bool writable;
  size_t swap_table_index;
  bool swapped;
  bool loaded;
};

struct mmap_table_entry
{
  off_t offset;
  uint32_t readbytes;
  uint32_t zerobytes;
  struct hash_elem elem;
  struct list_elem listelem;
  bool writable;
  uint8_t* addr;
  struct file* f;
  int mapid_t;
  void* page;
};

void free_sup_page_table(struct hash* table);
bool vm_allocate(struct sup_page_table_entry*);
struct sup_page_table_entry* get_sup_page_entry(struct hash*, void*);
bool create_sup_page_entry(struct file*, off_t, uint8_t*, uint32_t, uint32_t, bool);
void grow_stack(void* ptr);
unsigned sup_table_hash(const struct hash_elem*, void* aux UNUSED);
bool sup_table_less(const struct hash_elem*, const struct hash_elem*, void* aux UNUSED);


void frame_table_init(void);
void remove_frame_entry(void *frame);
struct frame_table_entry *allocate_frame(enum palloc_flags, struct sup_page_table_entry* entry);
void free_frame(struct frame_table_entry *);
bool bring_from_swap(struct sup_page_table_entry* entry);


void swap_init(void);
size_t insert_into_swap(void* frame_page);
void clear_swap_entry(size_t swap_pos);
void retrieve_from_swap(size_t swap_pos, void* frame_page);


int insert_mmap_entry (struct file *, int, uint8_t *); 
bool create_mmap_entry(struct file*, off_t, uint8_t*, uint32_t, uint32_t, bool, int);
struct mmap_table_entry* get_mmap_entry(struct hash*, void*);
void mmap_exit(void);
bool mmap_write_back(struct mmap_table_entry*);
bool mmap_allocate(struct mmap_table_entry*);
unsigned mmap_table_hash (const struct hash_elem*, void* aux UNUSED);
bool mmap_table_less (const struct hash_elem*, const struct hash_elem*, void* aux UNUSED);
