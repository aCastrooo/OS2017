#include "my_pthread_t.h"

struct scheduler_ {

  struct node_* runQ[RUN_QUEUE_SIZE];
  struct node_* current;
  struct node_* schedContext;
  struct node_* joinList;
  struct node_* deadList;

  //list of mutexes + waitQ
  struct my_pthread_mutex_t_* mutexList;

}

struct node_ {

    pthread_t threadID;
    ucontext_t ut;
    struct node_ * next;

}

void enQ(node * head, ucontext_t ut) {
    node * ptr = head;

    while (ptr->next != NULL) {
        ptr = ptr->next;
    }

    ptr->next = malloc(sizeof(node));
    ptr->next->ut = ut;
    ptr->next->next = NULL;
}

node deQ(node * head) {
    node * next = NULL;

    if (*head == NULL) {
        return NULL;
    }

    next = (*head)->next;
    free(*head);
    *head = next;

    return node;
}

void initialize(){

}

void schedule(){

}

int my_pthread_create( pthread_t * thread, pthread_attr_t * attr, void *(*function)(void*), void * arg){
//thread is pointer that references this thread
//attr is unused
//function is the function that the thread will be running and will be passed to the context
//arg is the void pointer that points to the arg(s) passed to the function

//step 1: setup scheduler if it is not already set up
//step 2: call getcontext then makecontext using info from thread
//step 3: call scheduler function that adds this context to a list

  return 0;
}
