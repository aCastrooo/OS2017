#include "my_pthread_t.h"

/******************************structs**************************/
typedef struct scheduler_ {
  //multilevel priority running queue of size 5
  struct queue_* runQ[RUN_QUEUE_SIZE];

  //node which holds the context which currently is running
  struct node_* current;

  //the context of the scheduler function which every other context will point to
  ucontext_t* schedContext;

  //number of times the scheduler function was called, used for maintainence
  int cycles;

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
    int timeCreated;
    int yielding;
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
    newNode->priority = 0;
    newNode->timeCreated = 0;
    newNode->yielding = 0;

    return newNode;
}

//enqueues the node in the next lower priority queue if it can be demoted
void demoteNode(node* demotee){

    int newPriority = demotee->priority;

    if(demotee->priority < RUN_QUEUE_SIZE - 1){
        newPriority++;
    }

    enQ(scd->runQ[newPriority], demotee);
}

//takes pointer to queue and pointer to the node to be inserted
void enQ(queue* q, node* newNode) {

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

//takes a pointer to the pointer to the queue and returns pointer to node removed from head
node* deQ(queue* q) {

    node* head = q->head;

    if (head == NULL) {
        return NULL;
    }

    node* result = head;
    q->head = head->next;

    return result;
}

//checks if any of the runqueues are empty, returns 1 if they are all empty, 0 otherwise
int isQempty(){

    pause_timer(scd->timer);
    int i;
    for ( i = 0; i < RUN_QUEUE_SIZE; i++) {
        if(scd->runQ[i] == NULL){
            unpause_timer(scd->timer);
            return 1;
        }
    }

    unpause_timer(scd->timer);

    return 0;
}

//dequeues the next node in the priority queue and returns it
node* getNextNode(){

    int i;
    for(i = 0; i < RUN_QUEUE_SIZE; i++){
        if(scd->runQ[i] != NULL){
            return deQ(scd->runQ[i]);
        }
    }

    return NULL;
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

//pause the timer for use in "blocking calls" so that if a
//function is using shared data (scheduler/queues/etc) it doesnt
//fuck with it
void pause_timer(struct itimerval* timer){

    struct itimerval zero = { 0 };
    setitimer(ITIMER_REAL, &zero, timer);
}

//always unpause your timers after doing the sensitive "blocking" task
void unpause_timer(struct itimerval* timer){

    setitimer(ITIMER_REAL, timer, NULL);
}

//alarm signal handler that will set to the scheduler context which will change what is running
void timerHandler(int signum){
    schedule();
}

//scheduler context function
void schedule(){
    /*
    while(queues/lists are not empty){
      //loops while the run queues arent empty or while there is something in the join list that can
      //be added to the run queue

      //enqueue the previously ran context on its respective queue or list

      //check join list/dead list to see if anything can be added back to the run queue

      //set the timer again
      //dequeue the next thing to be run
      //set scheduler->current to be what was dequeued
      //swapcontext with scheduler->current
    }
    */

    pause_timer(scd->timer);

    scd->cycles++;

    node* justRun = NULL;

    if(scd->timer->it_value.tv_usec > 0){
        //context finished within its allotted time and linked back to scheduler
        //or it yielded and has time left
        if(scd->current->yielding == 1){
            puts("someone yielded");
            scd->current->yielding = 0;
            enQ(scd->runQ[scd->current->priority], scd->current);
        }else{
            puts("someone died");
            insertToList(scd->current, scd->deadList);

        }
    }else{
        //context ran out of time and should be requeued
        demoteNode(scd->current);
    }

    justRun = scd->current;
    printf("justRun is %d\n",justRun->threadID );

    node* nextNode = getNextNode();

    if(nextNode == NULL){
        //nothing left to run, only thing left to do is exit
        printf("job's done\n");
        return;
    }
    printf("nextNode is %d\n",nextNode->threadID );

/*    node* ptr=NULL;
    int i=0;
    for(i = 0; i<5;i++){
      printf("queue #%d:",i );
      for(ptr = scd->runQ[i]->head;ptr != NULL; ptr=ptr->next){
        printf("%d ->",ptr->threadID );
      }
      printf("null\n" );
    }
*/
    scd->current = nextNode;

    //run time is 50ms * (level of priority + 1)
    scd->timer->it_value.tv_usec = 50000 * (scd->current->priority + 1);
    setitimer(ITIMER_REAL, scd->timer, NULL);

    swapcontext(justRun->ut, scd->current->ut);

}


//sets up all of the scheduler stuff
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

    ucontext_t* ct = (ucontext_t*) malloc(sizeof(ucontext_t));
    getcontext(ct);
    ct->uc_stack.ss_sp = (char*) malloc(STACK_SIZE);
    ct->uc_stack.ss_size = STACK_SIZE;
    makecontext(ct, schedule, 0);
    scd->schedContext = ct;

    scd->cycles = 0;


    ucontext_t* mainCxt = (ucontext_t*) malloc(sizeof(ucontext_t));
    getcontext(mainCxt);
    node* mainNode = createNode(mainCxt, (pthread_t*) 0);

    //enQ(scd->runQ[0], mainNode);
    scd->current = mainNode;

    //set up signal and timer
    signal(SIGALRM, timerHandler);

    scd->timer->it_value.tv_usec = 50000;//50ms
    setitimer(ITIMER_REAL, scd->timer, NULL);
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

    if(newCxt == NULL){
        return 0;
    }

    getcontext(newCxt);
    newCxt->uc_stack.ss_sp = (char*) malloc(STACK_SIZE);

    if(newCxt->uc_stack.ss_sp == NULL){
        return 0;
    }

    newCxt->uc_stack.ss_size = STACK_SIZE;
    newCxt->uc_link = scd->schedContext;

    union pointerConverter pc;
    pc.ptr = arg;

    //fix this to make it work
    makecontext(newCxt, function, 2, pc.arr[0], pc.arr[1]);

    node* newNode = createNode(newCxt, thread);

    pause_timer(scd->timer);
    enQ(scd->runQ[0], newNode);
    unpause_timer(scd->timer);

    return 1;
}

//tekes the thread that called this function and requeues it at the end of the current priority queue
void my_pthread_yield(){

    //1 means the context is yielding which the scheduler will look at and know what to do

    scd->current->yielding = 1;

    schedule();
    //swapcontext(scd->current->ut, scd->schedContext);
}

void* testfunc(void* arg){

    printf("got here and my number is %d\n",*(int*)arg );
    my_pthread_yield();
    printf("got here again\n" );
    long long int i = 0;
    long long int j=0;
    int c = 0;
    for (i = 0; i < 100000000; i++) {
      j++;
        if(j%10000000 == 0){
          printf("thread says j is: %d\n",c++ );
        }

    }
}

int main(int argc, char const *argv[]) {
  /*
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
  */

  int a=5;

  my_pthread_create((pthread_t*) 123, (pthread_attr_t*)0, testfunc, (void*) &a );
  my_pthread_yield();
  printf("back from first yield\n" );
  my_pthread_yield();
  printf("back from second yield\n" );
  long long int i = 0;
  long long int j=0;
  int c = 0;
  for (i = 0; i < 10000000000; i++) {
    j++;
      if(j%10000000 == 0){
        printf("main says j is: %d\n",c++ );
      }

  }

  return 0;
}
