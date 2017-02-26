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

typedef struct scheduler_ {
  //multilevel priority running queue of size 5
  struct queue_* runQ[RUN_QUEUE_SIZE];

  //node which holds the context which currently is running
  struct node_* current;

  //the context of the scheduler function which every other context will point to
  ucontext_t* termHandler;

  //number of times the scheduler function was called, used for maintainence
  int cycles;

  //timer to be set and reset that will set off the alarm signals
  struct itimerval* timer;

  //list of nodes waiting for a join
  struct list_* joinList;

  //list of threads
  struct threadList_* threads;

  //number of threads created for use in making thread id's
  int threadNum;

  //sorts the nodes in order of time created then re-enQ nodes to runQ with updated priorityLevel
  struct queue_* promotionQ[RUN_QUEUE_SIZE - 1];

  //start time of the scheduler
  time_t start_time;

} scheduler;

typedef struct node_ {
    struct my_pthread_t_* threadID;
    ucontext_t* ut;
    int priority;
    time_t timeCreated;
    double runtime;
    enum STATUS {
        NEUTRAL,
        YIELDING,
        EXITING,
        JOINING
    } status;
    struct my_pthread_t_* joinee;
    struct node_ * next;
} node;

typedef struct queue_ {
    struct node_* head;
    struct node_* rear;
    int priorityLevel;
} queue;

typedef struct list_ {
    struct node_* head;
} list;

typedef struct threadList_ {
    struct my_pthread_t_* head;
} threadList;

typedef struct my_pthread_mutex_t_ {
  int isLocked; //1 = locked, 0 = not locked
    int mutexID;
  struct my_pthread_mutex_t_ *next;
} my_pthread_mutex_t;

typedef struct my_pthread_t_ {
    int id;
    int isDead;
    void* exitArg;
    struct my_pthread_t_* next;
} my_pthread_t;

typedef struct mutex_list_ {
  struct my_pthread_mutex_t_ *head;
}mutex_list;

union pointerConverter{
    void* ptr;
    int arr[2];
};

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
