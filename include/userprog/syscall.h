#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
void halt(void);
void exit(int status);
int fork (const char *thread_name);
#endif /* userprog/syscall.h */
