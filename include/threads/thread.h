#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "filesys/file.h"
#include "threads/vaddr.h" /* project 2 */
#ifdef VM
#include "vm/vm.h"
#endif

#define USERPROG

/* States in a thread's life cycle. */
enum thread_status
{
	THREAD_RUNNING, /* Running thread. */
	THREAD_READY,	/* Not running but ready to run. */
	THREAD_BLOCKED, /* Waiting for an event to trigger. */
	THREAD_DYING	/* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) - 1) /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0	   /* Lowest priority. */
#define PRI_DEFAULT 31 /* Default priority. */
#define PRI_MAX 63	   /* Highest priority. */

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread
{
	/* Owned by thread.c. */
	tid_t tid;				   /* Thread identifier. */
	enum thread_status status; /* Thread state. */
	char name[16];			   /* Name (for debugging purposes). */
	int priority;			   /* Priority. */
	int origin_priority;	   /* Origin_Priority.*/
	int64_t wake_up_ticks;	   /* Compare ticks to unblock */
	struct list locks;		   /*List of locks thread have*/
	struct lock *waiting_lock; /*Lock thread waiting*/
	int nice;
	int recent_cpu;

	/*project 2 ---------------------------------------*/
	struct thread* parent; /*parent thread*/

	int exit_status; /* syscall exit()*/
	struct file *fdt[FD_MAX];
	
	struct semaphore child_waiting_sema; /*waiting child exit*/
	struct list children_list; /*parent's children list*/
	struct list_elem child_elem; /*for list elem*/
	//struct semaphore create_sema; /* waiting child create */
	/* ----------------------------------------------- */
	struct list_elem thread_elem;
	bool children_list_initialized;

	/* Shared between thread.c and synch.c. */
	struct list_elem elem; /* List element. */

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4; /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf; /* Information for switching */
	unsigned magic;		  /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

bool priority_value_greater(const struct list_elem *a_, const struct list_elem *b_,
							void *aux UNUSED);

bool lock_priority_greater(const struct list_elem *a_, const struct list_elem *b_,
						   void *aux UNUSED);

void thread_init(void);
void thread_start(void);

void thread_tick(void);
void thread_print_stats(void);

typedef void thread_func(void *aux);
tid_t thread_create(const char *name, int priority, thread_func *, void *);

void thread_block(void);
void thread_unblock(struct thread *);

struct thread *thread_current(void);
tid_t thread_tid(void);
const char *thread_name(void);

void thread_exit(void) NO_RETURN;
void thread_yield(void);
void thread_preemption(void);


int thread_get_priority(void);
void thread_set_priority(int);


void thread_donate_priority(struct lock *);
void thread_recover_priority(struct lock *);

int thread_get_nice(void);
void thread_set_nice(int);
int thread_get_recent_cpu(void);
int thread_get_load_avg(void);
void set_recent_cpu();
void set_load_avg(void);
void thread_set_mlfqs_priority(void);

void do_iret(struct intr_frame *tf);

struct list_elem *list_max_with_empty_check(struct list *list, list_less_func *less, void *aux); // <Yeram522> Test용 max priority code

#define F (1 << 14)
#define INT_TO_FP(n) ((n) * F)
#define FP_TO_INT(x) ((x) / F)
#define FP_TO_INT_ROUND(x) (((x) >= 0 ? (x) + F / 2 : (x) - F / 2) / F)
#define FP_ADD(x, y) ((x) + (y))
#define FP_SUB(x, y) ((x) - (y))
#define FP_MUL(x, y) (((int64_t) (x)) * (y) / F)
#define FP_DIV(x, y) (((int64_t) (x)) * F / (y))

#endif /* threads/thread.h */
