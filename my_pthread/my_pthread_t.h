#ifndef my_pthread_t_h
#define my_pthread_t_h

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ucontext.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include <malloc.h>
#include <string.h>

#define RUN_QUEUE_SIZE 5
#define QUANTA_TIME 50
#define NUM_CYCLES 10

#define MAX_MEMORY 8388608
#define LIBRARYREQ 0
#define THREADREQ 1
#define PAGESIZE sysconf(_SC_PAGE_SIZE)
#define STACK_SIZE PAGESIZE - sizeof(ucontext_t)

#define pthread_t my_pthread_t
#define pthread_attr_t my_pthread_attr_t
#define pthread_mutex_t my_pthread_mutex_t
#define pthread_mutex_attr_t my_pthread_mutexattr_t

#define pthread_create my_pthread_create
#define pthread_yield my_pthread_yield
#define pthread_exit my_pthread_exit
#define pthread_join my_pthread_join
#define pthread_mutex_init my_pthread_mutex_init
#define pthread_mutex_lock my_pthread_mutex_lock
#define pthread_mutex_unlock my_pthread_mutex_unlock
#define pthread_mutex_destroy my_pthread_mutex_destroy

#define malloc(x) myallocate( x, __FILE__, __LINE__, THREADREQ);
#define free(x) mydeallocate(x, __FILE__, __LINE__, THREADREQ);

typedef struct my_pthread_mutex_t_ {
  int isLocked; //1 = locked, 0 = not locked
    int mutexID;
    int isInit;
  struct my_pthread_mutex_t_ *next;
} my_pthread_mutex_t;

typedef struct my_pthread_t_ {
    int id;
    int isDead;
    void* exitArg;
    struct my_pthread_t_* next;
} my_pthread_t;

typedef struct my_pthread_attr_t_ {
    int nothing;
} my_pthread_attr_t;

typedef struct my_pthread_mutexattr_t_ {
    int nothing;
} my_pthread_mutexattr_t;


int my_pthread_create(my_pthread_t * thread, my_pthread_attr_t * attr,
  void *(*function)(void*), void * arg);
void my_pthread_yield();
void my_pthread_exit(void * value_ptr);
int my_pthread_join(my_pthread_t thread, void ** value_ptr);
int my_pthread_mutex_init(my_pthread_mutex_t * mutex,
  const my_pthread_mutexattr_t * mutexattr);
int my_pthread_mutex_lock(my_pthread_mutex_t * mutex);
int my_pthread_mutex_unlock(my_pthread_mutex_t * mutex);
int my_pthread_mutex_destroy(my_pthread_mutex_t * mutex);

void* myallocate(size_t size, const char* file, int line, int caller);
void mydeallocate(void* ptr, const char* file, int line, int caller);

#endif
