#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "lib/kernel/list.h"
#include "timer.h"

/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/*Get list entry*/
#define list_entry(LIST_ELEM, STRUCT, MEMBER)           \
	((STRUCT *) ((uint8_t *) &(LIST_ELEM)->next     \
		- offsetof (STRUCT, MEMBER.next)))

/* Number of timer ticks since OS booted. */
static int64_t ticks;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

/* List of checking sleeping thread */
static struct list sleep_list;

static intr_handler_func timer_interrupt;
static bool too_many_loops(unsigned loops);
static void busy_wait(int64_t loops);
static void real_time_sleep(int64_t num, int32_t denom);
static bool wake_up_ticks_less (const struct list_elem *a_, const struct list_elem *b_,void *aux UNUSED) ;


/* Sets up the 8254 Programmable Interval Timer (PIT) to
   interrupt PIT_FREQ times per second, and registers the
   corresponding interrupt. */
void timer_init(void)
{
	/* 8254 input frequency divided by TIMER_FREQ, rounded to
	   nearest. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	outb(0x43, 0x34); /* CW: counter 0, LSB then MSB, mode 2, binary. */
	outb(0x40, count & 0xff);
	outb(0x40, count >> 8);

	list_init(&sleep_list);

	intr_register_ext(0x20, timer_interrupt, "8254 Timer");
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void timer_calibrate(void)
{
	unsigned high_bit, test_bit;

	ASSERT(intr_get_level() == INTR_ON);
	printf("Calibrating timer...  ");

	/* Approximate loops_per_tick as the largest power-of-two
	   still less than one timer tick. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops(loops_per_tick << 1))
	{
		loops_per_tick <<= 1;
		ASSERT(loops_per_tick != 0);
	}

	/* Refine the next 8 bits of loops_per_tick. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops(high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf("%'" PRIu64 " loops/s.\n", (uint64_t)loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
int64_t
timer_ticks(void)
{
	enum intr_level old_level = intr_disable();
	int64_t t = ticks;
	intr_set_level(old_level);
	barrier();
	return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
int64_t
timer_elapsed(int64_t then)
{
	return timer_ticks() - then;
}

static bool
wake_up_ticks_less (const struct list_elem *a_, const struct list_elem *b_,
            void *aux UNUSED) 
{
  const struct thread *a = list_entry (a_, struct thread, elem);
  const struct thread *b = list_entry (b_, struct thread, elem);
  
  return a->wake_up_ticks < b->wake_up_ticks;
}

/* Suspends execution for approximately TICKS timer ticks. */
void timer_sleep(int64_t ticks)
{
	int64_t start = timer_ticks();
	ASSERT(intr_get_level() == INTR_ON);
	
	// 예외 처리
	if (ticks <= 0) {
		return;
	}

	struct thread *current_thread = thread_current();
	enum intr_level old_level = intr_disable(); // 현재 인터럽트 레벨 저장하고 비활성화 

	current_thread->wake_up_ticks = start + ticks; // 스레드를 unblock시켜야 하는 ticks를 계산해 스레드의 멤버 변수 wake_up_ticks에 저장

	list_insert_ordered(&sleep_list, &current_thread->elem, wake_up_ticks_less, NULL); // wake_up_ticks를 기준으로 오름차순 정렬하여 sleep_list에 스레드 삽입

	thread_block(); // 스레드를 sleep 상태로 전환

	intr_set_level(old_level); // 인터럽트 레벨 복구
}

/* Suspends execution for approximately MS milliseconds. */
void timer_msleep(int64_t ms)
{
	real_time_sleep(ms, 1000);
}

/* Suspends execution for approximately US microseconds. */
void timer_usleep(int64_t us)
{
	real_time_sleep(us, 1000 * 1000);
}

/* Suspends execution for approximately NS nanoseconds. */
void timer_nsleep(int64_t ns)
{
	real_time_sleep(ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void timer_print_stats(void)
{
	printf("Timer: %" PRId64 " ticks\n", timer_ticks());
}

/* Timer interrupt handler. */
static void
timer_interrupt(struct intr_frame *args UNUSED)
{
	ticks++;

	// Search target of wake_up_thread from sleep list 
	struct list_elem *e = list_begin(&sleep_list);
    while (e != list_end(&sleep_list))
    {
		//Get start adderess of thread t from element addr
        struct thread *t = list_entry(e, struct thread, elem); 
		
        if (ticks < t->wake_up_ticks)
            break;  // Assume sorting list

		/* Makes thread unblocock if timer_ticks() >=  thread->wake_up_ticks*/
        e = list_remove(e);  // remove current elem and move next elem

        thread_unblock(t); // use unblock func
    }

	// 타이머 인터럽트가 발생할 때마다 실행 중인 스레드만 recent_cpu가 1씩 증가
	thread_current()->recent_cpu ++;

	//모든 스레드(실행 중이든, ready상태거나 block되어 있는 것에 상관없이)의 recent_cpu 값이 다음의 공식을 사용하여 매 초마다 다시 계산
	/* recent_cpu의 재계산은 시스템 틱 카운터가 1초의 배수에 도달했을 때 
	즉, timer_ticks () % TIMER_FREQ == 0 일 때 정확하게 이루어져야하며 다른 어떤 시점에서도 이루어지면 안됩니다.*/
	if(timer_ticks () % TIMER_FREQ == 0) //per time
	{
		//cpu 재계산
		thread_set_recent_cpu(); //ready queue and waite queue
		//set load avg
		thread_set_load_avg();
	}
	
	thread_tick();
}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops(unsigned loops)
{
	/* Wait for a timer tick. */
	int64_t start = ticks;
	while (ticks == start)
		barrier();

	/* Run LOOPS loops. */
	start = ticks;
	busy_wait(loops);

	/* If the tick count changed, we iterated too long. */
	barrier();
	return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait(int64_t loops)
{
	while (loops-- > 0)
		barrier();
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep(int64_t num, int32_t denom)
{
	/* Convert NUM/DENOM seconds into timer ticks, rounding down.

	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
	   1 s / TIMER_FREQ ticks
	   */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT(intr_get_level() == INTR_ON);
	if (ticks > 0)
	{
		/* We're waiting for at least one full timer tick.  Use
		   timer_sleep() because it will yield the CPU to other
		   processes. */
		timer_sleep(ticks);
	}
	else
	{
		/* Otherwise, use a busy-wait loop for more accurate
		   sub-tick timing.  We scale the numerator and denominator
		   down by 1000 to avoid the possibility of overflow. */
		ASSERT(denom % 1000 == 0);
		busy_wait(loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}

