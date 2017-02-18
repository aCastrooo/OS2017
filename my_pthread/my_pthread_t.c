#include "my_pthread_t.h"

/******************************structs**************************/
typedef struct scheduler_ {
  //multilevel priority running queue of size 5
  struct queue_* runQ[RUN_QUEUE_SIZE];

  //node which holds the context which currently is running
  struct node_* current;

  //the context of the scheduler function which every other context will point to
  ucontext_t* schedContext;

  //timer to be set and reset that will set off the alarm signals
  struct itimerval* timer;

  //list of nodes waiting for a join
  struct list_* joinList;

  //list of nodes that have finished execution for use of nodes waiting on joins
  struct list_* deadList;

  //list of mutexes + waitQ
  struct my_pthread_mutex_t_* mutexList;
} scheduler;

typedef struct node_ {
    pthread_t* threadID;
    ucontext_t* ut;
    int priority;
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

typedef struct my_pthread_mutex_t_ {
    int mutexID;
    struct queue_* mutexWait;
} my_pthread_mutex_t;

union pointerConverter{
    void* ptr;
    int arr[2];
};

/******************globals***********************/
scheduler* scd = NULL;



/********************functions*******************/
//takes a pointer to a context and a pthread_t and returns a pointer to a node
node* createNode(ucontext_t* context, pthread_t* thread){
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
    /*
    //previous implementation of enQ before rear was added
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
    */
    /////////////
    if(q->head == NULL){
        q->head = newNode;
        q->rear = newNode;
        newNode->priority = q->priorityLevel;
    }else{
        q->rear->next = newNode;
        q->rear = newNode;
        newNode->priority = q->priorityLevel;
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
int existsInList(pthread_t* id, list* ls){
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
node* removeFromList(pthread_t* id, list* ls){
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
    scd = (scheduler*) malloc(sizeof(scheduler));

    int i = 0;
    for ( i = 0; i < RUN_QUEUE_SIZE; i++) {
        scd->runQ[i] = (queue*) malloc(sizeof(queue));
    }

    scd->current = NULL;

    scd->timer = (struct itimerval*) malloc(sizeof(struct itimerval));

    scd->joinList = (list*) malloc(sizeof(list));

    scd->deadList = (list*) malloc(sizeof(list));

    //to do: make a mutex list struct that holds a list of my_pthread_mutex_t
    scd->mutexList;


    //call getcontext, setup the ucontext_t, then makecontext with scheduler func
    /*
    ucontext_t* ct = (ucontext_t*) malloc(sizeof(ucontext_t));
    getcontext(ct);
    ct->uc_stack.ss_sp = (char*) malloc(STACK_SIZE);
    ct->uc_stack.ss_size = STACK_SIZE;
    makecontext(ct, POINTER_TO_SCHEDULER_FUNCTION, 0);
    scd->schedContext = ct;
    */

    ucontext_t* mainCxt = (ucontext_t*) malloc(sizeof(ucontext_t));
    getcontext(mainCxt);
    node* mainNode = createNode(mainCxt, (pthread_t*) 0);

    //enqueue mainNode into the runQ but don't switch contexts yet
    //let the first create thread call finish making the context for its thread
    //then start scheduling

}

//scheduler context function
void schedule(){
    /*
    while(queues are not empty){
      //loops while the run queues arent empty

      //enqueue the previously ran context on its respective queue or list

      //check join list/dead list to see if anything can be added back to the run queue

      //set the timer again
      //dequeue the next thing to be run
      //set scheduler->current to be what was dequeued
      //swapcontext with scheduler->current
    }
    */
}

//alarm signal handler that will set to the scheduler context which will change what is running
void timerHandler(int signum){

}

int my_pthread_create( pthread_t * thread, pthread_attr_t * attr, void *(*function)(void*), void * arg){
//thread is pointer that references this thread
//attr is unused
//function is the function that the thread will be running and will be passed to the context
//arg is the void pointer that points to the arg(s) passed to the function

//step 1: setup scheduler if it is not already set up
//step 2: call getcontext then makecontext using info from thread
//step 3: call scheduler function that adds this context to a list

    if(scd == NULL){
        initialize();
    }

    ucontext_t* newCxt = (ucontext_t*) malloc(sizeof(ucontext_t));
    getcontext(newCxt);
    newCxt->uc_stack.ss_sp = (char*) malloc(STACK_SIZE);
    newCxt->uc_stack.ss_size = STACK_SIZE;
    newCxt->uc_link = scd->schedContext;

    union pointerConverter pc;
    pc.ptr = arg;

    //fix this to make it work
    makecontext(newCxt, function, 2, pc.arr[1], pc.arr[0]);

    node* newNode = createNode(newCxt, thread);

    enQ(scd->runQ[0], newNode);

    return 1;
}

int main(int argc, char const *argv[]) {
  ucontext_t ct;

  getcontext(&ct);

  pthread_t* id = (pthread_t*) 0;

  node* head = createNode(&ct, id);

  printf("%d\n",head->threadID );

  queue* Q = (queue*) malloc(sizeof(queue));
  enQ(Q,head);

  int i;
  for ( i = 1; i <= 5; i++) {
    pthread_t* p = (pthread_t*) i;
    node* ptr = createNode(&ct, p);
    printf("created node %d\n",ptr->threadID );
    enQ(Q,ptr);
  }

  return 0;
}
