#include <string.h>
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"

unsigned
cache_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct cache_block *p = hash_entry (p_, struct cache_block, hash_elem);
  return hash_bytes (&p->inode_sector, sizeof p->inode_sector);
}

bool
cache_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED)
{
  const struct cache_block *a = hash_entry (a_, struct cache_block, hash_elem);
  const struct cache_block *b = hash_entry (b_, struct cache_block, hash_elem);

  return a->inode_sector < b->inode_sector;
}

struct cache_block *
cache_lookup (block_sector_t sector)
{
  struct cache_block c;
  struct hash_elem *e;

  c.inode_sector = sector;
  e = hash_find (&buffer_cache_table, &c.hash_elem);
  return e != NULL ? hash_entry (e, struct cache_block, hash_elem) : NULL;
}


struct cache_block *
read_cache_block (block_sector_t sector_idx, void *buffer)
{
  struct list *list;
  struct hash *hash;
  struct list_elem *e;
  struct cache_block *cb = NULL;
  size_t size;
  list = &buffer_cache;
  hash = &buffer_cache_table;
  
  cb = cache_lookup (sector_idx);
  if (cb)
    {
      if (buffer)
	  memcpy (buffer, cb->data, BLOCK_SECTOR_SIZE);	
      sema_down (&sema_cache);
      list_remove (&cb->elem);
      list_push_front (list, &cb->elem);
      sema_up (&sema_cache);
      return cb;
    }
  else
    {
      sema_down (&sema_cache);
      size = list_size (list);
      sema_up (&sema_cache);
      if (size == 64)
	{
	  struct cache_block *victim_block = list_entry (list_rbegin (list),
							 struct cache_block,
							 elem);
	  block_write (fs_device, victim_block->inode_sector, victim_block->data);
	  block_read (fs_device, sector_idx, victim_block->data);
	  sema_down (&sema_cache);
	  list_remove (&victim_block->elem);
	  hash_delete (hash, &victim_block->hash_elem);
	  victim_block->inode_sector = sector_idx;
	  list_push_front (list, &victim_block->elem);
	  hash_insert (hash, &victim_block->hash_elem);
	  sema_up (&sema_cache);
	  if (buffer)
	    memcpy (buffer, victim_block->data, BLOCK_SECTOR_SIZE);
	  return victim_block;
	}
      else
	{
	  struct cache_block *c = malloc (sizeof (struct cache_block));
	  c->inode_sector = sector_idx;
	  block_read (fs_device, sector_idx, c->data);
	  sema_down (&sema_cache);
	  list_push_front (list, &c->elem);
	  hash_insert (hash, &c->hash_elem);
	  sema_up (&sema_cache);
	  if (buffer)
	    memcpy (buffer, c->data, BLOCK_SECTOR_SIZE);
	  return c;
	}
    }
  return NULL;
}


struct cache_block *
write_cache_block (block_sector_t sector_idx, void *buffer)
{
  struct list *list;
  struct hash *hash;
  struct list_elem *e;
  struct cache_block *cb = NULL;
  size_t size;
  list = &buffer_cache;
  hash = &buffer_cache_table;

  cb = cache_lookup (sector_idx);
  if (cb)
    {
      memcpy (cb->data, buffer, BLOCK_SECTOR_SIZE);
      sema_down (&sema_cache);
      list_remove (&cb->elem);
      list_push_front (list, &cb->elem);
      sema_up (&sema_cache);
      return cb;
    }
  else
    {
      sema_down (&sema_cache);
      size = list_size (list);
      sema_up (&sema_cache);
      if (size == 64)
	{
	  struct cache_block *victim_block = list_entry (list_rbegin (list),
							 struct cache_block,
							 elem);
	  block_write (fs_device, victim_block->inode_sector, victim_block->data);
	  memcpy (victim_block->data, buffer, BLOCK_SECTOR_SIZE);
	  sema_down (&sema_cache);
	  list_remove (&victim_block->elem);
	  hash_delete (hash, &victim_block->hash_elem);
	  victim_block->inode_sector = sector_idx;
	  list_push_front (list, &victim_block->elem);
	  hash_insert (hash, &victim_block->hash_elem);
	  sema_up (&sema_cache);
	  return victim_block;
	}
      else
	{
	  struct cache_block *c = malloc (sizeof (struct cache_block));
	  c->inode_sector = sector_idx;
	  memcpy (c->data, buffer, BLOCK_SECTOR_SIZE);
	  sema_down (&sema_cache);
	  list_push_front (list, &c->elem);
	  hash_insert (hash, &c->hash_elem);
	  sema_up (&sema_cache);
	  return c;
	}
    }
  return NULL;
}

void
write_back_cache_blocks ()
{
  struct list *list;
  struct list_elem *e;

  list = &buffer_cache;
  for (e = list_begin (list); e != list_end (list);)
    {
      struct cache_block *cb = list_entry (e, struct cache_block, elem);
      block_write (fs_device, cb->inode_sector, cb->data);
      sema_down (&sema_cache);
      e = list_remove (&cb->elem);
      hash_delete (&buffer_cache_table, &cb->hash_elem);
      sema_up (&sema_cache);
      free (cb);
    }
}
