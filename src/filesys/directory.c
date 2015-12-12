#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "filesys/cache.h"
#include "filesys/free-map.h"

/* A directory. */
struct dir 
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

/* A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
  return inode_create (sector, entry_cnt * sizeof (struct dir_entry), true);
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 2 * sizeof (struct dir_entry);
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  sema_down (get_dir_sema(dir->inode));
  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

 done:
  sema_up (get_dir_sema(dir->inode));
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  sema_down (get_dir_sema(dir->inode));
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  sema_up (get_dir_sema(dir->inode));
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        } 
    }
  return false;
}

struct dir 
* dir_get (char *full_path)
{
  struct dir *cur_dir = dir_open (inode_open (thread_current ()->current_dir));
  char *path = malloc (sizeof (char) * (strlen (full_path) + 1));
  strlcpy (path, full_path, strlen (full_path) + 1);
  char *name, *brkt;
  struct inode *inode = NULL;
  
  if (inode_removed(cur_dir->inode))
    {
      free (path);
      return NULL;
    }

  if (!strcmp (path, "/"))
    {
      free (path);
      dir_close (cur_dir);
      return dir_open_root ();
    }
  if (path[0] == '/')
    {
      dir_close (cur_dir);
      cur_dir = dir_open_root ();
    }

  name = strtok_r (path, "/", &brkt);
  while (name)
    {
      if (dir_lookup (cur_dir, name, &inode))
	{
	  if (inode_is_dir (inode))
	    {
	      dir_close (cur_dir);
	      cur_dir = dir_open (inode);
	      inode = NULL;
	    }
	  else
	    {
	      inode_close (inode);
	      break;
	    }
	}
      else
	  break;

      name = strtok_r (NULL, "/", &brkt);
    }

  free (path);
  return cur_dir;
}

bool is_dir (char *full_path)
{
  struct dir *cur_dir = dir_open (inode_open (thread_current ()->current_dir));
  char *path = malloc (sizeof (char) * strlen (full_path) + 1);
  strlcpy (path, full_path, strlen (full_path) + 1);
  char *name, *brkt;
  struct inode *inode = NULL;
  
  if (path[0] == '/')
    {
      dir_close (cur_dir);
      cur_dir = dir_open_root ();
    }

  for (name = strtok_r (path, "/", &brkt);
       name;
       name = strtok_r (NULL, "/", &brkt))
    {
      if (dir_lookup (cur_dir, name, &inode))
	{
	  if (inode_is_dir (inode) && !inode_removed (inode) && !inode_removed (cur_dir->inode))
	    {
	      dir_close (cur_dir);
	      cur_dir = dir_open (inode);
	    }
	  else
	    {
	      inode_close (inode);
	      dir_close (cur_dir);
	      free (path);
	      return false;
	    }
	}
      else
	{
	  dir_close (cur_dir);
	  free (path);
	  return false;
	}
    }

  dir_close (cur_dir);
  free (path);
  return true;
}

char * 
get_filename (char *full_path)
{
  char *path = malloc (sizeof (char) * (strlen (full_path) + 1));
  strlcpy (path, full_path, strlen (full_path) + 1);
  char *name, *brkt;
  char *prev = NULL;
  char *result = NULL;
  for (name = strtok_r (path, "/", &brkt);
       name;
       name = strtok_r (NULL, "/", &brkt))
    {
      prev = name;
    }

  if (prev)
    {
      result = malloc (sizeof (char) * (strlen (prev) + 1));
      strlcpy (result, prev, strlen (prev) + 1);
    }
  free (path);
  return result;
}

bool
dir_mkdir (char *path)
{
  struct dir *cur_dir;
  struct dir_entry de;
  static char zeros[BLOCK_SECTOR_SIZE];
  struct dir *dir;
  char *name;
  bool status;

  if (is_file (path))
    return false;
  name = get_filename (path);
  strlcpy (de.name, name, NAME_MAX);

  de.in_use = true;
  if (free_map_allocate (1, &de.inode_sector))
    write_cache_block (de.inode_sector, zeros);

  cur_dir = dir_get (path);
  dir_create (de.inode_sector, 0);
  dir = dir_open (inode_open (de.inode_sector));
  dir_add (dir, ".", de.inode_sector);
  dir_add (dir, "..", inode_sector (cur_dir->inode));
  
  status = (inode_write_at (cur_dir->inode
			    , (void *) &de, sizeof (de)
			    , inode_length (cur_dir->inode))
	    == sizeof (de));

  free (name);
  dir_close (dir);
  dir_close (cur_dir);
  return status;
}

bool
dir_chdir (char *path)
{
  struct dir *dir = dir_get (path);
  if (is_dir (path))
    {
      thread_current ()->current_dir = inode_sector (dir->inode);
      dir_close (dir);
      return true;
    }
  return false;
}

bool
dir_rmdir (char *path)
{
  struct dir *dir = dir_get (path);
  char *name = get_filename (path);
  char *parent;
  struct dir_entry e;
  size_t ofs;
  struct inode *inode = NULL;

  for (ofs = 2 * sizeof e; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
    if (e.in_use)
      return false;

  parent = malloc (strlen (path) + 3);
  strlcpy (parent, path, strlen (path) + 1);
  strlcat (parent, "..", strlen (path) + 3);
  
  if (dir_lookup (dir_get (parent), name, &inode)
      && inode_sector (inode) == ROOT_DIR_SECTOR)
    return false;

  dir_remove (dir_get (parent), name);

  dir_close (dir);
  free (name);
  free (parent);
  return true;
}
