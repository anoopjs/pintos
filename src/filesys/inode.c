#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"
#include "threads/thread.h"
/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

enum inode_type
  {
    FILE = 0,
    DIR  = 1
  };
/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    enum inode_type type;
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    block_sector_t i_block[14];         /* Pointers to blocks */
    uint32_t unused[111];               /* Not used. */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
    struct semaphore sema;
    struct semaphore dir_sema;
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    {
      if (pos < 12 * BLOCK_SECTOR_SIZE)
	{
	  return inode->data.i_block[pos / BLOCK_SECTOR_SIZE];
	}
      else if (pos < (12 * BLOCK_SECTOR_SIZE + BLOCK_SECTOR_SIZE * BLOCK_SECTOR_SIZE / 4))
	{
	  block_sector_t sector;
	  block_sector_t *direct_block = malloc (sizeof (struct inode_disk));
	  read_cache_block (inode->data.i_block[12], direct_block);
	  sector =  *(direct_block + (pos - 12 * BLOCK_SECTOR_SIZE) / BLOCK_SECTOR_SIZE);
	  free (direct_block);
	  return sector;
	}
      else if (pos < (12 * BLOCK_SECTOR_SIZE 
		      + BLOCK_SECTOR_SIZE * BLOCK_SECTOR_SIZE / 4
		      + BLOCK_SECTOR_SIZE * BLOCK_SECTOR_SIZE * BLOCK_SECTOR_SIZE / 16))
	{
	  block_sector_t sector;
	  block_sector_t *indirect_block = malloc (sizeof (struct inode_disk));
	  block_sector_t *direct_block   = malloc (sizeof (struct inode_disk));
	  read_cache_block (inode->data.i_block[13], indirect_block);
	  read_cache_block (*(indirect_block + (pos 
						- 12 * BLOCK_SECTOR_SIZE
						- BLOCK_SECTOR_SIZE * BLOCK_SECTOR_SIZE / 4)
			      / (BLOCK_SECTOR_SIZE * BLOCK_SECTOR_SIZE / 4)), direct_block);
	  sector =  *(direct_block + ((pos 
				   - 12 * BLOCK_SECTOR_SIZE
				   - BLOCK_SECTOR_SIZE * BLOCK_SECTOR_SIZE / 4)
				   % (BLOCK_SECTOR_SIZE * BLOCK_SECTOR_SIZE / 4) / BLOCK_SECTOR_SIZE));
	  free (direct_block);
	  free (indirect_block);
	  return sector;
	}
    }
  else
    {
      return -1;
    }
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;
struct semaphore sema;
/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
  sema_init (&sema, 1);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;
  int i;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->type = dir ? DIR : FILE;
      disk_inode->magic = INODE_MAGIC;
      static char zeros[BLOCK_SECTOR_SIZE];
      struct inode_disk * indirect_block = NULL;
      struct inode_disk * double_indirect_block = NULL;
      for (i = 0; i < (int) sectors; i++)
	{
	  if (i < 12)
	    {
	      if (!free_map_allocate (1, &disk_inode->i_block[i]))
		break;
	      else
		write_cache_block (disk_inode->i_block[i], zeros);
	    }
	  else if (i < (12 + BLOCK_SECTOR_SIZE / 4))
	    {
	      if (i == 12)
		indirect_block = malloc (sizeof (struct inode_disk));

	      if (!free_map_allocate (1, ((void *) indirect_block 
					  + (i - 12) * sizeof (block_sector_t))))
		break;
	      else
		write_cache_block (*(block_sector_t *) ((void *) indirect_block
				     + (i - 12) * sizeof (block_sector_t)), zeros);
	  
	      if (i == (int) sectors - 1 || i == (12 + BLOCK_SECTOR_SIZE / 4 - 1))
		{
		  if (!free_map_allocate (1, &disk_inode->i_block[12]))
		    break;
		  else
		    write_cache_block (disk_inode->i_block[12], indirect_block);
		  
		  free (indirect_block);
		}
	    }
	  else if (i < (12 + BLOCK_SECTOR_SIZE / 4 + (BLOCK_SECTOR_SIZE * BLOCK_SECTOR_SIZE / 16)))
	    {
	      block_sector_t *temp_idx;
	      if (i == (12 + BLOCK_SECTOR_SIZE / 4))
		  double_indirect_block = malloc (sizeof (struct inode_disk));

	      if ((i - (12 + BLOCK_SECTOR_SIZE / 4)) % (BLOCK_SECTOR_SIZE / 4) == 0)
		  indirect_block = malloc (sizeof (struct inode_disk));
	      
	      temp_idx = (void *) indirect_block + ((i - (12 + BLOCK_SECTOR_SIZE / 4))
						    % (BLOCK_SECTOR_SIZE / 4)) 
		* sizeof (block_sector_t);

	      if (!free_map_allocate (1, temp_idx))
		break;
	      else
		write_cache_block (*temp_idx, zeros);

	      if (i == (int) (sectors - 1)
		  || i == (12 + BLOCK_SECTOR_SIZE / 4 
			   + (BLOCK_SECTOR_SIZE * BLOCK_SECTOR_SIZE / 16)) - 1)
		{
		  temp_idx = (void *) double_indirect_block
		    + ((i - (12 + BLOCK_SECTOR_SIZE / 4)) / (BLOCK_SECTOR_SIZE / 4))
		    * sizeof (block_sector_t);

		  if (!free_map_allocate (1, temp_idx))
		    break;
		  else
		    write_cache_block (*temp_idx, indirect_block);
		  
		  free (indirect_block);

		  if (!free_map_allocate (1, &disk_inode->i_block[13]))
		    break;
		  else
		    write_cache_block (disk_inode->i_block[13], double_indirect_block);

		  free (double_indirect_block);
		}
	      else if ((i - (12 + BLOCK_SECTOR_SIZE / 4)) % (BLOCK_SECTOR_SIZE / 4) ==
		  (BLOCK_SECTOR_SIZE / 4 - 1))
		{
		  temp_idx = ((void *) double_indirect_block)
		    + (i - (12 + BLOCK_SECTOR_SIZE / 4)) / (BLOCK_SECTOR_SIZE / 4);
		  if (!free_map_allocate (1, temp_idx))
		    break;
		  else
		    write_cache_block (*temp_idx, indirect_block);

		  free (indirect_block);
		}

	    }
	}
      if (i == (int) sectors)
	success = true;

      write_cache_block (sector, disk_inode);
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  sema_down (&sema);
  list_push_front (&open_inodes, &inode->elem);
  sema_up (&sema);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  sema_init (&inode->sema, 1);
  sema_init (&inode->dir_sema, 1);
  read_cache_block (inode->sector, &inode->data);
  //  block_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

   /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
	  int i;
	  for (i = 0; i < inode->data.length; i += BLOCK_SECTOR_SIZE)
	    {
	      block_sector_t sector = byte_to_sector (inode, i);
	      free_map_release (sector, 1);
	    }
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */

      block_sector_t sector_idx = byte_to_sector (inode, offset);
      if (sector_idx == -1)
	return 0;
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;
      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
	  //block_read (fs_device, sector_idx, buffer + bytes_read);
	  sema_down (&inode->sema);
	  read_cache_block (sector_idx, buffer + bytes_read);
	  sema_up (&inode->sema);
        }
      else 
        {
	  sema_down (&inode->sema);
	  struct cache_block *cb = read_cache_block (sector_idx, NULL);
	  sema_up (&inode->sema);
	  memcpy (buffer + bytes_read, (void *) &cb->data + sector_ofs, chunk_size);
        }
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}

bool
grow_one_block (struct inode *inode)
{
  struct inode_disk *disk_inode = &inode->data;
  size_t sectors       = bytes_to_sectors (disk_inode->length);
  static char zeros[BLOCK_SECTOR_SIZE];

  //  sema_down (&inode->sema);
  if (sectors < 12)
    {
      if (free_map_allocate (1, &disk_inode->i_block[sectors]))
	write_cache_block (disk_inode->i_block[sectors], zeros);
      else
	{
	  //	  sema_up (&inode->sema);
	  return false;
	}
    }
  else if (sectors < (12 + BLOCK_SECTOR_SIZE / 4))
    {
      struct inode_disk *direct_block = malloc (sizeof (struct inode_disk));
      if (sectors == 12)
	{
	  if (free_map_allocate (1, (block_sector_t *) direct_block))
	    write_cache_block (*(block_sector_t *) direct_block, zeros);
	  else
	    {
	      //	      sema_up (&inode->sema);
	      return false;
	    }

	  if (free_map_allocate (1, &disk_inode->i_block[12]))
	    write_cache_block (disk_inode->i_block[12], direct_block);
	  else
	    {
	      //	      sema_up (&inode->sema);
	      return false;
	    }


	}
      else
	{
	  read_cache_block (disk_inode->i_block[12], direct_block);
	  
	  if (free_map_allocate (1, ((block_sector_t *) direct_block) + sectors - 12))
	    write_cache_block (*(((block_sector_t *) direct_block) + sectors - 12), zeros);
	  else
	    {
	      //	      sema_up (&inode->sema);
	      return false;
	    }


	  write_cache_block (disk_inode->i_block[12], direct_block);
	}
      free (direct_block);
    }
  else if (sectors < (12 + BLOCK_SECTOR_SIZE / 4 + (BLOCK_SECTOR_SIZE * BLOCK_SECTOR_SIZE / 16)))
    {
      struct inode_disk *indirect_block = malloc (sizeof (struct inode_disk));
      struct inode_disk *direct_block   = malloc (sizeof (struct inode_disk));

      if (sectors == (12 + BLOCK_SECTOR_SIZE / 4))
	{
	  if (free_map_allocate (1, (block_sector_t *) direct_block))
	    write_cache_block (*(block_sector_t *) direct_block, zeros);
	  else
	    {
	      //	      sema_up (&inode->sema);
	      return false;
	    }

	  if (free_map_allocate (1, (block_sector_t *) indirect_block))
	    write_cache_block (*(block_sector_t *) indirect_block, direct_block);
	  else
	    {
	      //	      sema_up (&inode->sema);
	      return false;
	    }

	  if (free_map_allocate (1, &disk_inode->i_block[13]))
	    write_cache_block (disk_inode->i_block[13], indirect_block);
	  else
	    {
	      //	      sema_up (&inode->sema);
	      return false;
	    }
	}
      else
	{
	  read_cache_block (disk_inode->i_block[13], indirect_block);
	  if ((sectors - 12 - BLOCK_SECTOR_SIZE / 4) % (BLOCK_SECTOR_SIZE / 4) == 0)
	    {
	      block_sector_t *temp_idx;

	      if (free_map_allocate (1, (block_sector_t *) direct_block))
		write_cache_block (*(block_sector_t *) direct_block, zeros);
	      else
		{
		  //		  sema_up (&inode->sema);
		  return false;
		}


	      temp_idx = (void *) indirect_block
		+ ((sectors - (12 + BLOCK_SECTOR_SIZE / 4)) / (BLOCK_SECTOR_SIZE / 4))
		* sizeof (block_sector_t);

	      if (free_map_allocate (1, temp_idx))
		write_cache_block (*temp_idx, direct_block);
	      else
		{
		  //		  sema_up (&inode->sema);
		  return false;
		}

	      write_cache_block (disk_inode->i_block[13], indirect_block);
	    }
	  else
	    {
	      block_sector_t *temp_idx, *temp_idx2;
	      temp_idx = (void *) indirect_block
		+ ((sectors - (12 + BLOCK_SECTOR_SIZE / 4)) / (BLOCK_SECTOR_SIZE / 4))
		* sizeof (block_sector_t);

	      read_cache_block (*temp_idx, direct_block);
	      temp_idx2 = (void *) direct_block
		+ ((sectors - (12 + BLOCK_SECTOR_SIZE / 4)) % (BLOCK_SECTOR_SIZE / 4))
		* sizeof (block_sector_t);

	      if (free_map_allocate (1, temp_idx2))
		write_cache_block (*temp_idx2, zeros);
	      else
		{
		  //		  sema_up (&inode->sema);
		  return false;
		}

	      write_cache_block (*temp_idx, direct_block);
	    }
	}
      free (direct_block);
      free (indirect_block);
    }
  //  sema_up (&inode->sema);
  return true;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;
  bool grow = false;

  if (inode->deny_write_cnt)
    return 0;

  if (ROUND_UP (inode->data.length, BLOCK_SECTOR_SIZE) < ROUND_UP (offset, BLOCK_SECTOR_SIZE))
    {
      int sectors =  DIV_ROUND_UP (offset, BLOCK_SECTOR_SIZE)
	- DIV_ROUND_UP (inode->data.length, BLOCK_SECTOR_SIZE);

      while (sectors-- > 0)
	{
	  if (!grow_one_block (inode))
	    return 0;
	  inode->data.length = ROUND_UP (inode->data.length + 1, BLOCK_SECTOR_SIZE);
	}

      grow = true;
    }

  while (size > 0) 
    {
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      //      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      //      int min_left = inode_left < sector_left ? inode_left : sector_left;
      int min_left = sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      sema_down (&inode->sema);
      if (sector_idx == -1)
	{
	  if (!grow_one_block (inode))
	    return 0;
	  inode->data.length = inode->data.length + chunk_size;
	  sector_idx = byte_to_sector (inode, offset);
	  grow = true;
	}
	  

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
	  //	  sema_down (&inode->sema);
	  write_cache_block (sector_idx, buffer + bytes_written);
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
	    read_cache_block (sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
	  //	  sema_down (&inode->sema);
	  write_cache_block (sector_idx, bounce);
        }
      sema_up (&inode->sema);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  free (bounce);
  if (grow)
    {
      inode->data.length = offset;
      write_cache_block (inode->sector, &inode->data);
    }

  
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

bool
inode_is_dir (struct inode *inode)
{
  return inode->data.type == DIR;
}

bool
inode_is_file (struct inode *inode)
{
  return inode->data.type == FILE;
}

bool
inode_removed (struct inode *inode)
{
  return inode->removed;
}

block_sector_t
inode_sector (struct inode *inode)
{
  return inode->sector;
}

struct semaphore *
get_dir_sema (struct inode *inode)
{
  return &inode->dir_sema;
}
