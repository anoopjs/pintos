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

char *read_string (uint32_t);
struct file *get_file_from_handle (int);
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
static void syscall_handler (struct intr_frame *);

struct file_descriptor
{
  int fd;
  struct file *file;
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

  if (status == NULL)
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
	handle_sys_exit (f, -1);
      
      f->eax = file_write (file, buffer, ARG2);
    }

  free (buffer);
}
void
handle_sys_create (struct intr_frame *f)
{
  char *file_name;
  struct inode *inode = NULL;
  struct dir *d;
  uint32_t ARG1, ARG2;
  int i;

  ARG1 = *(uint32_t *) (f->esp + (sizeof (uint32_t)));
  ARG2 = *(uint32_t *) (f->esp + (sizeof (uint32_t)*2));

  if (ARG1 == (uint32_t) NULL)
    {
      handle_sys_exit (f, -1);
      return;
    }

  file_name = malloc (sizeof (char) * 100);
  for (i = 0; i < 100; i++)
    {
      file_name[i] = *(char *)(ARG1 + i);
      if (file_name[i] == '\0')
      	break;
    }
  if (strlen (file_name) == 0)
    f->eax = false;
  else
    {
      if (strlen (file_name) >= 511)
	f->eax = false;
      else
	{
	  d = dir_open_root ();
	  if (dir_lookup (d, file_name, &inode))
	    {
	      f->eax = false;
	    }
	  else
	    {
	      filesys_create (file_name, ARG2);
	    }
	  dir_close (d);
	}
    }
  free (file_name);
}

void 
handle_sys_open (struct intr_frame *f)
{
  struct file *file;
  uint32_t ARG0;
  char *file_name;
  struct file_descriptor * file_descriptor, *last;
  struct list *list;
  ARG0 = *(uint32_t *) (f->esp + (sizeof (uint32_t)));

  if (ARG0 == 0)
    {
      handle_sys_exit (f, -1);
      return;
    }
  file_name = read_string (ARG0);
  file = filesys_open (file_name);
  if (file != NULL)
    {
      file_descriptor = malloc (sizeof (struct file_descriptor));
      file_descriptor->file = file;
      if (list_empty (&thread_current ()->file_descriptors))
      	{
      	  file_descriptor->fd = 2;
      	  list_push_back (&thread_current ()->file_descriptors,
      			  &file_descriptor->elem);
      	  f->eax = 2;
      	}
      else
      	{
      	  list = &(thread_current ()->file_descriptors);
	  last = list_entry (list_rbegin (list), struct file_descriptor, elem);
	  file_descriptor->fd = last->fd + 1;
	  list_push_back (list,
      			  &file_descriptor->elem);
	  f->eax = file_descriptor->fd;
      	}
    }
  else
    f->eax = -1;
  free (file_name);
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

void
handle_sys_read (struct intr_frame *f)
{
  uint32_t fd, buffer, size;
  fd = *(uint32_t *) (f->esp + (sizeof (uint32_t)));
  buffer = *(uint32_t *) (f->esp + (sizeof (uint32_t)) * 2);
  size = *(uint32_t *) (f->esp + (sizeof (uint32_t)) * 3);
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
    file_read (file, buffer_temp, size);
  
  for (i = 0; i < (int) size; i++)
    {
      put_user (f, (void *) buffer + i, buffer_temp[i]);
    }

  free (buffer_temp);
  f->eax = size;
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

  char *file_name;
  int i;

  file_name = malloc (100);
  for (i = 0; i < 100; i++)
    {
      file_name[i] = get_user (f, (uint8_t *)(fname_ptr + i));
      if (file_name[i] == '\0')
	break;
    }
  f->eax = filesys_remove (file_name);
  free (file_name);
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
    default :
      handle_sys_exit (f, -1);      
      break;
    }
}
