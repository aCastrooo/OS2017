#include "my_pthread_t.h"


typedef struct scheduler_ {
  //multilevel priority running queue of size 5
  struct queue_* runQ[RUN_QUEUE_SIZE];

  //node which holds the context which currently is running
  struct node_* current;

  //the context of the scheduler function which every other context will point to
  ucontext_t schedContext;

  //list of nodes waiting for a join
  struct list_* joinList;

  //list of nodes that have finished execution for use of nodes waiting on joins
  struct list_* deadList;

  //list of mutexes + waitQ
  struct my_pthread_mutex_t_* mutexList;
} scheduler;

typedef struct node_ {
    pthread_t threadID;
    ucontext_t* ut;
    int priority;
    struct node_ * next;
} node;

typedef struct queue_ {
    struct node_* head;
    int priorityLevel;
} queue;

typedef struct list_ {
    struct node_* head;
} list;

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

//enqueues the node in the next lower priority queue if it can be demoted
void demoteNode(scheduler* sched, node* demotee){
    int newPriority = demotee->priority;

    if(demotee->priority < RUN_QUEUE_SIZE - 1){
        newPriority++;
    }

    enQ(sched->runQ[newPriority], demotee);
}

//takes pointer to head of list and pointer to the node to be inserted
void enQ(queue* q, node* newNode) {
    node* ptr = q->head;
    node* prev = NULL;

    newNode->priority = q->priorityLevel;

    while (ptr != NULL) {
        prev = ptr;
        ptr = ptr->next;
    }

    if(prev == NULL){
        q->head = newNode;
    }else{
        prev->next = newNode;
    }

}

//takes a pointer to the pointer of the head of the list NOT just a pointer to head
//returns pointer to node removed from head
node* deQ(queue* q) {
    node* head = q->head;

    if (head == NULL) {
        return NULL;
    }

    node* result = head;
    q->head = head->next;

    return result;
}

//returns 1 if the node with pthread id exists in list, 0 if not
int existsInList(pthread_t id, list* ls){
    node* ptr = ls->head;

    while(ptr != NULL){
      if(ptr->threadID == id){
        return 1;
      }
      ptr = ptr->next;
    }

    return 0;
}

void insertToList(node* newNode, list* ls) {
    newNode->next = ls->head;
    ls->head = newNode;
}

//removes a node from a list
node* removeFromList(pthread_t id, list* ls){
    node* ptr = ls->head;
    node* prev = NULL;

    while(ptr != NULL && ptr->threadID != id){
      prev = ptr;
      ptr = ptr->next;
    }

    if(ptr != NULL && ptr->threadID == id){
      if(prev == NULL){
        node* result = ptr;
        ls->head = ptr->next;
        return result;
      }else{
        prev->next = ptr->next;
        return ptr;
      }
    }

    return NULL;
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

  queue* Q = (queue*) malloc(sizeof(queue));
  enQ(Q,head);

  int i;
  for ( i = 1; i <= 5; i++) {
    pthread_t p = (pthread_t) i;
    node* ptr = createNode(&ct, p);
    printf("created node %d\n",ptr->threadID );
    enQ(Q,ptr);
  }

  return 0;
}
