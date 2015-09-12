#include "userprog/syscall.h"
#include "filesys/directory.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/init.h"
#include "filesys/off_t.h"
#include "filesys/inode.h"


static void syscall_handler (struct intr_frame *);

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}
 
/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void handle_sys_exit (struct intr_frame *f, int status)
{
  uint32_t *searcher, ARG0, ARG1, ARG2;
  int ofs, i;
  char *buffer, *exit_message;
  size_t exit_message_size;

  ARG0 = *(uint32_t *) (f->esp + (sizeof (uint32_t)));

  for (searcher = PHYS_BASE; *searcher != 0; --searcher) ;
  searcher++;
  for (ofs = 0; 
       ofs < 4 
	 && get_user ((uint32_t *) ((uint32_t) searcher + ofs)) == 0; 
       ofs++) ;

  buffer = malloc (100 * sizeof (char));
  for (i = 0; i < 100; i++)
    {
      buffer[i] = get_user ((uint32_t *) 
			    (ofs + i + (uint32_t) searcher));
      if (buffer[i] == '\0')
	break;
    }
  exit_message_size = sizeof (char) * (strlen(buffer) + 15);
  exit_message = malloc (exit_message_size);
  if (status == NULL)
    status = ARG0;
  snprintf (exit_message, exit_message_size, 
	    "%s: exit(%d)\n", buffer, status);
  putbuf (exit_message, strlen (exit_message));
  f->eax = status;
  thread_exit ();
}

void handle_sys_write (struct intr_frame *f)
{
  uint32_t ARG0, ARG1, ARG2;
  char *buffer, c;
  int i;
  ARG0 = *(uint32_t *) (f->esp + (sizeof (uint32_t)));
  ARG1 = *(uint32_t *) (f->esp + (sizeof (uint32_t) * 2));
  ARG2 = *(uint32_t *) (f->esp + (sizeof (uint32_t) * 3));

  buffer = malloc (sizeof (ARG2));
  for (i = 0; i < ARG2; i++)
    {
      buffer[i] = get_user (ARG1 + i);
    }

  if (ARG0 == 1)
    putbuf (buffer, ARG2);

  free (buffer);
  f->eax = ARG2;
}
void handle_sys_create (struct intr_frame *f)
{
  char *file_name;
  struct inode *inode = NULL;
  struct dir *d;
  uint32_t ARG1, ARG2;
  int i;
  ARG1 = *(uint32_t *) (f->esp + (sizeof (uint32_t)));
  ARG2 = *(uint32_t *) (f->esp + (sizeof (uint32_t)*2));

  if (ARG1 == NULL)
    {
      handle_sys_exit (f, -1);
      return;
    }
  file_name = malloc (sizeof (char) * 100);
  for (i = 0; i < 100; i++)
    {
      file_name[i] = *(char *)(ARG1 + 4*i);
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
	  /* if (dir_lookup (dir_open_root (), file_name, &inode)) */
	  /*   { */

	  /*   } */
	  /* else */
	  /*   { */
	      filesys_create (file_name, ARG2);
	      //	    }
	}
    }
}
static void
syscall_handler (struct intr_frame *f) 
{
  switch (*(uint32_t *) f->esp)
    {
    case SYS_HALT : 
      shutdown_power_off ();
      break;
    case SYS_EXIT : 
      handle_sys_exit (f, NULL);
      break;
    case SYS_WRITE : 
      handle_sys_write (f);
      break;
    case SYS_CREATE :
      //hex_dump (0, f->esp, PHYS_BASE - f->esp, 1);
      handle_sys_create (f);
      break;
    default :
      thread_exit ();
      break;
    }

}

