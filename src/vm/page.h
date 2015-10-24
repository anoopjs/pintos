#ifndef VM_PAGE_H
#define VM_PAGE_H
#include <hash.h>
#include "filesys/file.h"

enum lazy_page_type
  {
    stack_page, segment_page, mmap_page
  };

struct suppl_page
{
  struct hash_elem hash_elem;
  void *addr;
  struct file *file;
  off_t file_page;
  uint32_t read_bytes;
  uint32_t zero_bytes;
  bool writable;
  bool swapped;
  size_t swap_idx;
  enum lazy_page_type page_type;
};

struct mmap_region
{
  uint32_t ptr;
  int mmap_id;
  struct file *file;
  struct list_elem elem;
};

unsigned suppl_page_hash (const struct hash_elem *, void *);
bool suppl_page_less (const struct hash_elem *, const struct hash_elem*, void *);
bool force_load_page (struct suppl_page *);
#endif
