#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <signal.h>

struct node_;
typedef struct node_ node;

struct scheduler_;
typedef struct scheduler_ scheduler;

struct mutex_;
typedef struct mutex_ mutex;

int my_pthread_create(pthread_t * thread, pthread_attr_t * attr,
  void *(*function)(void*), void * arg);
void my_pthread_yield();
void my_pthread_exit(void * value_ptr);
int my_pthread_join(pthread_t thread, void ** value_ptr);
int my_pthread_mutex_init(my_pthread_mutex_t * mutex,
  const pthread_mutexattr_t * mutexattr);
int my_pthread_mutex_lock(my_pthread_mutex_t * mutex);
int my_pthread_mutex_unlock(my_pthread_mutex_t * mutex);
int my_pthread_mutex_destroy(my_pthread_mutex_t * mutex);
