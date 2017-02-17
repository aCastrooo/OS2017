#include "my_pthread_t.h"

struct scheduler{

}

struct node {

    pthread_t threadID;
    ucontext_t ut;
    struct node * next;

}


typedef struct pthread_t_ {

    int id;

} pthread_t;


void enqueue(node_t * head, ucontext_t ut) {
    node_t * ptr = head;

    while (ptr->next != NULL) {
        ptr = ptr->next;
    }

    ptr->next = malloc(sizeof(node_t));
    ptr->next->ut = ut;
    ptr->next->next = NULL;
}

ucontext_t dequeue(node_t ** head) {
    ucontext_t ut;
    node_t * next = NULL;

    if (*head == NULL) {
        return NULL;
    }

    next = (*head)->next;
    ut = (*head)->ut;
    free(*head);
    *head = next;

    return ut;
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
