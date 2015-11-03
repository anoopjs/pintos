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
  struct list_elem *e;
  if (!list_empty (&frame_table))
    {
      /* f = list_entry (list_rbegin (&frame_table), struct frame, elem); */
      /* list_remove (&f->elem); */
      for (e = list_rbegin (&frame_table);
	   e != list_begin (&frame_table);
	   e = list_prev (e))
	{
	  f = list_entry (e, struct frame, elem);
	  if (!f->pin)
	    {
	      list_remove (&f->elem);
	      break;
	    }
	}
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
      list_push_front (&frame_table, &upage->elem);
      return upage;
    }
  else
    {
      size_t page_idx;
      if (swap_size == 0)
	{
	  struct block *block;
	  block = block_get_role (BLOCK_SWAP);
	  swap_size = block_size (block) * 512;
	  swap_map = bitmap_create (swap_size / PGSIZE);
	}
      lock_acquire (&bitmap_lock);
      page_idx = bitmap_scan_and_flip (swap_map, 0, 1, false);
      lock_release (&bitmap_lock);
      if (page_idx != BITMAP_ERROR)
	{
	  int i;
	  struct hash_elem *e;
	  struct suppl_page *p;
	  struct thread *t;
	  struct suppl_page *s_page = malloc (sizeof (struct suppl_page));
	  struct block *block = block_get_role (BLOCK_SWAP);
	  lock_acquire (&lock);
	  struct frame *f = get_lru_frame ();
	  f->pin = true;
	  t = f->owner;
	  f->owner = thread_current ();
	  
	  s_page->addr = (void *) pg_round_down (f->uaddr);
	  lock_acquire (&filesys_lock);
	  for (i = 0; i < 8; i++)
	    {
	      block_write (block, page_idx * PGSIZE / 512 + i, f->kaddr + i * 512);
	    }
	  lock_release (&filesys_lock);

	  lock_acquire (&t->suppl_page_lock);
	  if (t && t->status != THREAD_DYING)
	    {
	      //	      printf ("%x %s --\n", t, t->name);
	      e = hash_find (&t->suppl_page_table,
			     &s_page->hash_elem);

	      if (e)
		{
		  p = hash_entry (e, struct suppl_page, hash_elem);
		  p->swapped = true;
		  p->swap_idx = page_idx;
		  if (t->pagedir)
		    {
		      pagedir_clear_page (t->pagedir, pg_round_down(f->uaddr));
		    }
		}
	      else
		printf("No Page hash found for %x! \n", s_page->addr);
	    }
	  free (s_page);
	  lock_release (&t->suppl_page_lock);
	  lock_release (&lock);
	  
	  memset (f->kaddr, 0, PGSIZE);
	  f->pin = false;
	  lock_acquire (&lock);
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
  lock_init (&bitmap_lock);
  lock_init (&block_lock);
  swap_size = 0;
}

void frame_free_page (void *page)
{
  struct frame *f;
  struct list_elem *e;

  lock_acquire (&lock);
  struct list *list = &frame_table;
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
