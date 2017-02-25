#include "my_pthread_t.h"

/******************************structs**************************/
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

  //list of mutexes + waitQ
  struct my_pthread_mutex_t_* mutexList;

  //number of threads created for use in making thread id's
  int threadNum;

  //sorts the nodes in order of time created then re-enQ nodes to runQ with updated priorityLevel
  struct queue_* promotionQ[RUN_QUEUE_SIZE - 1];
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
        EXITING
    } status;
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
    int mutexID;
    struct queue_* mutexWait;
} my_pthread_mutex_t;

typedef struct my_pthread_t_ {
    int id;
    int isDead;
    void* exitArg;
    struct my_pthread_t_* next;
} my_pthread_t;

union pointerConverter{
    void* ptr;
    int arr[2];
};



/******************globals***********************/
scheduler* scd = NULL;



/********************functions*******************/
//takes a pointer to a context and a pthread_t and returns a pointer to a node
node* createNode(ucontext_t* context, my_pthread_t* thread){
    node* newNode = (node*) malloc(sizeof(node));
    newNode->threadID = thread;
    newNode->ut = context;
    newNode->next = NULL;
    newNode->priority = 0;
    newNode->timeCreated = time(NULL);
    newNode->status = NEUTRAL;
    newNode->runtime = 0;
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

void insertByTime(queue* q, node* newNode){
  if(q->head == NULL){
    q->head = newNode;
    q->rear = newNode;
    q->head->next = NULL;
    q->rear->next = NULL;
  }
  node* prev = NULL;
  node* ptr;
  for(ptr = q->head; ptr != NULL; ptr = ptr->next){

    if(newNode->runtime > ptr->runtime){

      if(prev == NULL){
        newNode->next = ptr;
        q->head = newNode;
        return;
      }

      newNode->next = ptr;
      prev->next = newNode;
      return;

    }
    prev = ptr;
  }

  q->rear->next = newNode;
  q->rear = newNode;
  return;
}

//takes pointer to queue and pointer to the node to be inserted
void enQ(queue* q, node* newNode) {
    if(q->head == NULL){
        q->head = newNode;
        q->rear = newNode;
        q->head->next = NULL;
        q->rear->next = NULL;
        newNode->priority = q->priorityLevel;
    }else{
        /*if (q->rear == NULL) {
            printf("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n" );
        }*/
        q->rear->next = newNode;
        q->rear = newNode;
        q->rear->next = NULL;
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


    result->next = NULL;

    if(q->head == NULL){
        q->rear = NULL;

    }

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
        if(scd->runQ[i]->head != NULL){
            node* result = deQ(scd->runQ[i]);
            return result;
        }
    }

    return NULL;
}


//returns 1 if the node with pthread id exists in list, 0 if not
int existsInList(my_pthread_t* thread, list* ls){

    node* ptr = ls->head;

    while(ptr != NULL){
      if(ptr->threadID->id == thread->id){
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
node* removeFromList(my_pthread_t* thread, list* ls){

    node* ptr = ls->head;
    node* prev = NULL;

    while(ptr != NULL && ptr->threadID->id != thread->id){
      prev = ptr;
      ptr = ptr->next;
    }

    if(ptr != NULL && ptr->threadID->id == thread->id){
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

//adds thread to threadlist in scheduler struct
void addThread(my_pthread_t* thread){
    thread->next = scd->threads->head;
    scd->threads->head = thread;
}

void threadDied(my_pthread_t* thread){
    my_pthread_t* ptr = scd->threads->head;
    while(ptr != NULL){
        if(ptr->id == thread->id){
            ptr->isDead = 1;
            return;
        }
        ptr = ptr->next;
    }
}

//returns 1 if the node with pthread id exists in threadlist, 0 if not
int existsInThreadList(my_pthread_t* thread){

    my_pthread_t* ptr = scd->threads->head;

    while(ptr != NULL){
      if(ptr->id == thread->id){
        return 1;
      }
      ptr = ptr->next;
    }

    return 0;
}

//pause the timer for use in "blocking calls" so that if a
//function is using shared data (scheduler/queues/etc) it doesnt
//fuck with it
void pause_timer(struct itimerval* timer){

    struct itimerval zero = { 0 };
    setitimer(ITIMER_REAL, &zero, timer);
    scd->timer = timer;
}

//always unpause your timers after doing the sensitive "blocking" task
void unpause_timer(struct itimerval* timer){

    setitimer(ITIMER_REAL, timer, NULL);
}

//alarm signal handler that will set to the scheduler context which will change what is running
void timerHandler(int signum){
    //puts("times up");
    //printf("current's status is %d\n",scd->current->status );
    //printf("time left on timer = %d\n",scd->timer->it_value.tv_usec );

    /*printf("current thread id is %d\n",scd->current->threadID->id );
    puts("printing runQ");
      node* ptr=NULL;

      ptr = scd->runQ[0]->head;
      printf("0->head is %d\n",ptr );
      int i;
      for(i = 0; i<5;i++){
        printf("queue #%d:",i );
        for(ptr = scd->runQ[i]->head;ptr != NULL; ptr=ptr->next){
          printf("%d ->",ptr->threadID->id );
        }
        printf("null\n" );
      }

    puts("printing threadlist");
    my_pthread_t* pt;
      for ( pt = scd->threads->head; pt != NULL; pt=pt->next) {
          printf("thread %d is alive? %d\n",pt->id, pt->isDead );
      }*/
    scd->timer->it_value.tv_usec = 0;
    schedule();
}

void reschedule(){

  node* ptr;
  int i;
  int j;
  int k;

  int *arr = (int*)malloc((RUN_QUEUE_SIZE-1)*sizeof(int));

  for(i = 0; i < RUN_QUEUE_SIZE - 1; i++){

    arr[i] = 0;

    for(ptr = deQ(scd->runQ[i+1]); ptr != NULL; ptr = deQ(scd->runQ[i+1])){
      ptr->runtime = difftime(time(NULL), ptr->timeCreated);
      insertByTime(scd->promotionQ[i], ptr);
      arr[i]++;
    }

    for(j = 0; j < i + 2; j++){
      for(k = 0; k < arr[i]/(i+2); k++){
        node* ptr = deQ(scd->promotionQ[i]);
        enQ(scd->runQ[i - j], ptr);
      }
    }


  }
}

//scheduler context function
void schedule(){


    pause_timer(scd->timer);
    //printf("time left on timer = %d\n",scd->timer->it_value.tv_usec );
    scd->cycles++;

    if(scd->cycles > 100){
      scd->cycles = 0;
      reschedule();
    }

    //printf("cycles: %d\n",scd->cycles );
    node* justRun = NULL;

    //if(scd->timer->it_value.tv_usec > 0){
        //context finished within its allotted time and linked back to scheduler
        //or it yielded and has time left
        if(scd->current->status == YIELDING){
            //puts("someone yielded");
            scd->current->status = NEUTRAL;
            enQ(scd->runQ[scd->current->priority], scd->current);
        }else if(scd->current->status == EXITING){
            //puts("someone died");
            threadDied(scd->current->threadID);

            //check join list to see if anything can be added to runQ

        /*}else{
            puts("bad things happened");
        }*/
    }else{
        //context ran out of time and should be requeued
        demoteNode(scd->current);
    }

    justRun = scd->current;
    //printf("justRun is %d\n",justRun->threadID );

    node* nextNode = getNextNode();

    if(nextNode == NULL){
        //nothing left to run, only thing left to do is exit
        printf("job's done\n");
        return;
    }
    //printf("nextNode is %d\n",nextNode->threadID );
    /*
    node* ptr=NULL;
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

void terminationHandler(){
    while(1){
        scd->current->status = EXITING;
        schedule();
    }
}


//sets up all of the scheduler stuff
void initialize(){

    scd = (scheduler*) malloc(sizeof(scheduler));

    //yes this is probably the right way to do it but lets try hardcoding it

    int i = 0;
    for ( i = 0; i < RUN_QUEUE_SIZE; i++) {
        scd->runQ[i] = (queue*) malloc(sizeof(queue));
        scd->runQ[i]->head = NULL;
        scd->runQ[i]->rear = NULL;
        scd->runQ[i]->priorityLevel = i;

        if(i < RUN_QUEUE_SIZE - 1){
          scd->promotionQ[i] = (queue*) malloc(sizeof(queue));
          scd->promotionQ[i]->head = NULL;
          scd->promotionQ[i]->rear = NULL;
          scd->promotionQ[i]->priorityLevel = i;
        }
    }

    /*
    node* ptr=NULL;

    ptr = scd->runQ[0]->head;
    printf("0->head is %d\n",ptr );
    for(i = 0; i<5;i++){
      printf("queue #%d:",i );
      for(ptr = scd->runQ[i]->head;ptr != NULL; ptr=ptr->next){
        printf("%d ->",ptr->threadID );
      }
      printf("null\n" );
    }
*/

    scd->current = NULL;

    scd->timer = (struct itimerval*) malloc(sizeof(struct itimerval));

    scd->joinList = (list*) malloc(sizeof(list));
    scd->joinList->head = NULL;

    scd->threads = (threadList*) malloc(sizeof(threadList));
    scd->threads->head = NULL;

    //to do: make a mutex list struct that holds a list of my_pthread_mutex_t
    scd->mutexList;


    //call getcontext, setup the ucontext_t, then makecontext with scheduler func

    ucontext_t* ct = (ucontext_t*) malloc(sizeof(ucontext_t));
    getcontext(ct);
    ct->uc_stack.ss_sp = (char*) malloc(STACK_SIZE);
    ct->uc_stack.ss_size = STACK_SIZE;
    makecontext(ct, terminationHandler, 0);
    scd->termHandler = ct;

    scd->cycles = 0;


    ucontext_t* mainCxt = (ucontext_t*) malloc(sizeof(ucontext_t));
    getcontext(mainCxt);
    my_pthread_t* mainthread = (my_pthread_t*) malloc(sizeof(my_pthread_t));
    mainthread->id = -123456789;
    mainthread->isDead = 0;
    mainthread->exitArg = NULL;
    mainthread->next = NULL;
    node* mainNode = createNode(mainCxt, mainthread);

    //enQ(scd->runQ[0], mainNode);
    scd->current = mainNode;

    //set up signal and timer
    signal(SIGALRM, timerHandler);

    scd->timer->it_value.tv_usec = 50000;//50ms
    setitimer(ITIMER_REAL, scd->timer, NULL);
}




int my_pthread_create( my_pthread_t * thread, pthread_attr_t * attr, void *(*function)(void*), void * arg){
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

    pause_timer(scd->timer);

    ucontext_t* newCxt = (ucontext_t*) malloc(sizeof(ucontext_t));

    if(newCxt == NULL){
        unpause_timer(scd->timer);

        return 0;
    }

    getcontext(newCxt);
    newCxt->uc_stack.ss_sp = (char*) malloc(STACK_SIZE);

    if(newCxt->uc_stack.ss_sp == NULL){
        unpause_timer(scd->timer);
        return 0;
    }

    newCxt->uc_stack.ss_size = STACK_SIZE;
    newCxt->uc_link = scd->termHandler;

    union pointerConverter pc;
    pc.ptr = arg;

    //fix this to make it work
    makecontext(newCxt, function, 2, pc.arr[0], pc.arr[1]);

    thread = (my_pthread_t*)malloc(sizeof(my_pthread_t));
    thread->id = scd->threadNum;
    scd->threadNum++;
    //printf("thread id is %d\n",thread->id );
    thread->isDead = 0;
    thread->exitArg = NULL;
    thread->next = NULL;

    addThread(thread);


    node* newNode = createNode(newCxt, thread);

    //pause_timer(scd->timer);
    enQ(scd->runQ[0], newNode);
    unpause_timer(scd->timer);

    return 1;
}

//tekes the thread that called this function and requeues it at the end of the current priority queue
void my_pthread_yield(){

    if(scd == NULL){
        return;
    }

    pause_timer(scd->timer);
    scd->current->status = YIELDING;
    //unpause_timer(scd->timer);

    schedule();
}

//exits the thread that called this function and passes on value_ptr as an argument for whatever might join on this thread
void my_pthread_exit(void * value_ptr){
    if(scd == NULL){
        return;
    }

    //pause_timer(scd->timer);
    scd->current->status = EXITING;
    scd->current->threadID->exitArg = value_ptr;
    //unpause_timer(scd->timer);

    schedule();
}

void* testfunc(void* arg){

    printf("got here and my number is %d\n",*(int*)arg );
    my_pthread_yield();
    printf("got here again\n" );
    //printf("my thread id is %d\n",scd->current->threadID->id );
    long long int i = 0;
    long long int j=0;
    int c = 0;
    for (i = 0; i < 100000000; i++) {
      j++;
        if(j%10000000 == 0){
          printf("thread %d says is: %d\n",*(int*)arg,c++ );
          printf("thread's priority is %d\n", scd->current->priority );
          /*if(*(int*)arg == 1 && c > 5){
                printf("thread %d says IM GONNA EXIT\n",*(int*)arg);
                my_pthread_exit(NULL);
          }*/

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

  int a=1;
  int b=2;
  my_pthread_t* th;
  my_pthread_t* th2;

  my_pthread_create(th, (pthread_attr_t*)0, testfunc, (void*) &a );
  my_pthread_create(th2, (pthread_attr_t*)0, testfunc, (void*) &b );

  my_pthread_yield();
  printf("back from first yield\n" );
  my_pthread_yield();
  printf("back from second yield\n" );

  my_pthread_t* pt = NULL;
  for ( pt = scd->threads->head; pt != NULL; pt=pt->next) {
      printf("thread %d is alive? %d\n",pt->id, pt->isDead );
  }

  long long int i = 0;
  long long int j=0;
  int c = 0;
  for (i = 0; i < 1000000000; i++) {
    j++;
      if(j%10000000 == 0){
        printf("main says j is: %d\n",c++ );
        /*pt = NULL;
        for ( pt = scd->threads->head; pt != NULL; pt=pt->next) {
            printf("thread %d is alive? %d\n",pt->id, pt->isDead );
        }*/
        printf("main's priority is %d\n", scd->current->priority );
        if(c == 3 || c == 5){
            puts("yielding");
            my_pthread_yield();
        }
        //my_pthread_yield();
      }

  }


puts("printing runQ");
  node* ptr=NULL;

  ptr = scd->runQ[0]->head;
  printf("0->head is %d\n",ptr );
  for(i = 0; i<5;i++){
    printf("queue #%d:",i );
    for(ptr = scd->runQ[i]->head;ptr != NULL; ptr=ptr->next){
      printf("%d ->",ptr->threadID->id );
    }
    printf("null\n" );
  }

puts("printing threadlist");
  for ( pt = scd->threads->head; pt != NULL; pt=pt->next) {
      printf("thread %d is alive? %d\n",pt->id, pt->isDead );
  }

  for (i = 0; i < 1000000000; i++) {
    j++;
      if(j%10000000 == 0){
        printf("main says j is: %d\n",c++ );
        /*pt = NULL;
        for ( pt = scd->threads->head; pt != NULL; pt=pt->next) {
            printf("thread %d is alive? %d\n",pt->id, pt->isDead );
        }*/
        printf("main's priority is %d\n", scd->current->priority );
        //my_pthread_yield();
      }

  }



  return 0;
}
