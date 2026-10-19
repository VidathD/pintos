#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stddef.h>

/* Process identifier. */
typedef int pid_t;
#define PID_ERROR ((pid_t) -1)

void syscall_init (void);
void copy_from_user (void *dst, const void *src, size_t size);
void copy_to_user (void *dst, const void *src, size_t size);
void acquire_filesys_lock (void);
void release_filesys_lock (void);
void syscall_exit_thread (void);

#endif /* userprog/syscall.h */
