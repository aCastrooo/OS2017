#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>

#define pthread_t my_pthread_t
#define pthread_mutex_t my_pthread_mutex_t

#define pthread_create my_pthread_create
#define pthread_yield my_pthread_yield
#define pthread_exit my_pthread_exit
#define pthread_join my_pthread_join
#define pthread_mutex_init my_pthread_mutex_init
#define pthread_mutex_lock my_pthread_mutex_lock
#define pthread_mutex_unlock my_pthread_mutex_unlock
#define pthread_mutex_destroy my_pthread_mutex_destroy

#define RUN_QUEUE_SIZE 5
#define STACK_SIZE 10000

typedef struct node_ node;

typedef struct queue_ queue;

typedef struct list_ list;

typedef struct scheduler_ scheduler;

typedef struct threadList_ threadList;

typedef struct my_pthread_t_ my_pthread_t;

typedef struct mutex_list_ mutex_list;

typedef struct my_pthread_mutex_t_ my_pthread_mutex_t;

int my_pthread_create(my_pthread_t * thread, pthread_attr_t * attr,
  void *(*function)(void*), void * arg);
void my_pthread_yield();
void my_pthread_exit(void * value_ptr);
int my_pthread_join(my_pthread_t thread, void ** value_ptr);
int my_pthread_mutex_init(my_pthread_mutex_t * mutex,
  const pthread_mutexattr_t * mutexattr);
int my_pthread_mutex_lock(my_pthread_mutex_t * mutex);
int my_pthread_mutex_unlock(my_pthread_mutex_t * mutex);
int my_pthread_mutex_destroy(my_pthread_mutex_t * mutex);
