#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

int insert_mmap_entry (struct file *file, int file_length, uint8_t *upage); 
#endif /* userprog/process.h */
