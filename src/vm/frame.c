#include "vm/frame.h"
#include <stdio.h>
#include <debug.h>
#include "threads/malloc.h"
#include "threads/thread.h"
struct frame
{
  void *addr;
  struct thread *owner;
  struct list_elem elem;
};

void * frame_get_page (enum palloc_flags flags)
{
  void *page = palloc_get_page (flags | PAL_USER);
  if (page != NULL)
    {
      struct frame *upage = malloc (sizeof (struct frame));
      upage->addr = page;
      upage->owner = thread_current ();
      list_push_front (&frame_table, &upage->elem);
      return page;
    }
  return NULL;
}

void frame_init ()
{
  list_init (&frame_table);
}

void frame_free_page (void *page)
{
  struct list *list = &frame_table;
  struct frame *f;
  struct list_elem *e;

  for (e = list_begin (list); e != list_end (list);
       e = list_next (e))
    {
      f = list_entry (e, struct frame, elem);
      if (f->addr == page)
	{
	  list_remove (&f->elem);
	  free (f);
	  break;
	}
    }
}
