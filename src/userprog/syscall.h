#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <string.h>
#include <stdio.h>
#include <list.h>
void syscall_init (void);

struct child_status
{
  int tid;
  int status;
  struct list_elem elem;
};

#endif /* userprog/syscall.h */
