#include "vm/page.h"
#include "vm/frame.h"
#include <debug.h>
#include <bitmap.h>
#include <string.h>
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include "devices/block.h"

unsigned
suppl_page_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct suppl_page *s = hash_entry (p_, struct suppl_page, hash_elem);
  return hash_bytes (&s->addr, sizeof s->addr);
}

bool
suppl_page_less (const struct hash_elem *a_, const struct hash_elem *b_,
	    void *aux UNUSED)
{
  const struct suppl_page *a = hash_entry (a_, struct suppl_page, hash_elem);
  const struct suppl_page *b = hash_entry (b_, struct suppl_page, hash_elem);
  
  return a->addr < b->addr;
}

bool
force_load_page (struct suppl_page *s)
{
  struct frame *frame = frame_get_page (PAL_USER);
  frame->uaddr = s->addr;
  if (!frame)
    {
      printf ("No frame !\n");
      return false;
    }

  uint8_t *kpage = frame->kaddr;
  if (kpage == NULL)
    {
      printf ("No frame !!\n");
      return false;
    }
  if (s->swapped)
    {
      block = block_get_role (BLOCK_SWAP);
      int i;
      for (i = 0; i < 8; i++)
	{
	  block_read (block, s->swap_idx * PGSIZE / 512 + i, kpage + i * 512);
	}
      lock_acquire (&bitmap_lock);
      bitmap_set_multiple (swap_map, s->swap_idx, 1, false);
      lock_release (&bitmap_lock);
    }

  else {
    if (s->page_type == segment_page)
      {
	if (s->read_bytes > 0)
	  {
	    //	    lock_acquire (&filesys_lock);
	    if (file_read_at (s->file, kpage, s->read_bytes, s->file_page) 
		!= (int) s->read_bytes)
	      {
		frame_free_page (kpage);
		printf ("File read went wrong !\n");
		return false; 
	      }
	    //	    lock_release (&filesys_lock);
	  }
	memset (kpage + s->read_bytes, 0, s->zero_bytes);
      }

    if (s->page_type == mmap_page)
      {
	if (s->read_bytes > 0)
	  {
	    //	    lock_acquire (&filesys_lock);
	    if (file_read_at (s->file, kpage, s->read_bytes, s->file_page) 
		!= (int) s->read_bytes)
	      {
		printf ("File read went wrong !!\n");
		frame_free_page (kpage);
		return false; 
	    }
	    //	    lock_release (&filesys_lock);
	  }
	memset (kpage + s->read_bytes, 0, s->zero_bytes);      
      }
  }

  if (!install_page (s->addr, kpage, s->writable)) 
    {
      printf ("Installing page went wrong !\n");
      frame_free_page (kpage);
      return false; 
    }
  return true;
}

