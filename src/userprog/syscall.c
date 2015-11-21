#include "userprog/syscall.h"
#include "userprog/process.h"
#include "filesys/directory.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/init.h"
#include "filesys/off_t.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include <string.h>
#include "devices/shutdown.h"
#include "devices/input.h"
#include "lib/user/syscall.h"

char *read_string (uint32_t);
struct file *get_file_from_handle (int);
struct dir *get_dir_from_handle (int);
void handle_sys_exit (struct intr_frame *, int);
void handle_sys_create (struct intr_frame *);
void handle_sys_open (struct intr_frame *);
void handle_sys_write (struct intr_frame *);
void handle_sys_read (struct intr_frame *);
void handle_sys_close (struct intr_frame *);
void handle_sys_filesize (struct intr_frame *);
void handle_sys_exec (struct intr_frame *);
void handle_sys_wait (struct intr_frame *);
void handle_sys_seek (struct intr_frame *);
void handle_sys_tell (struct intr_frame *);
void handle_sys_remove (struct intr_frame *);
void handle_sys_mkdir (struct intr_frame *);
void handle_sys_chdir (struct intr_frame *);
void handle_sys_readdir (struct intr_frame *);
void handle_sys_isdir (struct intr_frame *);
void handle_sys_inumber (struct intr_frame *);
static void syscall_handler (struct intr_frame *);

struct file_descriptor
{
  int fd;
  struct file *file;
  struct list_elem elem;
};

struct dir_descriptor
{
  int dd;
  struct dir *dir;
  struct list_elem elem;
};

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (struct intr_frame *f, const uint8_t *uaddr)
{
  int result;

  if ((uint32_t) uaddr <= (uint32_t) PHYS_BASE)
    {
      asm ("movl $1f, %0; movzbl %1, %0; 1:"
	   : "=&a" (result) : "m" (*uaddr));
      return result;
    }
  handle_sys_exit (f, -1);
  return -1;
}
 
/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (struct intr_frame *f, uint8_t *udst, uint8_t byte)
{
  int error_code;
  if ((uint32_t) udst <= (uint32_t) PHYS_BASE)
    {
      asm ("movl $1f, %0; movb %b2, %1; 1:"
	   : "=&a" (error_code), "=m" (*udst) : "q" (byte));
      return error_code != -1;
    }
  handle_sys_exit (f, -1);
  return false;
}
 
char *read_string (uint32_t addr)
{
  char *file_name;
  int i;
  file_name = malloc (sizeof (char) * 100);
  for (i = 0; i < 100; i++)
    {
      file_name[i] = *(char *)(addr + i);
      if (file_name[i] == '\0')
      	break;
    }
  return file_name;
}
void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void
handle_sys_exit (struct intr_frame *f, int status)
{
  uint32_t *searcher, ARG0;
  int ofs, i;
  char *buffer, *exit_message, *brkt;
  size_t exit_message_size;
  struct list *list;
  struct list_elem *e;
  struct file_descriptor *cur;
  struct dir_descriptor *cur_dir;
  if (status == (int) NULL)
    {
      if (f->esp + (sizeof (uint32_t) * 2) > PHYS_BASE)
	handle_sys_exit (f, -1);
      ARG0 = *(uint32_t *) (f->esp + (sizeof (uint32_t)));
    }
  list = &(thread_current ()->file_descriptors);
  
  for (e = list_begin (list); e != list_end (list);)
    {
      cur = list_entry (e, struct file_descriptor, elem);
      e = list_next (e);
      file_close (cur->file);
      list_remove (&cur->elem);
      free (cur);
    }

  list = &(thread_current ()->dir_descriptors);
  
  for (e = list_begin (list); e != list_end (list);)
    {
      cur_dir = list_entry (e, struct dir_descriptor, elem);
      e = list_next (e);
      dir_close (cur_dir->dir);
      list_remove (&cur_dir->elem);
      free (cur_dir);
    }

  for (searcher = PHYS_BASE; *searcher != 0; --searcher) ;
  searcher++;
  for (ofs = 0; 
       ofs < 4 
	 && get_user (f, (void *)searcher + ofs) == 0; 
       ofs++) ;
  
  buffer = malloc (100 * sizeof (char));
  for (i = 0; i < 100; i++)
    {
      buffer[i] = get_user (f, (uint8_t *) 
			    (ofs + i + (uint32_t) searcher));
      if (buffer[i] == '\0')
	break;
    }
  free (buffer);
  buffer = strtok_r (thread_current ()->name, " ", &brkt);
  exit_message_size = sizeof (char) * (strlen(buffer) + 15);
  exit_message = malloc (exit_message_size);
  if (status == (int) NULL)
    status = ARG0;
  snprintf (exit_message, exit_message_size,
  	    "%s: exit(%d)\n", buffer, status);
  putbuf (exit_message, strlen (exit_message));

  free (exit_message);
  f->eax = status;
  thread_current ()->exit_status = status;
  thread_exit ();
}

void
handle_sys_write (struct intr_frame *f)
{
  uint32_t ARG0, ARG1, ARG2;
  uint8_t *buffer;
  int i;

  ARG0 = *(uint32_t *) (f->esp + (sizeof (uint32_t)));
  ARG1 = *(uint32_t *) (f->esp + (sizeof (uint32_t) * 2));
  ARG2 = *(uint32_t *) (f->esp + (sizeof (uint32_t) * 3));

  struct file *file = NULL;

  if (ARG1 + ARG2 > (uint32_t) PHYS_BASE)
    handle_sys_exit (f, -1);

  buffer = malloc (ARG2);
  for (i = 0; i < (int) ARG2; i++)
    {
      buffer[i] = get_user (f, (uint8_t *)(ARG1 + i));
    }

  if (ARG0 == 1)
    {
      putbuf ((char *) buffer, ARG2);
      f->eax = ARG2;
    }
  else if (ARG0 == 0)
    handle_sys_exit (f, -1);
  else
    {
      file = get_file_from_handle ((int) ARG0);
      if (file == NULL)
	f->eax = -1;
      else
	f->eax = file_write (file, buffer, ARG2);
    }

  free (buffer);
}
void
handle_sys_create (struct intr_frame *f)
{
  char *file_path, *name;
  struct inode *inode = NULL;
  uint32_t ARG1, ARG2;
  int i;

  ARG1 = *(uint32_t *) (f->esp + (sizeof (uint32_t)));
  ARG2 = *(uint32_t *) (f->esp + (sizeof (uint32_t)*2));
  
  if (ARG1 == (uint32_t) NULL)
    {
      handle_sys_exit (f, -1);
      return;
    }

  file_path = malloc (sizeof (char) * 100);
  for (i = 0; i < 100; i++)
    {
      file_path[i] = *(char *)(ARG1 + i);
      if (file_path[i] == '\0')
      	break;
    }
  if (strlen (file_path) == 0)
    f->eax = false;
  else
    {
      if (strlen (file_path) >= 511)
	f->eax = false;
      else
	{
	  name = get_filename (file_path);
	  if (is_dir (file_path)
	      || !dir_get (file_path)
	      || dir_lookup (dir_get (file_path), name, &inode))
	    {
	      f->eax = false;
	    }
	  else
	    {
	      f->eax = filesys_create (file_path, ARG2);
	    }
	}
    }
  free (file_path);
}

void 
handle_sys_open (struct intr_frame *f)
{
  struct file *file;
  uint32_t ARG0;
  char *path;
  struct file_descriptor * file_descriptor, *last;
  struct dir_descriptor * dir_descriptor, *last_dir;
  struct dir *dir;
  struct list *list;

  ARG0 = *(uint32_t *) (f->esp + (sizeof (uint32_t)));

  if (ARG0 == 0)
    {
      handle_sys_exit (f, -1);
      return;
    }
  
  path = read_string (ARG0);
  if (strlen (path) == 0)
    {
      f->eax = -1;
      return;
    }

  file = filesys_open (path);

  if (file != NULL)
    {
      file_descriptor = malloc (sizeof (struct file_descriptor));
      file_descriptor->file = file;
      if (list_empty (&thread_current ()->file_descriptors))
      	{
	  if (list_empty (&thread_current ()->dir_descriptors))
	    {
	      file_descriptor->fd = 2;
	      list_push_back (&thread_current ()->file_descriptors,
			      &file_descriptor->elem);
	      f->eax = 2;
	    }
	  else
	    {
	      last_dir = list_entry (list_rbegin (&thread_current ()->dir_descriptors)
				     , struct dir_descriptor, elem);
	      file_descriptor->fd = last_dir->dd + 1;
	      list_push_back (&thread_current ()->file_descriptors,
			      &file_descriptor->elem);
	      f->eax = file_descriptor->fd;
	    }
      	}
      else
      	{
	  if (list_empty (&thread_current ()->dir_descriptors))
	    {
	      last = list_entry (list_rbegin (&thread_current ()->file_descriptors)
				     , struct file_descriptor, elem);
	      file_descriptor->fd = last->fd + 1;
	      list_push_back (&thread_current ()->file_descriptors,
			      &file_descriptor->elem);
	      f->eax = file_descriptor->fd;

	    }
	  else
	    {
	      list = &(thread_current ()->file_descriptors);
	      last = list_entry (list_rbegin (list), struct file_descriptor, elem);
	      last_dir = list_entry (list_rbegin (&thread_current ()->dir_descriptors)
				     , struct dir_descriptor, elem);
	      file_descriptor->fd = (last->fd > last_dir->dd 
				     ? last->fd : last_dir->dd) + 1;
	      list_push_back (list,
			      &file_descriptor->elem);
	      f->eax = file_descriptor->fd;
	    }
      	}
    }
  else if (is_dir (path))
    {
      dir = dir_get (path);
      if (strlen (path) &&
	  (strcmp (path, "/") == 0 || is_dir (path)))
	{
	  dir_descriptor = malloc (sizeof (struct dir_descriptor));
	  dir_descriptor->dir = dir;
	  if (list_empty (&thread_current ()->dir_descriptors))
	    {
	      if (list_empty (&thread_current ()->file_descriptors))
		{
		  dir_descriptor->dd = 2;
		  list_push_back (&thread_current ()->dir_descriptors,
				  &dir_descriptor->elem);
		  f->eax = 2;
		}
	      else
		{
		  last = list_entry (list_rbegin (&thread_current ()->file_descriptors)
					 , struct file_descriptor, elem);
		  dir_descriptor->dd = last->fd + 1;
		  list_push_back (&thread_current ()->dir_descriptors,
				  &dir_descriptor->elem);
		  f->eax = dir_descriptor->dd;
		}
	    }
	  else
	    {
	      if (list_empty (&thread_current ()->file_descriptors))
		{
		  last_dir = list_entry (list_rbegin 
					 (&thread_current ()->dir_descriptors)
				     , struct dir_descriptor, elem);
		  dir_descriptor->dd = last_dir->dd + 1;
		  list_push_back (&thread_current ()->dir_descriptors,
				  &dir_descriptor->elem);
		  f->eax = dir_descriptor->dd;
		}
	      else
		{
		  list = &(thread_current ()->dir_descriptors);
		  last_dir = list_entry (list_rbegin (list), 
					 struct dir_descriptor, elem);
		  last = list_entry (list_rbegin (&thread_current ()->file_descriptors)
				     , struct file_descriptor, elem);
		  dir_descriptor->dd = (last->fd > last_dir->dd 
					? last->fd : last_dir->dd) + 1;
		  list_push_back (list,
				  &dir_descriptor->elem);
		  f->eax = dir_descriptor->dd;
		}
	    }
	  
	}
      else
	f->eax = -1;

    }
  else
    f->eax = -1;

  free (path);
}

void
handle_sys_close (struct intr_frame *f)
{
  uint32_t ARG0;
  ARG0 = *(uint32_t *) (f->esp + (sizeof (uint32_t)));

  if (ARG0 < 2)
    handle_sys_exit (f, -1);

  struct thread *cur = thread_current ();
  struct list *list = &cur->file_descriptors;
  struct dir_descriptor *dir_desc;
  struct file_descriptor *file_desc;
  struct list_elem *e;

  for (e = list_begin (list); e != list_end (list);
       e = list_next (e))
    {
      file_desc = list_entry (e, struct file_descriptor, elem);
      if (file_desc->fd == (int) ARG0)
	{
	  file_close (file_desc->file);
	  list_remove (&file_desc->elem);
	  f->eax = true;
	  return;
	}
    }

  list = &cur->dir_descriptors;

  for (e = list_begin (list); e != list_end (list);
       e = list_next (e))
    {
      dir_desc = list_entry (e, struct dir_descriptor, elem);
      if (dir_desc->dd == (int) ARG0)
	{
	  dir_close (dir_desc->dir);
	  list_remove (&dir_desc->elem);
	  f->eax = true;
	  return;
	}
    }
  handle_sys_exit (f, -1);
}

struct file *
get_file_from_handle (int fd)
{
  struct thread *cur = thread_current ();
  struct list *list = &cur->file_descriptors;
  struct file_descriptor *file_desc;
  struct list_elem *e;
  struct file *file = NULL;
  for (e = list_begin (list); e != list_end (list);
       e = list_next (e))
    {
      file_desc = list_entry (e, struct file_descriptor, elem);
      if (file_desc->fd == (int) fd)
	{
	  file = file_desc->file;
	  break;
	}
    }
  return file;
}

struct dir *
get_dir_from_handle (int dd)
{
  struct thread *cur = thread_current ();
  struct list *list = &cur->dir_descriptors;
  struct dir_descriptor *dir_descriptor;
  struct list_elem *e;
  struct file *dir = NULL;
  for (e = list_begin (list); e != list_end (list);
       e = list_next (e))
    {
      dir_descriptor = list_entry (e, struct file_descriptor, elem);
      if (dir_descriptor->dd == (int) dd)
	{
	  dir = dir_descriptor->dir;
	  break;
	}
    }
  return dir;
}

void
handle_sys_read (struct intr_frame *f)
{
  uint32_t fd, buffer, size, _size;
  fd = *(uint32_t *) (f->esp + (sizeof (uint32_t)));
  buffer = *(uint32_t *) (f->esp + (sizeof (uint32_t)) * 2);
  size = *(uint32_t *) (f->esp + (sizeof (uint32_t)) * 3);
  _size = size;
  int i;
  struct file *file = NULL;

  if (buffer + size > (uint32_t) PHYS_BASE)
    handle_sys_exit (f, -1);
  char *buffer_temp = malloc (size);

  file = get_file_from_handle (fd);
  if (file == NULL && fd != 0)
    handle_sys_exit (f, -1);

  if (fd == 0)
    {
      for (i = 0; i < (int) size; i++)
	put_user (f, (void *) buffer + i, input_getc ());
    }
  else
    _size = file_read (file, buffer_temp, size);
  
  for (i = 0; i < (int) size; i++)
    {
      put_user (f, (void *) buffer + i, buffer_temp[i]);
    }

  free (buffer_temp);
  f->eax = _size;
}

void
handle_sys_filesize (struct intr_frame *f)
{
  uint32_t fd;
  fd = *(uint32_t *) (f->esp + (sizeof (uint32_t)));

  struct thread *cur = thread_current ();
  struct list *list = &cur->file_descriptors;
  struct file_descriptor *file_desc;
  struct list_elem *e;
  struct file *file = NULL;
  for (e = list_begin (list); e != list_end (list);
       e = list_next (e))
    {
      file_desc = list_entry (e, struct file_descriptor, elem);
      if (file_desc->fd == (int) fd)
	{
	  file = file_desc->file;
	  f->eax = file_length (file);
	  return;
	}
    }

  f->eax = false;
 }
void
handle_sys_exec (struct intr_frame *f)
{
  uint32_t ARG0;
  ARG0 = *(uint32_t *) (f->esp + (sizeof (uint32_t)));
  char *cmd_line;
  struct list_elem *e;
  tid_t child_tid;
  int i;

  cmd_line = malloc (100);
  for (i = 0; i < 100; i++)
    {
      cmd_line[i] = get_user (f, (uint8_t *)(ARG0 + i));
      if (cmd_line[i] == '\0')
	break;
    }

  child_tid = process_execute (cmd_line);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      if (t->tid == child_tid)
	sema_down (&t->load);
    } 

  free (cmd_line);
  if (!load_success || child_tid == TID_ERROR)
    f->eax = -1;
  else
    f->eax = child_tid;

}

void 
handle_sys_wait (struct intr_frame *f)
{
  tid_t child_tid;
  child_tid = *(uint32_t *) (f->esp + (sizeof (uint32_t)));
  
  f->eax = process_wait (child_tid);
}

void 
handle_sys_seek (struct intr_frame *f)
{
  uint32_t handle, position;
  handle = *(uint32_t *) (f->esp + (sizeof (uint32_t)));
  position  = *(uint32_t *) (f->esp + (sizeof (uint32_t)) * 2);

  struct file *file = get_file_from_handle (handle);
  if (file == NULL)
    f->eax = -1;
  else
    file_seek (file, position);
}

void
handle_sys_tell (struct intr_frame *f)
{
  uint32_t handle;
  handle = *(uint32_t *) (f->esp + (sizeof (uint32_t)));

  struct file *file = get_file_from_handle (handle);
  if (file == NULL)
    f->eax = -1;
  else
    f->eax = file_tell (file);
}

void
handle_sys_remove (struct intr_frame *f)
{
  uint32_t fname_ptr;
  fname_ptr = *(uint32_t *) (f->esp + (sizeof (uint32_t)));
  struct dir *dir;
  char *path, *name;
  struct inode *inode;
  int i;

  path = malloc (100);
  for (i = 0; i < 100; i++)
    {
      path[i] = get_user (f, (uint8_t *)(fname_ptr + i));
      if (path[i] == '\0')
	break;
    }

  dir = dir_get (path);
  name = get_filename (path);
  if (name && dir_lookup (dir, name, &inode))
    f->eax = filesys_remove (path);
  else
    f->eax = dir_rmdir (path);

  dir_close (dir);
  free (path);
}

void
handle_sys_mkdir (struct intr_frame *f)
{
  uint32_t dir_name_ptr;
  dir_name_ptr = *(uint32_t *) (f->esp + (sizeof (uint32_t)));

  char *dir_name;
  int i;

  dir_name = malloc (NAME_MAX + 1);
  for (i = 0; i < 100; i++)
    {
      dir_name[i] = get_user (f, (uint8_t *) (dir_name_ptr + i));
      if (dir_name[i] == '\0')
	break;
    }
  if (dir_name[0] == '\0')
    f->eax = 0;
  else
    f->eax = dir_mkdir (dir_name);
  free (dir_name);
}

void
handle_sys_chdir (struct intr_frame *f)
{
  uint32_t dir_name_ptr;
  dir_name_ptr = *(uint32_t *) (f->esp + (sizeof (uint32_t)));

  char *dir_name;
  int i;

  dir_name = malloc (NAME_MAX + 1);
  for (i = 0; i < 100; i++)
    {
      dir_name[i] = get_user (f, (uint8_t *) (dir_name_ptr + i));
      if (dir_name[i] == '\0')
	break;
    }
  f->eax = dir_chdir (dir_name);
  free (dir_name);
}

void
handle_sys_readdir (struct intr_frame *f)
{
  uint32_t dd, buffer;
  struct dir *dir;
  dd = *(uint32_t *) (f->esp + (sizeof (uint32_t)));
  buffer = *(uint32_t *) (f->esp + (sizeof (uint32_t)) * 2);
  char buf[READDIR_MAX_LEN + 1];
  int i;

  dir = get_dir_from_handle (dd);
  f->eax = dir_readdir (dir, buf);
  for (i = 0; i < READDIR_MAX_LEN && buf[i] != '\0'; i++)
    {
      put_user (f, (void *) buffer + i, buf[i]);      
    }
  put_user (f, (void *) buffer + i, '\0');
}

void
handle_sys_isdir (struct intr_frame *f)
{
  uint32_t dd;
  dd = *(uint32_t *) (f->esp + (sizeof (uint32_t)));
  
  if (get_dir_from_handle (dd))
    f->eax = true;
  else if (get_file_from_handle (dd))
    f->eax = false;
  else
    f->eax = -1;
}

void
handle_sys_inumber (struct intr_frame *f)
{
  uint32_t fd;
  fd = *(uint32_t *) (f->esp + (sizeof (uint32_t)));

  struct file *file = NULL;
  struct dir *dir = NULL;

  file = get_file_from_handle (fd);
  if (file)
    {
      f->eax = inode_get_inumber (file_get_inode (file));
      return;
    }

  dir = get_dir_from_handle (fd);
  if (dir)
    {
      f->eax = inode_get_inumber (dir_get_inode (dir));
      return;
    }

  f->eax = 0;
}

static void
syscall_handler (struct intr_frame *f) 
{
  if ((uint32_t) f->esp > (uint32_t) PHYS_BASE || ((uint32_t) f->esp) < 0x08048000)
    handle_sys_exit (f, -1);

  switch ( *(uint32_t *) f->esp)
    {
    case SYS_HALT : 
      shutdown_power_off ();
      break;
    case SYS_EXIT : 
      handle_sys_exit (f, (int) NULL);
      break;
    case SYS_EXEC :
      handle_sys_exec (f);
      break;
    case SYS_WAIT :
      handle_sys_wait (f);
      break;
    case SYS_FILESIZE :
      handle_sys_filesize (f);
      break;
    case SYS_READ :
      handle_sys_read (f);
      break;
    case SYS_WRITE : 
      handle_sys_write (f);
      break;
    case SYS_CREATE :
      handle_sys_create (f);
      break;
    case SYS_OPEN :
      handle_sys_open (f);
      break;
    case SYS_CLOSE :
      handle_sys_close (f);
      break;
    case SYS_REMOVE :
      handle_sys_remove (f);
      break;
    case SYS_SEEK :
      handle_sys_seek (f);
      break;
    case SYS_TELL :
      handle_sys_tell (f);
      break;
    case SYS_MKDIR :
      handle_sys_mkdir (f);
      break;
    case SYS_CHDIR :
      handle_sys_chdir (f);
      break;
    case SYS_READDIR :
      handle_sys_readdir (f);
      break;
    case SYS_ISDIR :
      handle_sys_isdir (f);
      break;
    case SYS_INUMBER :
      handle_sys_inumber (f);
      break;
    default :
      handle_sys_exit (f, -1);      
      break;
    }
}
