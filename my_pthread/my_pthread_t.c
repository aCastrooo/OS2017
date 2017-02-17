#include "my_pthread_t.h"


typedef struct scheduler_ {

  //multilevel priority running queue of size 5
  struct node_* runQ[RUN_QUEUE_SIZE];

  //node which holds the context which currently is running
  struct node_* current;

  //the context of the scheduler function which every other context will point to
  ucontext_t schedContext;

  //list of nodes waiting for a join
  struct node_* joinList;

  //list of nodes that have finished execution for use of nodes waiting on joins
  struct node_* deadList;

  //list of mutexes + waitQ
  struct my_pthread_mutex_t_* mutexList;


} scheduler;

typedef struct node_ {

    pthread_t threadID;
    ucontext_t* ut;
    struct node_ * next;

} node;

typedef struct my_pthread_mutex_t_ {
    int mutexID;

} my_pthread_mutex_t;

//takes a pointer to a context and a pthread_t and returns a pointer to a node
node* createNode(ucontext_t* context, pthread_t thread){
    node* newNode = (node*) malloc(sizeof(node));
    newNode->threadID = thread;
    newNode->ut = context;
    newNode->next = NULL;

    return newNode;
}

//takes pointer to head of list and pointer to the node to be inserted
void enQ(node* head, node* newNode) {
    node* ptr = head;
    node* prev = NULL;


    while (ptr != NULL) {
        prev = ptr;
        ptr = ptr->next;
    }

    if(prev == NULL){
        head = newNode;
    }else{
        prev->next = newNode;
    }

}

//takes a pointer to the pointer of the head of the list NOT just a pointer to head
//returns pointer to node removed from head
node* deQ(node** listPtr) {
    node* head = *listPtr;

    if (head == NULL) {
        return NULL;
    }

    node* result = head;
    head = head->next;
    *listPtr = head;

    return result;
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

int main(int argc, char const *argv[]) {
  ucontext_t ct;

  getcontext(&ct);

  pthread_t id = (pthread_t) 0;

  node* head = createNode(&ct, id);

  printf("%d\n",head->threadID );

  int i;
  for ( i = 1; i <= 5; i++) {
    pthread_t p = (pthread_t) i;
    node* ptr = createNode(&ct, p);
    printf("created node %d\n",ptr->threadID );
    enQ(head,ptr);
  }

  node* x = head;

  while (head != NULL) {
    node** hptr = &head;
    x=deQ(hptr);
    printf("dequeued %d\n",x->threadID );
  }

  if(head == NULL){
    printf("head is no more\n" );
  }
  return 0;
}
