#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include "userprog/pagedir.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);
static bool valid_user_buffer (const void *, size_t);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Checks that every page touched by a user buffer is mapped and below
   PHYS_BASE.  Invalid user memory terminates the current process. */
static bool
valid_user_buffer (const void *buffer, size_t size)
{
  const uint8_t *start = buffer;
  const uint8_t *end;
  uintptr_t page;
  uintptr_t last_page;

  if (size == 0)
    return true;

  end = start + size - 1;
  // Check that the entire buffer is below PHYS_BASE and mapped.
  if (start >= (uint8_t *) PHYS_BASE || end < start
      || end >= (uint8_t *) PHYS_BASE)
    thread_bad_exit ();

  page = (uintptr_t) pg_round_down (start);
  last_page = (uintptr_t) pg_round_down (end);

  // Check that every page touched by the buffer is mapped.
  for (; page <= last_page; page += PGSIZE)
    if (pagedir_get_page (thread_current ()->pagedir, (void *) page) == NULL)
      thread_bad_exit ();

  return true;
}

/* Copies SIZE bytes from a validated user buffer into kernel memory. */
void
copy_from_user (void *dst, const void *src, size_t size)
{
  valid_user_buffer (src, size);
  memcpy (dst, src, size);
}

/* Copies SIZE bytes from kernel memory into a validated user buffer. */
void
copy_to_user (void *dst, const void *src, size_t size)
{
  valid_user_buffer (dst, size);
  memcpy (dst, src, size);
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  printf ("system call!\n");
  thread_bad_exit ();
}
