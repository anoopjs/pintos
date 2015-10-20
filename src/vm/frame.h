#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <hash.h>
#include "threads/palloc.h"

struct list frame_table;
void * frame_get_page (enum palloc_flags);
void frame_free_page (void *);
void frame_init (void);

#endif


