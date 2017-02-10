#include "my_pthread_t.h"

int my_pthread_create( pthread_t * thread, pthread_attr_t * attr, void *(*function)(void*), void * arg){
//thread is pointer that references this thread
//attr is unused
//function is the function that the thread will be running and will be passed to the context
//arg is the void pointer that points to the arg(s) passed to the function

//step 1: setup scheduler if it is not already set up
//step 2: call makecontext using info from thread
  return 0;
}
