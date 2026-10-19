#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stddef.h>

void syscall_init (void);
void copy_from_user (void *dst, const void *src, size_t size);
void copy_to_user (void *dst, const void *src, size_t size);

#endif /* userprog/syscall.h */
