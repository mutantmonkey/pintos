#include "threads/synch.h"
#include <list.h>
#include "threads/palloc.h"

struct frame_table_entry
{
  struct thread* thread;
  struct list_elem frame_table_elem;
  void* frame_page;
  void* page;
};


struct list frame_table;
struct lock frame_table_lock;

void frame_table_init(void);
void *allocate_frame(enum palloc_flags);
void free_frame(void *);
