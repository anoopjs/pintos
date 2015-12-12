#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <list.h>
#include "devices/block.h"
#include "threads/synch.h"
#include <hash.h>
struct cache_block
{
  block_sector_t inode_sector;
  uint8_t data[BLOCK_SECTOR_SIZE];
  struct list_elem elem;
  struct hash_elem hash_elem;
};

unsigned
cache_hash (const struct hash_elem *, void *);

bool
cache_less (const struct hash_elem *, const struct hash_elem *,
	    void *);

struct list buffer_cache;
struct hash buffer_cache_table;
struct semaphore sema_cache;
struct cache_block * read_cache_block (block_sector_t, void *);
struct cache_block * write_cache_block (block_sector_t, void *);
void write_back_cache_blocks (void);
void cache_init (void);
#endif 
