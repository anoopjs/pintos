#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/init.h"

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

static void
syscall_handler (struct intr_frame *f) 
{
  uint32_t ARG0, ARG1, ARG2;
  uint32_t *searcher;
  int ofs;
  char *buffer, *exit_message;
  char c;
  int i;
  size_t exit_message_size;
  switch (*(uint32_t *) f->esp)
    {
    case SYS_HALT : shutdown_power_off ();
      break;
    case SYS_EXIT : 
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
      snprintf (exit_message, exit_message_size, 
		"%s: exit(%d)\n", buffer, ARG0);
      putbuf (exit_message, strlen (exit_message));
      f->eax = ARG0;
      thread_exit ();
      break;
    case SYS_WRITE : 
      ARG0 = *(uint32_t *) (f->esp + (sizeof (uint32_t)));
      ARG1 = *(uint32_t *) (f->esp + (sizeof (uint32_t) * 2));
      ARG2 = *(uint32_t *) (f->esp + (sizeof (uint32_t) * 3));

      buffer = malloc (sizeof (ARG2));
      for (i = 0; i < ARG2; i++)
	{
	  buffer[i] = get_user (ARG1 + i);
	}
      putbuf (buffer, ARG2);
      free (buffer);
      f->eax = ARG2;
      break;
    }
  //hex_dump (0, f->esp, PHYS_BASE - f->esp, 1);
  //thread_exit ();
}

