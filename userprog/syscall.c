#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
//#include "userprog/process.h"
#include "threads/flags.h"
#include "threads/palloc.h"
#include "filesys/filesys.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* Projects 2 and later. */
bool fd_pool[FD_MAX];

typedef int pid_t;

pid_t fork (const char *thread_name);
void exit(int status);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned length);
int write (int fd, const void *buffer, unsigned length);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);

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
	
	memset(fd_pool, false, FD_MAX);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	// 1. 시스템 콜 번호(include/lib/syscall-nr.h에 목록이 있음)를 받아온다. -> %rax에 있다.(f로 콜러의 레지스터에 접근 가능)
	// 2. 시스템 콜 인자들을 받아온다. -> 인자는 %rdi, %rsi, %rdx, %r10, %r8, %r9 순서로 전달
	// 3. 알맞은 액션을 취한다. (반환은 %rax에)
	struct thread* curr = thread_current();
	switch(f->R.rax) {
		case SYS_HALT:
			power_off();
			break;
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
		case SYS_FORK:
		 	f->R.rax = fork(f->R.rdi);
		 	break;
		case SYS_EXEC:
			if(!is_user_vaddr(f->R.rdi)) exit(-1);
			f->R.rax = process_exec(f->R.rdi);
			if(f->R.rax == -1) {
				exit(-1);
			}
	 	case SYS_WAIT:
		 	f->R.rax = process_wait(f->R.rdi);
		 	break;
		case SYS_CREATE:
			if(!is_user_vaddr(f->R.rdi)) exit(-1);

			if(filesys_create(f->R.rdi, f->R.rsi))
				f->R.rax = true;
			else
				exit(-1);
			break;
		case SYS_REMOVE:
			if(!is_user_vaddr(f->R.rdi)) exit(-1);

			if(filesys_remove(f->R.rdi))
				f->R.rax = true;
			else
				exit(-1);
			break;
		case SYS_OPEN:
			f->R.rax = open(f->R.rdi);
			if(f->R.rax < 0)
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

}

pid_t fork (const char *thread_name) {
	//새로은 프로세스의 parent에 현재 쓰레드 넣기.
	// 부모의 child list에 새로운자식 프로세스 elem 넣기
	// 새로운 프로세스가 자신의 락 aquire 하기.
	return process_fork(thread_name, thread_current()->tf);
}

void exit(int status){
	thread_current()->exit_status = status;
	thread_exit ();
}

int open (const char *file) {
	//printf ("systemcall!:open \n");

	if(file == NULL || !is_user_vaddr(file) ||  file == "\0") {
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
	//printf ("systemcall!:filesize \n");
	struct thread* curr = thread_current();
	struct file* file = curr->fdt[fd];
	if(file == NULL)
		return -1;
	return (int) file_length(file);
}

int read (int fd, void *buffer, unsigned length) {
	//printf ("systemcall!:read \n");
	struct file * file = thread_current()->fdt[fd];
	if(file == NULL)
		return -1;
	return (int) file_read(file,buffer,length);
}

int write (int fd, const void *buffer, unsigned length) {
	//printf ("systemcall!:write \n");
	
	if(fd == STDOUT_FILENO)
	{
		putbuf(buffer, length);
		return length;
	}
	if(STDIN_FILENO || STDERR_FILENO) return -1;

	// FD >= 3 일 경우 사용자 입출력
	struct file * file = thread_current()->fdt[fd];
	if(file == NULL)
		return -1;
	return (int) file_write(file,buffer,length);
}

void seek (int fd, unsigned position) {
	//printf ("systemcall!:seek \n");
	struct file * file = thread_current()->fdt[fd];
	if(file == NULL)
		return -1;
	file_seek(file, position);
}

unsigned tell (int fd) {
	//printf ("systemcall!:tell \n");
	struct file * file = thread_current()->fdt[fd];
	if(file == NULL)
		return -1;
	return (unsigned) file_tell(file);
}

void close (int fd) {
	// 유효한 주소 체크 함수 넣어주어야함.
	// 전역 fd pool의 해당 fd 인덱스 off
	//printf ("systemcall!:close \n");
	if(!fd_pool[fd]) // 파일을 연속으로 닫으려고 할 경우 = 이미 닫혀있는 경우
		exit(-1);

	struct file * file = thread_current()->fdt[fd];
	if(file == NULL)
		exit(-1);

	fd_pool[fd] = false;
	file_close(file);
}
