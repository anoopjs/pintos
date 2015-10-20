#include "vm/page.h"
#include <debug.h>
#include "threads/malloc.h"
#include "vm/frame.h"
#include "userprog/process.h"
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
  /* Get a page of memory. */
  uint8_t *kpage = frame_get_page (PAL_USER);
  if (kpage == NULL)
    return false;

  /* Load this page. */
  if (s->page_type == segment_page)
    {
      if (file_read_at (s->file, kpage, s->read_bytes, s->file_page) 
	  != (int) s->read_bytes)
	{
	  frame_free_page (kpage);
	  return false; 
	}
      memset (kpage + s->read_bytes, 0, s->zero_bytes);
    }

  if (s->page_type == mmap_page)
    {
      if (file_read_at (s->file, kpage, s->read_bytes, s->file_page) 
	  != (int) s->read_bytes)
	{
	  frame_free_page (kpage);
	  return false; 
	}
      memset (kpage + s->read_bytes, 0, s->zero_bytes);      
    }

  /* Add the page to the process's address space. */
  if (!install_page (s->addr, kpage, s->writable)) 
    {
      frame_free_page (kpage);
      return false; 
    }
}

