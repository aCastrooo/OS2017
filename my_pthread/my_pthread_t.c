#include "my_pthread_t.h"

/******************globals***********************/
static scheduler* scd = NULL;
static int currMutexID = 0;
static mutex_list *mutexList = NULL;

/********************functions*******************/

//pause the timer for use in "blocking calls" so that if a
//function is using shared data (scheduler/queues/etc) it doesnt
//fuck with it
void pause_timer(struct itimerval* timer){

    struct itimerval zero;
    zero.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &zero, timer);
    scd->timer = timer;
}

//always unpause your timers after doing the sensitive "blocking" task
void unpause_timer(struct itimerval* timer){

    setitimer(ITIMER_REAL, timer, NULL);
}


//inits the mutex list that stores mutexes
void initMutexList(){
    if(mutexList != NULL){
        return;
    }

    mutexList = (mutex_list*) malloc(sizeof(mutex_list));
    mutexList->head = NULL;
}


//takes a pointer to a context and a pthread_t and returns a pointer to a node
node* createNode(ucontext_t* context, my_pthread_t* thread){
    node* newNode = (node*) malloc(sizeof(node));
    newNode->threadID = thread;
    newNode->ut = context;
    newNode->next = NULL;
    newNode->priority = 0;
    newNode->timeCreated = clock();
    newNode->status = NEUTRAL;
    newNode->runtime = 0;
    newNode->joinee = NULL;
    return newNode;
}


void insertByTime(queue* q, node* newNode){

  if(q->head == NULL){
    q->head = newNode;
    q->rear = newNode;
    q->head->next = NULL;
    q->rear->next = NULL;
    return;
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
  q->rear->next = NULL;
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

        q->rear->next = newNode;
        q->rear = newNode;
        q->rear->next = NULL;
        if(newNode->status != YIELDING){
            newNode->priority = q->priorityLevel;
        }
    }

}


//enqueues the node in the next lower priority queue if it can be demoted
void demoteNode(node* demotee){

    int newPriority = demotee->priority;

    if(demotee->priority < RUN_QUEUE_SIZE - 1){
        newPriority++;
    }

    enQ(scd->runQ[newPriority], demotee);
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


void reschedule(){

  node* ptr;
  int i;
  int j;
  int k;
  clock_t currTime = clock();

  int *arr = (int*)malloc((RUN_QUEUE_SIZE-1)*sizeof(int));

  for(i = 0; i < RUN_QUEUE_SIZE - 1; i++){

    arr[i] = 0;

    for(ptr = deQ(scd->runQ[i+1]); ptr != NULL; ptr = deQ(scd->runQ[i+1])){

      ptr->runtime = ((double)(currTime - ptr->timeCreated) / CLOCKS_PER_SEC);
      insertByTime(scd->promotionQ[i], ptr);
      arr[i]++;
    }
  }

  for(i = 0; i < RUN_QUEUE_SIZE - 1; i++){
    for(j = i + 2; j > 0; j--){
      if(j > 1){
        for(k = 0; k < arr[i]/(i+2); k++){
          ptr = deQ(scd->promotionQ[i]);
          if(ptr != NULL){
            enQ(scd->runQ[j - 1], ptr);
          }

        }
      }else{
        for(k = (arr[i]/(i+2))*(i + 1); k < arr[i]; k++){
          ptr = deQ(scd->promotionQ[i]);
          if(ptr != NULL){
            enQ(scd->runQ[j - 1], ptr);
          }

        }
      }
    }
  }
  free(arr);
  arr = NULL;
}



//scheduler context function
void schedule(){


    pause_timer(scd->timer);

    scd->cycles++;
    /*
    //uncomment this block if you would like to see our benchmark function in action
    if(scd->cycles % 10 == 0){
      benchmark();
    }
    */
    if(scd->cycles > 100){
      scd->cycles = 0;
      reschedule();
    }

    node* justRun = NULL;


        //context finished within its allotted time and linked back to scheduler
        //or it yielded and has time left
    if(scd->current->status == YIELDING){

        int requeuepriority = scd->current->priority;
        enQ(scd->runQ[requeuepriority], scd->current);
        scd->current->status = NEUTRAL;
    }else if(scd->current->status == EXITING){
        threadDied(scd->current->threadID);

        node* jptr = scd->joinList->head;
        while(jptr != NULL){
            if(jptr->joinee->id == scd->current->threadID->id){
                node* returnToQ = removeFromList(jptr->threadID, scd->joinList);
                enQ(scd->runQ[returnToQ->priority], returnToQ);
                jptr = scd->joinList->head;
            }else{
                jptr = jptr->next;
            }
        }
    }else if(scd->current->status == JOINING){

        //current is waiting on another running thread to finish
        //put current in joining queue and after every thread death check the join list if it should be requeued
        insertToList(scd->current, scd->joinList);

    }else{
        //context ran out of time and should be requeued
        demoteNode(scd->current);
    }

    justRun = scd->current;

    node* nextNode = getNextNode();

    if(nextNode == NULL){
        //nothing left to run, only thing left to do is exit
        return;
    }

    scd->current = nextNode;

    //run time is 50ms * (level of priority + 1)
    scd->timer->it_value.tv_usec = QUANTA_TIME * 1000 * (scd->current->priority + 1);
    setitimer(ITIMER_REAL, scd->timer, NULL);

    if(scd->current->threadID->id != justRun->threadID->id){
        swapcontext(justRun->ut, scd->current->ut);
    }


}


//alarm signal handler that will set to the scheduler context which will change what is running
void timerHandler(int signum){

    scd->timer->it_value.tv_usec = 0;
    schedule();
}

void terminationHandler(){
    while(1){
        scd->current->status = EXITING;
        schedule();
    }
}

void benchmark(){
  clock_t currTime = clock();
  double runtime = ((double)(currTime - scd->start_time)/CLOCKS_PER_SEC);
  printf("runtime of the program is: %f\n", runtime);
}

//sets up all of the scheduler stuff
void initialize(){

    scd = (scheduler*) malloc(sizeof(scheduler));

    scd->start_time = clock();

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

    scd->current = mainNode;

    //set up signal and timer
    signal(SIGALRM, timerHandler);

    scd->timer->it_value.tv_usec = QUANTA_TIME * 1000;//50ms
    setitimer(ITIMER_REAL, scd->timer, NULL);
}




int my_pthread_create( my_pthread_t * thread, my_pthread_attr_t * attr, void *(*function)(void*), void * arg){
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

    makecontext(newCxt, (void (*) (void))function, 1, arg);

    my_pthread_t* newthread = (my_pthread_t*)malloc(sizeof(my_pthread_t));
    newthread = thread;
    newthread->id = scd->threadNum;
    scd->threadNum++;
    newthread->isDead = 0;
    newthread->exitArg = NULL;
    newthread->next = NULL;

    addThread(newthread);


    node* newNode = createNode(newCxt, thread);

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

    schedule();
}

//exits the thread that called this function and passes on value_ptr as an argument for whatever might join on this thread
void my_pthread_exit(void * value_ptr){
    if(scd == NULL){
        return;
    }

    scd->current->status = EXITING;
    scd->current->threadID->exitArg = value_ptr;

    schedule();
}

//joins calling thread on the thread in argument, and saves previous thread's exit argument to be pointed to by value_ptr
int my_pthread_join(my_pthread_t thread, void ** value_ptr){
    if(scd == NULL){
        return 1;
    }


    my_pthread_t* ptr = scd->threads->head;
    while (ptr != NULL) {

        if(ptr->id == thread.id){
            if(ptr->isDead == 1){

                //thread to join on is dead, take its arg and return

                if(value_ptr != NULL){
                  *value_ptr = ptr->exitArg;

                }

                return 0;
            }else{
                //thread hasn't died yet, put this thread on a waitlist until that thread dies
                pause_timer(scd->timer);
                scd->current->status = JOINING;
                scd->current->joinee = ptr;
                schedule();

                if(value_ptr != NULL){
                  *value_ptr = ptr->exitArg;

                }

                return 0;
            }
        }
        ptr = ptr->next;
    }
    return 1;
}


//Just a note:
//p_thread_mutexattr_t will always be null/ignored, so the struct is there for compilation, but wont be malloc'd
int my_pthread_mutex_init(my_pthread_mutex_t *mutex, const my_pthread_mutexattr_t *mutexattr) {
  initMutexList();

  my_pthread_mutex_t *newMutex = (my_pthread_mutex_t*)malloc(sizeof(my_pthread_mutex_t));

  newMutex = mutex;
  newMutex->mutexID = ++currMutexID;
  newMutex->isInit = 1;
  newMutex->next = NULL;
  newMutex->isLocked = 0;

/*
  mutex->mutexID = currMutexID++;
  mutex->isLocked = 0;
  mutex->next = NULL;
*/
  insertToMutexList(mutex);

  //Successful, so returns 0
  return 0;

}

//locks the mutex, updates the list
//uses spinlocking with test and set implementation
int my_pthread_mutex_lock(my_pthread_mutex_t *mutex) {
  if(mutex->isInit != 1 && existsInMutexList(mutex->mutexID)){
    return -1;
  }

  while(__sync_lock_test_and_set(&(mutex->isLocked), 1));
  return 0;
}

//unlocks the mutex, updates the list
int my_pthread_mutex_unlock(my_pthread_mutex_t *mutex) {
  __sync_lock_release(&(mutex->isLocked), 1);
  mutex->isLocked = 0;
  return 0;
}

//removes the mutex from the list of tracked mutexes by the scheduler and destroys it
int my_pthread_mutex_destroy(my_pthread_mutex_t *mutex) {
  my_pthread_mutex_t *mFromList = removeFromMutexList(mutex->mutexID);
  if(mFromList == NULL){
    return -1;
  }
  mFromList->isLocked = 0;
  mFromList->next = NULL;
  mFromList->mutexID = -1;

  mFromList = NULL;
  mutex = NULL;
  return 0;
}
