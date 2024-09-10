#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "filesys/filesys.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

bool fd_pool[PGSIZE/sizeof(bool)];

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	// 1. 시스템 콜 번호(include/lib/syscall-nr.h에 목록이 있음)를 받아온다. -> %rax에 있다.(f로 콜러의 레지스터에 접근 가능)
	// 2. 시스템 콜 인자들을 받아온다. -> 인자는 %rdi, %rsi, %rdx, %r10, %r8, %r9 순서로 전달
	// 3. 알맞은 액션을 취한다. (반환은 %rax에)
	int systemcall_num = f->R.rax;

	switch(systemcall_num) {
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
		// case SYS_FORK:
		// 	f->R.rax = fork(f->R.rdi);
		// 	break;
		case SYS_EXEC:
			f->R.rax = exec(f->R.rdi);
			if(f->R.rax == -1) {
				exit(-1);
			}
		// case SYS_WAIT:
		// 	f->R.rax = fork(f->R.rdi);
		// 	break;
		case SYS_CREATE:
			if(create(f->R.rdi, f->R.rsi))
				f->R.rax = true;
			else
				exit(-1);
			break;
		case SYS_REMOVE:
			if(remove(f->R.rdi))
				f->R.rax = true;
			else
				exit(-1);
			break;
		case SYS_OPEN:
			if(open(f->R.rdi))
				f->R.rax = true;
			else
				exit(-1);
			break;
		case SYS_FILESIZE:
			f->R.rax = filesize(f->R.rdi);
			break;
		case SYS_READ:
			f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:
			seek(f->R.rdi, f->R.rsi);
			break;
		case SYS_TELL:
			f->R.rax = tell(f->R.rdi);
			break;
		case SYS_CLOSE:
			close(f->R.rdi);
			break;
		default:
			exit(-1);
			break;
	}
	printf ("system call!\n");
	thread_exit ();
}

void halt(void) {
	power_off();
}

void exit (int status) {
	struct thread* curr = thread_current();
	curr->exit_status = status;
	printf("%s: exit(%d)\n", curr->name, status);
	thread_exit();
}

// pid_t fork (const char *thread_name) {
// 	process_fork(thread_name, thread_current()->tf);
// }

int exec (const char *file) {
	if(!is_user_vaddr(file)) {
		return -1;
	}

	return process_exec(file);
}

// // int wait (pid_t) {

// // }

bool create (const char *file, unsigned initial_size) {
	if(!is_user_vaddr(file)) {
		return false;
	}
	return filesys_create(file, initial_size);
}

bool remove (const char *file) {
	if(!is_user_vaddr(file)) {
		return false;
	}
	return filesys_remove(file);
}

int open (const char *file) {
	if(!is_user_vaddr(file)) {
		return false;
	}
	if(file == NULL || file == "\0") {
		return -1;
	}
	if(!is_user_vaddr(file)) {
		return -1;
	}
	struct file *file_ = filesys_open(file);

	int fd = STDERR_FILENO;
	while(fd_pool[fd++]) {}
	fd_pool[fd] = true;

	struct thread* curr = thread_current();
	curr->fdt[fd] = file_;

	return fd;
}

int filesize (int fd) {
	struct thread* curr = thread_current();
	struct file* file = curr->fdt[fd];
	if(file == NULL)
		return -1;
	return (int) file_length(file);
}

int read (int fd, void *buffer, unsigned length) {
	struct file * file = thread_current()->fdt[fd];
	if(file == NULL)
		return -1;
	return (int) file_read(file);
}

int write (int fd, const void *buffer, unsigned length) {
	struct file * file = thread_current()->fdt[fd];
	if(file == NULL)
		return -1;
	return (int) file_write(file);
}

void seek (int fd, unsigned position) {
	struct file * file = thread_current()->fdt[fd];
	if(file == NULL)
		return -1;
	file_seek(file, position);
}

unsigned tell (int fd) {
	struct file * file = thread_current()->fdt[fd];
	if(file == NULL)
		return -1;
	return (unsigned) file_tell(file);
}

void close (int fd) {
	// 유효한 주소 체크 함수 넣어주어야함.
	// 전역 fd pool의 해당 fd 인덱스 off
	if(!fd_pool[fd]) // 파일을 연속으로 닫으려고 할 경우 = 이미 닫혀있는 경우
		exit(-1);

	struct file * file = thread_current()->fdt[fd];
	if(file == NULL)
		exit(-1);

	fd_pool[fd] = false;
	file_close(file);
}