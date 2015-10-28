#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <hash.h>
#include "threads/synch.h"
#include "threads/palloc.h"

struct frame
{
  void *uaddr;
  void *kaddr;
  struct thread *owner;
  struct list_elem elem;
  bool pin;
};


uint32_t swap_size;
struct bitmap *swap_map;
struct lock lock;
struct list frame_table;
struct lock bitmap_lock;
struct lock block_lock;
struct frame * frame_get_page (enum palloc_flags);
void frame_free_page (void *);
void frame_init (void);

#endif


