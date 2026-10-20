#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

/* Record for each open file descriptor owned by a user process. */
struct file_desc
  {
    int fd;
    struct file *file;
    struct list_elem elem;
  };

static struct lock filesys_lock;

static void syscall_handler (struct intr_frame *);
static bool valid_user_buffer (const void *, size_t);
static bool valid_user_string (const char *);
static struct file_desc *find_file_desc (int fd);

void
syscall_init (void) 
{
  lock_init (&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Acquires the global file system lock. */
void
acquire_filesys_lock (void)
{
  lock_acquire (&filesys_lock);
}

/* Releases the global file system lock if held by the current thread. */
void
release_filesys_lock (void)
{
  if (lock_held_by_current_thread (&filesys_lock))
    lock_release (&filesys_lock);
}

/* Closes and frees all open file descriptors for the current thread. */
void
syscall_exit_thread (void)
{
  struct thread *cur = thread_current ();

  if (!lock_held_by_current_thread (&filesys_lock))
    acquire_filesys_lock ();
  
  while (!list_empty (&cur->file_descriptors))
    {
      struct list_elem *e = list_pop_front (&cur->file_descriptors);
      struct file_desc *desc = list_entry (e, struct file_desc, elem);
      file_close (desc->file);
      free (desc);
    }
  release_filesys_lock ();
}

/* Finds a file descriptor entry in the current thread's list. */
static struct file_desc *
find_file_desc (int fd)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;

  for (e = list_begin (&cur->file_descriptors);
       e != list_end (&cur->file_descriptors);
       e = list_next (e))
    {
      struct file_desc *desc = list_entry (e, struct file_desc, elem);
      if (desc->fd == fd)
        return desc;
    }
  return NULL;
}

/* Checks that every page touched by a user buffer is mapped and below
   PHYS_BASE. Invalid user memory terminates the current process. */
static bool
valid_user_buffer (const void *buffer, size_t size)
{
  if (buffer == NULL || buffer >= PHYS_BASE)
    thread_bad_exit ();

  if (size == 0)
    return true;

  const uint8_t *start = buffer;
  const uint8_t *end = start + size - 1;

  if (end < start || end >= (const uint8_t *) PHYS_BASE)
    thread_bad_exit ();

  uintptr_t page = (uintptr_t) pg_round_down (start);
  uintptr_t last_page = (uintptr_t) pg_round_down (end);

  for (; page <= last_page; page += PGSIZE)
    if (pagedir_get_page (thread_current ()->pagedir, (const void *) page) == NULL)
      thread_bad_exit ();

  return true;
}

/* Checks that a user-provided null-terminated string lies entirely within
   mapped user address space below PHYS_BASE. */
static bool
valid_user_string (const char *str)
{
  if (str == NULL || (const void *) str >= PHYS_BASE)
    thread_bad_exit ();

  const char *p = str;
  while (true)
    {
      if ((const void *) p >= PHYS_BASE)
        thread_bad_exit ();
      if (pagedir_get_page (thread_current ()->pagedir, p) == NULL)
        thread_bad_exit ();
      if (*p == '\0')
        break;
      p++;
    }
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

/* System call handler functions. */

static void
halt (void)
{
  shutdown_power_off ();
}

static void
exit (int status)
{
  struct thread *cur = thread_current ();
  cur->exit_status = status;
  thread_exit ();
}

static pid_t
exec (const char *cmd_line)
{
  valid_user_string (cmd_line);
  return process_execute (cmd_line);
}

static int
wait (pid_t pid)
{
  return process_wait (pid);
}

static bool
create (const char *file, unsigned initial_size)
{
  valid_user_string (file);
  acquire_filesys_lock ();
  bool success = filesys_create (file, initial_size);
  release_filesys_lock ();
  return success;
}

static bool
remove (const char *file)
{
  valid_user_string (file);
  acquire_filesys_lock ();
  bool success = filesys_remove (file);
  release_filesys_lock ();
  return success;
}

static int
open (const char *file)
{
  valid_user_string (file);
  acquire_filesys_lock ();
  struct file *f = filesys_open (file);
  if (f == NULL)
    {
      release_filesys_lock ();
      return -1;
    }

  struct thread *cur = thread_current ();
  struct file_desc *desc = malloc (sizeof *desc);
  if (desc == NULL)
    {
      file_close (f);
      release_filesys_lock ();
      return -1;
    }

  desc->fd = cur->next_fd++;
  desc->file = f;
  list_push_back (&cur->file_descriptors, &desc->elem);
  release_filesys_lock ();
  return desc->fd;
}

static int
filesize (int fd)
{
  struct file_desc *desc = find_file_desc (fd);
  if (desc == NULL)
    return -1;

  acquire_filesys_lock ();
  int size = file_length (desc->file);
  release_filesys_lock ();
  return size;
}

static int
read (int fd, void *buffer, unsigned size)
{
  valid_user_buffer (buffer, size);

  if (size == 0)
    return 0;

  if (fd == STDIN_FILENO)
    {
      uint8_t *buf = buffer;
      unsigned i;
      for (i = 0; i < size; i++)
        buf[i] = input_getc ();
      return size;
    }

  if (fd == STDOUT_FILENO || fd < 0)
    return -1;

  struct file_desc *desc = find_file_desc (fd);
  if (desc == NULL)
    return -1;

  acquire_filesys_lock ();
  int bytes_read = file_read (desc->file, buffer, size);
  release_filesys_lock ();
  return bytes_read;
}

static int
write (int fd, const void *buffer, unsigned size)
{
  valid_user_buffer (buffer, size);

  if (size == 0)
    return 0;

  if (fd == STDOUT_FILENO)
    {
      putbuf (buffer, size);
      return size;
    }

  if (fd == STDIN_FILENO || fd < 0)
    return -1;

  struct file_desc *desc = find_file_desc (fd);
  if (desc == NULL)
    return -1;

  acquire_filesys_lock ();
  int bytes_written = file_write (desc->file, buffer, size);
  release_filesys_lock ();
  return bytes_written;
}

static void
seek (int fd, unsigned position)
{
  struct file_desc *desc = find_file_desc (fd);
  if (desc == NULL)
    return;

  acquire_filesys_lock ();
  file_seek (desc->file, position);
  release_filesys_lock ();
}

static unsigned
tell (int fd)
{
  struct file_desc *desc = find_file_desc (fd);
  if (desc == NULL)
    return 0;

  acquire_filesys_lock ();
  unsigned pos = file_tell (desc->file);
  release_filesys_lock ();
  return pos;
}

static void
close (int fd)
{
  struct file_desc *desc = find_file_desc (fd);
  if (desc == NULL)
    return;

  acquire_filesys_lock ();
  file_close (desc->file);
  list_remove (&desc->elem);
  free (desc);
  release_filesys_lock ();
}

static void
syscall_handler (struct intr_frame *f) 
{
  valid_user_buffer (f->esp, sizeof (int));
  int syscall_nr = *(int *) f->esp;

  switch (syscall_nr)
    {
    case SYS_HALT:
      halt ();
      break;

    case SYS_EXIT:
      valid_user_buffer (f->esp, sizeof (int) * 2);
      exit (((int *) f->esp)[1]);
      break;

    case SYS_EXEC:
      valid_user_buffer (f->esp, sizeof (int) * 2);
      f->eax = exec (((const char **) f->esp)[1]);
      break;

    case SYS_WAIT:
      valid_user_buffer (f->esp, sizeof (int) * 2);
      f->eax = wait (((pid_t *) f->esp)[1]);
      break;

    case SYS_CREATE:
      valid_user_buffer (f->esp, sizeof (int) * 3);
      f->eax = create (((const char **) f->esp)[1], ((unsigned *) f->esp)[2]);
      break;

    case SYS_REMOVE:
      valid_user_buffer (f->esp, sizeof (int) * 2);
      f->eax = remove (((const char **) f->esp)[1]);
      break;

    case SYS_OPEN:
      valid_user_buffer (f->esp, sizeof (int) * 2);
      f->eax = open (((const char **) f->esp)[1]);
      break;

    case SYS_FILESIZE:
      valid_user_buffer (f->esp, sizeof (int) * 2);
      f->eax = filesize (((int *) f->esp)[1]);
      break;

    case SYS_READ:
      valid_user_buffer (f->esp, sizeof (int) * 4);
      f->eax = read (((int *) f->esp)[1], ((void **) f->esp)[2], ((unsigned *) f->esp)[3]);
      break;

    case SYS_WRITE:
      valid_user_buffer (f->esp, sizeof (int) * 4);
      f->eax = write (((int *) f->esp)[1], ((const void **) f->esp)[2], ((unsigned *) f->esp)[3]);
      break;

    case SYS_SEEK:
      valid_user_buffer (f->esp, sizeof (int) * 3);
      seek (((int *) f->esp)[1], ((unsigned *) f->esp)[2]);
      break;

    case SYS_TELL:
      valid_user_buffer (f->esp, sizeof (int) * 2);
      f->eax = tell (((int *) f->esp)[1]);
      break;

    case SYS_CLOSE:
      valid_user_buffer (f->esp, sizeof (int) * 2);
      close (((int *) f->esp)[1]);
      break;

    default:
      thread_bad_exit ();
      break;
    }
}

