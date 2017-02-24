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

  //list of nodes that have finished execution for use of nodes waiting on joins
  struct list_* deadList;
} scheduler;

typedef struct node_ {
    struct my_pthread_t_* threadID;
    ucontext_t* ut;
    int priority;
    int timeCreated;
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



/******************globals***********************/
scheduler* scd = NULL;
int currMutexID = -1;
mutex_list *mutexList = NULL;
//For testing the mutexes
int c = 0;
struct my_pthread_mutex_t_ *L1 = NULL;



/********************functions*******************/
//inits the mutex list that stores mutexes
void initMutexList(){
    if(mutexList != NULL){
  return;
    }

    mutexList= (mutex_list*) calloc(0, sizeof(mutex_list));
    mutexList->head = (my_pthread_mutex_t*) calloc(0, sizeof(my_pthread_mutex_t));
}


//takes a pointer to a context and a pthread_t and returns a pointer to a node
node* createNode(ucontext_t* context, my_pthread_t* thread){
    node* newNode = (node*) malloc(sizeof(node));
    newNode->threadID = thread;
    newNode->ut = context;
    newNode->next = NULL;
    newNode->priority = 0;
    newNode->timeCreated = 0;
    newNode->status = NEUTRAL;
    newNode->joinee = NULL;

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

//searches through the mutex list in the scheduler to see if the mutex lock has already been created
int existsInMutexList(int mutexToCheck) {
  my_pthread_mutex_t *ptr = mutexList->head;
  while (ptr != NULL) {
    if (ptr->mutexID == mutexToCheck) {
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

void insertToMutexList(my_pthread_mutex_t *newMutex) {
    if(mutexList->head == NULL){
  mutexList->head = newMutex;
    }

    newMutex->next = mutexList->head;
    mutexList->head = newMutex;
}

my_pthread_mutex_t* removeFromMutexList(int mID) {

    my_pthread_mutex_t* ptr = mutexList->head;
    my_pthread_mutex_t* prev = NULL;

    while(ptr != NULL && ptr->mutexID != mID){
      prev = ptr;
      ptr = ptr->next;
    }

    if(ptr != NULL && ptr->mutexID == mID){
      if(prev == NULL){
        my_pthread_mutex_t* result = ptr;
        mutexList->head = ptr->next;
        return result;
      }else{
        prev->next = ptr->next;
        return ptr;
      }
    }

    return NULL;
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
        result->next = NULL;
        return result;
      }else{
        prev->next = ptr->next;
        ptr->next = NULL;
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
    ////puts("times up");
    //printf("current's status is %d\n",scd->current->status );
    //printf("time left on timer = %d\n",scd->timer->it_value.tv_usec );

    /*printf("current thread id is %d\n",scd->current->threadID->id );
    //puts("printing runQ");
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

    //puts("printing threadlist");
    my_pthread_t* pt;
      for ( pt = scd->threads->head; pt != NULL; pt=pt->next) {
          printf("thread %d is alive? %d\n",pt->id, pt->isDead );
      }*/
    scd->timer->it_value.tv_usec = 0;
    schedule();
}

//scheduler context function
void schedule(){


    pause_timer(scd->timer);
    //puts("schedule time");
    //printf("time left on timer = %d\n",scd->timer->it_value.tv_usec );
    scd->cycles++;
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
        //puts("is joinlist->head null?");
        //check join list to see if anything can be added to runQ
        node* jptr = scd->joinList->head;
        while(jptr != NULL){
            if(jptr->joinee->id == scd->current->threadID->id){
                //puts("returning things");
                node* returnToQ = removeFromList(jptr->threadID, scd->joinList);
                enQ(scd->runQ[returnToQ->priority], returnToQ);
                jptr = scd->joinList->head;
            }else{
                //puts("movin on");
                jptr = jptr->next;
            }
        }
        //puts("probably");
    }else if(scd->current->status == JOINING){
        //puts("joining");
        //current is waiting on another running thread to finish
        //put current in joining queue and after every thread death check the join list if it should be requeued
        insertToList(scd->current, scd->joinList);

    }else{
        //puts("neutral");
        //context ran out of time and should be requeued
        demoteNode(scd->current);
    }

    justRun = scd->current;
    //printf("justRun is %d\n",justRun->threadID );
    //puts("are you failing here?");
    node* nextNode = getNextNode();
    //puts("are you failing here????????????");

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

        return 1;
    }

    getcontext(newCxt);
    newCxt->uc_stack.ss_sp = (char*) malloc(STACK_SIZE);

    if(newCxt->uc_stack.ss_sp == NULL){
        unpause_timer(scd->timer);
        return 1;
    }

    newCxt->uc_stack.ss_size = STACK_SIZE;
    newCxt->uc_link = scd->termHandler;

    union pointerConverter pc;
    pc.ptr = arg;

    //fix this to make it work
    makecontext(newCxt, function, 2, pc.arr[0], pc.arr[1]);

    my_pthread_t* newthread = (my_pthread_t*)malloc(sizeof(my_pthread_t));
    newthread = thread;
    newthread->id = scd->threadNum;
    scd->threadNum++;
    //printf("thread id is %d\n",thread->id );
    newthread->isDead = 0;
    newthread->exitArg = NULL;
    newthread->next = NULL;

    addThread(newthread);


    node* newNode = createNode(newCxt, thread);

    //pause_timer(scd->timer);
    enQ(scd->runQ[0], newNode);
    unpause_timer(scd->timer);

    return 0;
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

//joins calling thread on the thread in argument, and saves previous thread's exit argument to be pointed to by value_ptr
int my_pthread_join(my_pthread_t thread, void ** value_ptr){
    if(scd == NULL){
        //puts("asdf");
        return 1;
    }

    //my_pthread_t* threadptr = &thread;
/*
    if(!existsInThreadList(threadptr)){
        //puts("asdfasdf");
        return 1;
    }
*/
    my_pthread_t* ptr = scd->threads->head;
//puts("got here tho");
    while (ptr != NULL) {

      ////puts("well it wasnt null to start");
        if(ptr->id == /*threadptr->id*/thread.id){
            if(ptr->isDead == 1){
                //puts("found em");
                //thread to join on is dead, take its arg and return
                *value_ptr = ptr->exitArg;
                return 0;
            }else{
                //thread hasn't died yet, put this thread on a waitlist until that thread dies
                pause_timer(scd->timer);
                //puts("didnt found em");
                scd->current->status = JOINING;
                scd->current->joinee = ptr;
                schedule();

                *value_ptr = ptr->exitArg;
                return 0;
            }
        }
        ptr = ptr->next;
    }
    //puts("asdfasdfasdfasdfasdf");
    return 1;
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

    my_pthread_exit(arg);

}


//Just a note:
//p_thread_mutexattr_t will always be null/ignored, so the struct is there for compilation, but wont be malloc'd
int my_pthread_mutex_init(my_pthread_mutex_t *mutex, const pthread_mutexattr_t *mutexattr) {
  initMutexList();
  mutex = (my_pthread_mutex_t*)malloc(sizeof(my_pthread_mutex_t));


  mutex->mutexID = currMutexID++;
  mutex->isLocked = 0;
  mutex->next = NULL;

  insertToMutexList(mutex);
  
  //Successful, so returns 0
  printf("initialized mutex: \n", mutex->mutexID);
  return 0;
  
}

//locks the mutex, updates the list
//uses spinlocking with test and set implementation
int my_pthread_mutex_lock(my_pthread_mutex_t *mutex) {
  while(__sync_lock_test_and_set(&(mutex->isLocked), 1));
  printf("locked mutex: %d\n", mutex->mutexID);
  return 0;
}

//unlocks the mutex, updates the list
int my_pthread_mutex_unlock(my_pthread_mutex_t *mutex) {
  __sync_lock_release(&(mutex->isLocked), 1);
  mutex->isLocked = 0;
  printf("unlocked mutex: %d\n", mutex->mutexID);
  return 0;
}

//removes the mutex from the list of tracked mutexes by the scheduler and destroys it
int my_pthread_mutex_destroy(my_pthread_mutex_t *mutex) {
  printf("mutex %d\n", mutex->mutexID);
  my_pthread_mutex_t *mFromList = removeFromMutexList(mutex->mutexID);
  printf("mutex %d\n", mFromList->mutexID);  
  if(mFromList == NULL){
    printf("destroying failed\n");
    return -1;
  }
  mFromList->isLocked = 0;
  mFromList->next = NULL;
  mFromList->mutexID = -1;

  mFromList = NULL;
  mutex = NULL;
  return 0;
}


/*
/The following two funcitons are used to test mutexes
*/
void incrementOne(){
  int i = 0;
  for(i = 0; i < 100; i++){
    my_pthread_mutex_lock(&L1);
      c++;
    my_pthread_mutex_unlock(&L1);
  }

}

void incrementTwo(){
  int i = 0;
  for(i = 0; i < 100; i++){
    my_pthread_mutex_lock(&L1);
      c++;
    my_pthread_mutex_unlock(&L1);
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
  my_pthread_t th;
  my_pthread_t th2;

  my_pthread_create(&th, (pthread_attr_t*)0, testfunc, (void*) &a );
  my_pthread_create(&th2, (pthread_attr_t*)0, testfunc, (void*) &b );

  my_pthread_yield();
  printf("back from first yield\n" );
  my_pthread_yield();
  printf("back from second yield\n" );

  my_pthread_t* pt = NULL;
  for ( pt = scd->threads->head; pt != NULL; pt=pt->next) {
      printf("thread %d is alive? %d\n",pt->id, pt->isDead );
  }
  printf("th.id is %d, th2.id is %d\n",th.id,th2.id );

  int* rt1;
  int* rt2;
  int re1 = my_pthread_join(th,(void**)&rt1);
  int re2 = my_pthread_join(th2,(void**)&rt2);

  printf("re1 is %d, re2 is %d\n",re1,re2 );

  printf("rt1 is %d\n",*rt1 );
  printf("rt2 is %d\n",*rt2 );

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
            //puts("yielding");
            my_pthread_yield();
        }
        //my_pthread_yield();
      }

  }


//puts("printing runQ");
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

//puts("printing threadlist");
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
