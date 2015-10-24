#include "vm/frame.h"
#include <stdio.h>
#include <debug.h>
#include <bitmap.h>
#include <string.h>
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "devices/block.h"
#include "userprog/syscall.h"
struct frame *
get_lru_frame (void)
{
  struct frame *f = NULL;
  if (!list_empty (&frame_table))
    {
      f = list_entry (list_rbegin (&frame_table), struct frame, elem);
      list_remove (&f->elem);
    }
  return f;
}

struct frame *
frame_get_page (enum palloc_flags flags)
{
  void *page = palloc_get_page (flags | PAL_USER);
  if (page != NULL)
    {
      struct frame *upage = malloc (sizeof (struct frame));
      upage->kaddr = page;
      upage->owner = thread_current ();
      lock_acquire (&lock);
      list_push_front (&frame_table, &upage->elem);
      lock_release (&lock);
      return upage;
    }
  else
    {
      size_t page_idx;
      if (swap_size == 0)
	{
	  block = block_get_role (BLOCK_SWAP);
	  swap_size = block_size (block) * 512;
	  swap_map = bitmap_create (swap_size / PGSIZE);
	}
      lock_acquire (&lock);
      page_idx = bitmap_scan_and_flip (swap_map, 0, 1, false);
      lock_release (&lock);
      if (page_idx != BITMAP_ERROR)
	{
	  int i;
	  lock_acquire (&lock);
	  struct frame *f = get_lru_frame ();
	  lock_release (&lock);

	  lock_acquire (&filesys_lock);
	  for (i = 0; i < 8; i++)
	    {
	      block_write (block, page_idx * PGSIZE / 512 + i, f->kaddr + i * 512);
	    }
	  lock_release (&filesys_lock);
	  
	  struct hash_elem *e;
	  struct suppl_page *p;
	  struct suppl_page *s_page = malloc (sizeof (struct suppl_page));
	  s_page->addr = (void *) pg_round_down (f->uaddr);

	  e = hash_find (&f->owner->suppl_page_table,
			 &s_page->hash_elem);

	  if (e)
	    {
	      lock_acquire (&thread_current ()->suppl_page_lock);
	      p = hash_entry (e, struct suppl_page, hash_elem);
	      p->swapped = true;
	      p->swap_idx = page_idx;
	      lock_release (&thread_current ()->suppl_page_lock);
	    }
	  else
	    printf("No Page hash found for %x! \n", s_page->addr);
	  
	  if (f->owner->pagedir)
	    {
	      pagedir_clear_page (f->owner->pagedir, pg_round_down(f->uaddr));
	    }
	  
	  lock_acquire (&lock);
	  f->owner = thread_current ();
	  memset (f->kaddr, 0, PGSIZE);
	  list_push_front (&frame_table, &f->elem);
	  lock_release (&lock);
	  return f;
	}
      else
	{
	  printf ("BITMAP ERROR!!\n");
	  return NULL;
	}
    }
  return NULL;
}

void frame_init ()
{
  list_init (&frame_table);
  lock_init (&lock);
  lock_init (&block_lock);
  swap_size = 0;
}

void frame_free_page (void *page)
{
  struct list *list = &frame_table;
  struct frame *f;
  struct list_elem *e;

  lock_acquire (&lock);
  for (e = list_begin (list); e != list_end (list);
       e = list_next (e))
    {
      f = list_entry (e, struct frame, elem);
      if (f->kaddr == page)
	{
	  list_remove (&f->elem);
	  free (f);
	  break;
	}
    }
  palloc_free_page (page);
  lock_release (&lock);
}
