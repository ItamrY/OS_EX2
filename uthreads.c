#include "uthreads.h"
#include <sys/time.h>
#include <signal.h>

#include <stdlib.h>
#include <setjmp.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>


static thread_t threads[MAX_THREAD_NUM];         // Thread table
static char stacks[MAX_THREAD_NUM][STACK_SIZE];  // Stacks for all threads
static int total_quantums = 0;                   // Total number of quantums
static int current_tid = 0;                // Currently running thread ID

// Round-robin ready queue
static int ready_queue[MAX_THREAD_NUM];
static int ready_front = 0, ready_back = 0;

// Timer setup
static struct itimerval timer;
static struct sigaction sa;
static int quantum_usecs = 0;



void enqueue(int tid) {
    ready_queue[ready_back++] = tid;
    if (ready_back == MAX_THREAD_NUM) ready_back = 0;
}

int dequeue() {
    if (ready_front == ready_back) return -1; // empty
    int tid = ready_queue[ready_front++];
    if (ready_front == MAX_THREAD_NUM) ready_front = 0;
    return tid;
}

bool is_empty() {
    return ready_front == ready_back;
}



int uthread_init(int usecs) {
    if (usecs <= 0) return -1;
    quantum_usecs = usecs;

    // Setup signal handler
    sa.sa_handler = &timer_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGVTALRM, &sa, NULL) < 0) return -1;

    // Setup timer
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = quantum_usecs;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = quantum_usecs;
    if (setitimer(ITIMER_VIRTUAL, &timer, NULL)) return -1;

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

// int uthread_spawn(thread_entry_point entry_point) {
//     if (entry_point == NULL) {
//         fprintf(stderr, "Error: entry_point cannot be NULL.\n");
//         return -1;
//     }

//     // Find available slot
//     int tid = -1;
//     for (int i = 1; i < MAX_THREAD_NUM; i++) {
//         if (threads[i].state == THREAD_UNUSED) {
//             tid = i;
//             break;
//         }
//     }

//     if (tid == -1) {
//         fprintf(stderr, "Error: No available thread slots.\n");
//         return -1;
//     }

//     // Initialize the thread struct
//     threads[tid].tid = tid;
//     threads[tid].state = THREAD_READY;
//     threads[tid].quantums = 0;
//     threads[tid].sleep_until = 0;
//     threads[tid].entry = entry_point;

//     // 🟨 This is a global 2D array of stacks for each thread
//     // You should declare this globally in your implementation file:
//     // static char thread_stacks[MAX_THREAD_NUM][STACK_SIZE];
//     setup_thread(tid, thread_stacks[tid], entry_point);

//     // 🟨 This assumes you have some ready queue, e.g. an array or list
//     // You can replace `enqueue_ready_thread(tid)` with your own method
//     enqueue_ready_thread(tid);  // ❗️ MADE-UP FUNCTION: add tid to ready queue

//     return tid;
// }

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

            setup_thread(i, thread_stacks[i], entry_point);

            enqueue_ready_thread(i);
            // ##### TODO: implement enqueue_ready_thread
            return i;
        }
    }
    return -1; // No available slot
}

int uthread_terminate(int tid) {
    if (tid < 0 || tid >= MAX_THREAD_NUM || threads[tid].state == THREAD_UNUSED) {
        return -1;
    }

    threads[tid].state = THREAD_TERMINATED;

    if (tid == 0) {
        // Clean up and terminate entire process if main thread ends
        exit(0);
    }

    if (tid == current_tid) {
        schedule_next();
    }

    return 0;
}

int uthread_block(int tid) {
    if (tid <= 0 || tid >= MAX_THREAD_NUM || threads[tid].state == THREAD_UNUSED) {
        return -1;
    }
    if (threads[tid].state == THREAD_BLOCKED) {
        return 0; // Already blocked
    }

    threads[tid].state = THREAD_BLOCKED;

    if (tid == current_tid) {
        schedule_next();
    }

    return 0;
}

int uthread_resume(int tid) {
    if (tid <= 0 || tid >= MAX_THREAD_NUM || threads[tid].state == THREAD_UNUSED) {
        return -1;
    }
    if (threads[tid].state != THREAD_BLOCKED) {
        return 0;
    }

    threads[tid].state = THREAD_READY;
    enqueue_ready_thread(tid);
    return 0;
}

int uthread_sleep(int num_quantums) {
    if (current_tid == 0 || num_quantums <= 0) {
        return -1;
    }

    threads[current_tid].sleep_until = total_quantums + num_quantums;
    threads[current_tid].state = THREAD_BLOCKED;
    schedule_next();

    return 0;
}

int uthread_get_tid() {
    return current_tid;
}

int uthread_get_total_quantums() {
    return total_quantums;
}

int uthread_get_quantums(int tid) {
    if (tid < 0 || tid >= MAX_THREAD_NUM || threads[tid].state == THREAD_UNUSED) {
        return -1;
    }
    return threads[tid].quantums;
}

void schedule_next(void) {
    // Placeholder
}

void context_switch(thread_t *current, thread_t *next) {
    // Placeholder
}

void timer_handler(int signum) {
    // Placeholder
}

void setup_thread(int tid, char *stack, thread_entry_point entry_point) {
    // Placeholder
}



