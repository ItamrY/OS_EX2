#include "uthreads.h"
#include <sys/time.h>
#include <signal.h>

#include <stdlib.h>
#include <setjmp.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>


static thread_t threads[MAX_THREAD_NUM];                // Thread table
static char thread_stacks[MAX_THREAD_NUM][STACK_SIZE];  // Each thread's dedicated stack
static int total_quantums = 0;                          // Total number of quantums
static int current_tid = 0;                             // Currently running thread ID


// Round-robin ready queue
static int ready_queue[MAX_THREAD_NUM];
static int ready_front = 0, ready_back = 0;

// Timer and signal setup
static struct itimerval timer;      // Holds interval timer settings for the scheduler
static struct sigaction sa;         // Registers the timer signal handler
static int quantum_usecs = 0;       // Duration of each quantum in microseconds



// void enqueue(int tid) {
//     ready_queue[ready_back++] = tid;
//     if (ready_back == MAX_THREAD_NUM) ready_back = 0;
// }

// int dequeue() {
//     if (ready_front == ready_back) return -1; // empty
//     int tid = ready_queue[ready_front++];
//     if (ready_front == MAX_THREAD_NUM) ready_front = 0;
//     return tid;
// }

// bool is_empty() {
//     return ready_front == ready_back;
// }

/*
Queue helpers - used to enqueue a ready thread into the scheduler and dequeue once quantum is up
*/

void enqueue_ready_thread(int tid) {
    ready_queue[ready_back % MAX_THREAD_NUM] = tid;
    ready_back++;
}

int dequeue_ready_thread() {
    if (ready_front == ready_back) return -1;
    int tid = ready_queue[ready_front % MAX_THREAD_NUM];
    ready_front++;
    return tid;
}



int uthread_init(int usecs) {
    if (usecs <= 0) return -1;
    quantum_usecs = usecs;

    // Setup signal handler for SIGVTALRM (virtual timer signal)
    sa.sa_handler = &timer_handler;             // Set handler function
    sigemptyset(&sa.sa_mask);                   // Block no additional signals
    sa.sa_flags = 0;                            // Default behavior
    if (sigaction(SIGVTALRM, &sa, NULL) < 0)    // Register handler
        return -1;

    // Configure virtual timer to go off every 'quantum_usecs' microseconds
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = quantum_usecs;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = quantum_usecs;
    if (setitimer(ITIMER_VIRTUAL, &timer, NULL)) //start virtual timer
        return -1;

    // Initialize main thread
    thread_t *main_thread = &threads[0];
    main_thread->tid = 0;
    main_thread->state = THREAD_RUNNING;
    main_thread->quantums = 1;
    main_thread->sleep_until = 0;
    main_thread->entry = NULL;
    total_quantums = 1;

    return 0;
}

// Spawns a new thread with the given entry point and adds it to the ready queue.
int uthread_spawn(thread_entry_point entry_point) {

    if (!entry_point) return -1;

    for (int i = 1; i < MAX_THREAD_NUM; i++) {
        if (threads[i].state == THREAD_UNUSED) {
            thread_t *t = &threads[i];
            t->tid = i;
            t->state = THREAD_READY;
            t->quantums = 0;
            t->sleep_until = 0;
            t->entry = entry_point;

            //     // Initialize the thread struct
            //     threads[tid].tid = tid;
            //     threads[tid].state = THREAD_READY;
            //     threads[tid].quantums = 0;
            //     threads[tid].sleep_until = 0;
            //     threads[tid].entry = entry_point;

            setup_thread(i, thread_stacks[i], entry_point);
            enqueue_ready_thread(i);
            return i;
        }
    }
    return -1; // No available slot
}

// Terminates the thread with the given ID. If it's the main thread, the process exits.
int uthread_terminate(int tid) {
    //check if thread is active and valid
    if (tid < 0 || tid >= MAX_THREAD_NUM || threads[tid].state == THREAD_UNUSED) {
        return -1;
    }

    threads[tid].state = THREAD_TERMINATED;

    // if main thread terminates, shut down process
    if (tid == 0) {
        exit(0);
    }

    // if current thread terminates, call scheduler and context switch to next thread
    if (tid == current_tid) {
        schedule_next();
    }

    return 0;
}

// Blocks the specified thread. The main thread (tid = 0) cannot be blocked.
int uthread_block(int tid) {
    //check if thread is valid and active
    if (tid <= 0 || tid >= MAX_THREAD_NUM || threads[tid].state == THREAD_UNUSED) {
        return -1;
    }
    if (threads[tid].state == THREAD_BLOCKED) {
        return 0; // Already blocked
    }

    threads[tid].state = THREAD_BLOCKED;

    // if current thread blocked, call scheduler and context switch to next thread
    if (tid == current_tid) {
        schedule_next();
    }

    return 0;
}

// Resumes a blocked thread, placing it back in the ready queue.
int uthread_resume(int tid) {
    if (tid <= 0 || tid >= MAX_THREAD_NUM || threads[tid].state == THREAD_UNUSED) {
        return -1;
    }
    if (threads[tid].state != THREAD_BLOCKED) {
        return 0; // not blocked - no need to resume
    }

    threads[tid].state = THREAD_READY;
    enqueue_ready_thread(tid);
    return 0;
}

// Puts the current thread to sleep for the given number of quantums.
int uthread_sleep(int num_quantums) {
    // check that main isn't sleeping and count is positive
    if (current_tid == 0 || num_quantums <= 0) {
        return -1;
    }

    // Put the current thread to sleep for a specified number of quantums and context switch to the next thread
    threads[current_tid].sleep_until = total_quantums + num_quantums;
    threads[current_tid].state = THREAD_BLOCKED;
    schedule_next();

    return 0;
}

// Return thread ID
int uthread_get_tid() {
    return current_tid;
}

// Returns the total number of quantums since the program started.
int uthread_get_total_quantums() {
    return total_quantums;
}

// Return the amount of quantums the thread has run
int uthread_get_quantums(int tid) {
    // check valid active thread
    if (tid < 0 || tid >= MAX_THREAD_NUM || threads[tid].state == THREAD_UNUSED) {
        return -1;
    }
    return threads[tid].quantums;
}

/*
HELPER FUNCTIONS
*/

void schedule_next(void) {
    // save current runnign tid
    int prev_tid = current_tid;
    wake_sleeping_threads(); // helper function below

    // get next ready thread from ready queue
    int next_tid = dequeue_ready_thread();

    //if no thread is ready, switch to next
    if (next_tid == -1 || threads[next_tid].state != THREAD_READY) 
        return;

    // if current thread is still runnning - mark it READY
    if (threads[prev_tid].state == THREAD_RUNNING) {
        threads[prev_tid].state = THREAD_READY;
        enqueue_ready_thread(prev_tid);
    }

    // schedule next - set next to running, update current_tid and call context switch 
    threads[next_tid].state = THREAD_RUNNING;
    current_tid = next_tid;
    context_switch(&threads[prev_tid], &threads[next_tid]);
}

// Check if any blocked threads have finished sleeping, and move them to the ready queue.
void wake_sleeping_threads() {
    for (int i = 0; i < MAX_THREAD_NUM; ++i) {
        //check if thread is blocked and sleeping time is over
        if (threads[i].state == THREAD_BLOCKED && threads[i].sleep_until > 0 && threads[i].sleep_until <= total_quantums) {
            // Ready the thread, reset sleep time and push back to ready queue
            threads[i].state = THREAD_READY;
            threads[i].sleep_until = 0;
            enqueue_ready_thread(i);
        }
    }
}

void context_switch(thread_t *current, thread_t *next) {
    
    if (sigsetjmp(current->env, 1) == 0) {      // save the thread context
        siglongjmp(next->env, 1);               // restore context of next thread from where it stopped
    }
}

void timer_handler(int signum) {
    (void)signum;                           // timer handler signature var
    total_quantums++;                       // incr total quantums that have run
    threads[current_tid].quantums++;        // incr total quantums the thread has run
    schedule_next();                        // switch to next
}

void setup_thread(int tid, char *stack, thread_entry_point entry_point) {
        sigsetjmp(threads[tid].env, 1);

#define JB_SP 6
#define JB_PC 7

    if (sigsetjmp(threads[tid].env, 1) == 0) {
    // Allocate stack top
    void* sp = thread_stacks[tid] + STACK_SIZE;

    // Align stack if needed
    sp = (void*)((uintptr_t)sp & -8L);  // 8-byte alignment

    // Cast to jmp_buf internal long array — only works on Linux
    long* buf = (long*)&threads[tid].env;

    buf[JB_SP] = (long)sp;
    buf[JB_PC] = (long)thread_jumper;
}

}

void thread_jumper() {
    int tid = current_tid;
    if (threads[tid].entry != NULL) {
        threads[tid].entry();  // Run the user function
    }
    uthread_terminate(tid);  // Exit when done
}



