/************Group Members**********/
//Daniel Schley (drs218)
//Anthony Castronuovo (ajc320)
//Thanassi Natsis (tmn61)
//ilab machines used: cd.cs.rutgers.edu
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

  //number of threads created for use in making thread id's
  int threadNum;

  //sorts the nodes in order of time created then re-enQ nodes to runQ with updated priorityLevel
  struct queue_* promotionQ[RUN_QUEUE_SIZE - 1];

  //start time of the scheduler
  time_t start_time;

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

typedef struct mutex_list_ {
  struct my_pthread_mutex_t_ *head;
}mutex_list;

typedef struct Block {
    bool isFree;
    unsigned int size; // We use short instead of size_t to save space
    struct Block* next;
} Block;

typedef struct Page_ {
    int threadID;
    int pageID;
    int sizeLeft;
    struct Page_* next;
} Page;

/******************globals***********************/
static scheduler* scd = NULL;
static bool timerSet = false;
static int currMutexID = 0;
static mutex_list *mutexList = NULL;

static const unsigned short BLOCK_SIZE = sizeof(Block);
static char memory[MAX_MEMORY];
static bool firstMalloc = true;
static bool firstThreadMalloc = true;
static Block* rootBlock;

static char* userSpace = NULL;
static int pageNum = 0;
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

    mutexList = (mutex_list*) myallocate(sizeof(mutex_list), __FILE__, __LINE__, LIBRARYREQ);
    mutexList->head = NULL;
}


//takes a pointer to a context and a pthread_t and returns a pointer to a node
node* createNode(ucontext_t* context, my_pthread_t* thread){
    node* newNode = (node*) myallocate(sizeof(node), __FILE__, __LINE__, LIBRARYREQ);
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

/*
maintenance on the run queue that runs every 100 cycles
in the 2nd level queue, moves 1/2 of the oldest threads up and other 1/2 remain
in 3rd level, moves oldest 1/3 up 2 levels, 2nd oldest 1/3 up 1 level,
last 1/3 remains, and so on...
*/
void maintenance(){

  node* ptr;
  int i;
  int j;
  int k;
  clock_t currTime = clock();

  int *arr = (int*)myallocate((RUN_QUEUE_SIZE-1)*sizeof(int), __FILE__, __LINE__, LIBRARYREQ);

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

void benchmark(){
  clock_t currTime = clock();
  double runtime = ((double)(currTime - scd->start_time)/CLOCKS_PER_SEC);
  printf("runtime of the program is: %f\n", runtime);
}

//scheduler context function
void schedule(){


    pause_timer(scd->timer);

    scd->cycles++;
    /*
    //uncomment this block if you would like to see our benchmark function in action
    if(scd->cycles % NUM_CYCLES == 0){
      benchmark();
    }
    */
    if(scd->cycles > 100){
      scd->cycles = 0;
      maintenance();
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


//sets up all of the scheduler stuff
void initialize(){

    scd = (scheduler*) myallocate(sizeof(scheduler), __FILE__, __LINE__, LIBRARYREQ);

    scd->start_time = clock();

    //yes this is probably the right way to do it but lets try hardcoding it

    int i = 0;
    for ( i = 0; i < RUN_QUEUE_SIZE; i++) {
        scd->runQ[i] = (queue*) myallocate(sizeof(queue), __FILE__, __LINE__, LIBRARYREQ);
        scd->runQ[i]->head = NULL;
        scd->runQ[i]->rear = NULL;
        scd->runQ[i]->priorityLevel = i;

        if(i < RUN_QUEUE_SIZE - 1){
          scd->promotionQ[i] = (queue*) myallocate(sizeof(queue), __FILE__, __LINE__, LIBRARYREQ);
          scd->promotionQ[i]->head = NULL;
          scd->promotionQ[i]->rear = NULL;
          scd->promotionQ[i]->priorityLevel = i;
        }
    }


    scd->current = NULL;

    scd->timer = (struct itimerval*) myallocate(sizeof(struct itimerval), __FILE__, __LINE__, LIBRARYREQ);

    scd->joinList = (list*) myallocate(sizeof(list), __FILE__, __LINE__, LIBRARYREQ);
    scd->joinList->head = NULL;

    scd->threads = (threadList*) myallocate(sizeof(threadList), __FILE__, __LINE__, LIBRARYREQ);
    scd->threads->head = NULL;

    scd->threadNum = 2;

    //call getcontext, setup the ucontext_t, then makecontext with scheduler func

    ucontext_t* ct = (ucontext_t*) myallocate(sizeof(ucontext_t), __FILE__, __LINE__, LIBRARYREQ);
    getcontext(ct);
    ct->uc_stack.ss_sp = (char*) myallocate(STACK_SIZE, __FILE__, __LINE__, LIBRARYREQ);
    ct->uc_stack.ss_size = STACK_SIZE;
    makecontext(ct, terminationHandler, 0);
    scd->termHandler = ct;

    scd->cycles = 0;

    ucontext_t* mainCxt = (ucontext_t*) myallocate(sizeof(ucontext_t), __FILE__, __LINE__, LIBRARYREQ);
    getcontext(mainCxt);

    my_pthread_t* mainthread = (my_pthread_t*) myallocate(sizeof(my_pthread_t), __FILE__, __LINE__, LIBRARYREQ);
    mainthread->id = 1;
    mainthread->isDead = 0;
    mainthread->exitArg = NULL;
    mainthread->next = NULL;
    node* mainNode = createNode(mainCxt, mainthread);

    scd->current = mainNode;

    //set up signal and timer
    signal(SIGALRM, timerHandler);

    timerSet = true;
    scd->timer->it_value.tv_usec = QUANTA_TIME * 1000;//50ms
    setitimer(ITIMER_REAL, scd->timer, NULL);
}




int my_pthread_create( my_pthread_t * thread, my_pthread_attr_t * attr, void *(*function)(void*), void * arg){


    if(scd == NULL){
        initialize();
    }

    pause_timer(scd->timer);

    ucontext_t* newCxt = (ucontext_t*) myallocate(sizeof(ucontext_t), __FILE__, __LINE__, LIBRARYREQ);

    if(newCxt == NULL){
        unpause_timer(scd->timer);

        return 1;
    }

    getcontext(newCxt);
    //newCxt->uc_stack.ss_sp = (char*) myallocate(STACK_SIZE, __FILE__, __LINE__, LIBRARYREQ);
    newCxt->uc_stack.ss_sp = (char*) calloc(1,STACK_SIZE);

    if(newCxt->uc_stack.ss_sp == NULL){
        unpause_timer(scd->timer);
        return 1;
    }

    newCxt->uc_stack.ss_size = STACK_SIZE;
    newCxt->uc_link = scd->termHandler;

    makecontext(newCxt, (void (*) (void))function, 1, arg);

    my_pthread_t* newthread = (my_pthread_t*)myallocate(sizeof(my_pthread_t), __FILE__, __LINE__, LIBRARYREQ);
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

  my_pthread_mutex_t *newMutex = (my_pthread_mutex_t*)myallocate(sizeof(my_pthread_mutex_t), __FILE__, __LINE__, LIBRARYREQ);

  newMutex = mutex;
  newMutex->mutexID = ++currMutexID;
  newMutex->isInit = 1;
  newMutex->next = NULL;
  newMutex->isLocked = 0;


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
  mFromList->isInit = 0;
  mFromList = NULL;
  mutex = NULL;
  return 0;
}




/**
 * Initialize the root block. This is only called the first time that mymalloc is used.
 */
static void initializeRoot() {
    rootBlock = (Block*) memory;
    rootBlock->isFree = true;
    rootBlock->size = (100 * PAGESIZE) - BLOCK_SIZE;
    rootBlock->next = NULL;
    firstMalloc = false;
}

static void initializePage(Page* pg){

    if(scd == NULL){
        pg->threadID = 1;
    }else{
        pg->threadID = scd->current->threadID->id;
    }

    pg->pageID = pageNum;
    pageNum++;
    pg->sizeLeft = PAGESIZE - sizeof(Page);
    pg->next = NULL;

    Block* rootBlock = (Block*) ((char*) pg + sizeof(Page));
    rootBlock->isFree = true;
    rootBlock->size = PAGESIZE - sizeof(Page) - BLOCK_SIZE;
    rootBlock->next = NULL;
}


/**
 * Performs a basic check to ensure that the pointer address is contained
 * inside memory. It does not verify that all of the promised space is in memory,
 * nor does it verify that the address is actually correct (e.g. a Block pointer).
 *
 * @param ptr Check if this pointer points to an address in memory.
 * @return true if ptr is in memory, false otherwise.
 */
static bool inMemorySpace(Block* ptr) {
    return (char*) ptr >= &memory[0] && (char*) ptr < &memory[100 * PAGESIZE];
}


/**
 * Free the block and merge it with its neighbors if they are free as well. Iterates
 * over the entire list of blocks until the correct block is found, while keeping
 * track of the previous block. When the correct block is found, it is marked as free.
 * If the previous and/or next blocks are free, they will be merged into one.
 *
 * @param toFree the Block to be freed.
 * @return true if toFree was freed, false otherwise.
 */
static bool freeAndMerge(Block* toFree) {

    Block* previous = NULL;
    Block* current = rootBlock;

    // Iterate through the list until toFree is found.
    do {

        if (current == toFree) {

            current->isFree = true;
            Block* next = current->next;

            // Merge current with next if next is free.
            if (next != NULL && next->isFree) {
                current->size += next->size + BLOCK_SIZE;
                current->next = next->next;
            }

            // Merge previous with current if previous is free.
            if (previous != NULL && previous->isFree) {
                previous->size += current->size + BLOCK_SIZE;
                previous->next = current->next;
            }

            return true;
        }

        else {
            previous = current;
        }

    } while ((current = current->next) != NULL);

    // Return false if toFree was not found.
    return false;
}


/**
 * Allocates memory in a Block, if possible, and returns a pointer to the position of the block
 * that contains the memory. The first 16 bits of the memory chunk are used to store the
 * information of the Block. The Blocks are chained together in a linked list, and new blocks
 * are added with a first-fit placement strategy.
 *
 * @param file defined to be __FILE__, used to print messages when an error is detected.
 * @param line defined to be __LINE__, used to print messages when an error is detected.
 * @return void pointer to memory in memory
 */
void* myallocate(size_t size, const char* file, int line, int caller) {

    if(timerSet){
        pause_timer(scd->timer);
    }

    // If it is the first time this function has been called, then initialize the root block.
    if (firstMalloc) {
        userSpace = &memory[100 * PAGESIZE];

        initializeRoot();
    }

    Block* current;
    const unsigned short sizeWithBlock = size + BLOCK_SIZE; // Include extra space for metadata.


    if(caller == LIBRARYREQ){
        //do normal malloc stuff

            current = rootBlock;

            // First fit placement strategy.
            do {

                // Look for free block with enough space.
                if (!current->isFree || current->size < size) {
                    continue;
                }

                else if (current->isFree) {

                    if (current->size == size) {
                        // Mark current block as taken and return it.
                        current->isFree = false;

                        if(timerSet){
                            unpause_timer(scd->timer);
                        }

                        return ((char*) current) + BLOCK_SIZE;
                    }

                    // If a free block has more than enough space, create new free block to take up the rest of the space.
                    else if (current->size >= sizeWithBlock) {

                        Block* newBlock = (Block*) ((char*) current + sizeWithBlock);

                        newBlock->isFree = true;
                        newBlock->size = current->size - sizeWithBlock;
                        newBlock->next = current->next;

                        current->next = newBlock;
                        current->size = size;

                        // Mark current block as taken and return it.
                        current->isFree = false;

                        if(timerSet){
                            unpause_timer(scd->timer);
                        }

                        return ((char*) current) + BLOCK_SIZE;
                    }

                    /* NOTE: If current->size is greater than size, but less than sizeWithBlock,
                     * then there is not enough room to accommodate both the space and a new Block,
                     * so we continue the search. */
                }

            } while ((current = current->next) != NULL);

            // If no suitable free block is found, print an error message and return NULL pointer.
            printf("Error at line %d of %s: not enough space is available to allocate.\n", line, file);

            if(timerSet){
                unpause_timer(scd->timer);
            }

            return NULL;
    }else{
        //do page stuff for threads

        Page* prev = NULL;
        Page* pg = (Page*) userSpace;
        int thread = (scd == NULL) ? 1 : scd->current->threadID->id;

        if(firstThreadMalloc){
            initializePage(pg);
            firstThreadMalloc = false;
        }

        //find the page that belongs to this thread
        while (pg != NULL) {
            if(pg->threadID == thread){
                break;
            }

            prev = pg;
            pg = pg->next;
        }

        if(pg == NULL){
            //thread does not have a page in memory so make one if theres enough space
            //or find a page that has been freed already

            //search again for first free page if there is one available
            prev = NULL;
            pg = (Page*) userSpace;

            while (pg != NULL) {
                if(pg->sizeLeft == PAGESIZE - sizeof(Page)){
                    break;
                }

                prev = pg;
                pg = pg->next;
            }

            if(pg == NULL){
                //there were no empty pages to write over
                if((char*)pg + sizeof(Page) > &memory[MAX_MEMORY]){
                    printf("Error at line %d of %s: not enough space is available to allocate.\n", line, file);
                    return NULL;
                }
                pg =(Page*) ((char*) prev + sizeof(Page));
                initializePage(pg);
                prev->next = pg;
            }else{
                //write over the currently free page pg
                pg->threadID = thread;
                pg->pageID = pageNum;
                pageNum++;
                pg->sizeLeft = PAGESIZE - sizeof(Page);

                Block* rootBlock = (Block*) ((char*) pg + sizeof(Page));
                rootBlock->isFree = true;
                rootBlock->size = PAGESIZE - sizeof(Page) - BLOCK_SIZE;
                rootBlock->next = NULL;
            }

        }

        current = (Block*) ((char*) pg + sizeof(Page));

        do {

            // Look for free block with enough space.
            if (!current->isFree || current->size < size) {
                continue;
            }

            else if (current->isFree) {

                if (current->size == size) {
                    // Mark current block as taken and return it.
                    current->isFree = false;

                    if(timerSet){
                        unpause_timer(scd->timer);
                    }

                    return ((char*) current) + BLOCK_SIZE;
                }

                // If a free block has more than enough space, create new free block to take up the rest of the space.
                else if (current->size >= sizeWithBlock) {

                    Block* newBlock = (Block*) ((char*) current + sizeWithBlock);

                    newBlock->isFree = true;
                    newBlock->size = current->size - sizeWithBlock;
                    newBlock->next = current->next;

                    current->next = newBlock;
                    current->size = size;

                    // Mark current block as taken and return it.
                    current->isFree = false;

                    if(timerSet){
                        unpause_timer(scd->timer);
                    }

                    return ((char*) current) + BLOCK_SIZE;
                }

                /* NOTE: If current->size is greater than size, but less than sizeWithBlock,
                 * then there is not enough room to accommodate both the space and a new Block,
                 * so we continue the search. */
            }

        } while ((current = current->next) != NULL);

        // If no suitable free block is found, print an error message and return NULL pointer.
        printf("Error at line %d of %s: not enough space is available to allocate.\n", line, file);

        if(timerSet){
            unpause_timer(scd->timer);
        }

        return NULL;

    }

}


/**
 * Checks if the block is eligible to be freed, and frees it if it is.
 */
void mydeallocate(void* ptr, const char* file, int line, int caller) {
    if(caller == LIBRARYREQ){
        //called from library
        if (!inMemorySpace(ptr)) {
            printf("Error at line %d of %s: pointer was not created using malloc.\n", line, file);
        }

        else {

            Block* block = (Block*) ((char*) ptr - BLOCK_SIZE);

            if (block->isFree) {
                printf("Error at line %d of %s: pointer has already been freed.\n", line, file);
            }

            else if (!freeAndMerge(block)) {
                printf("Error at line %d of %s: pointer was not created using malloc.\n", line, file);
            }
        }
    }else{
        //called from thread

    }


}

void* test(void* arg){
    printf("got here\n" );
/*
    int v = (int) *arg;

    int* x = (int*) malloc(sizeof(int));
    *x = v;
    printf("I am thread %d and my number is %d\n",*arg, *x );
    my_pthread_yield();
    printf("I am still thread %d and my number is %d\n",*arg, *x );
*/
    return NULL;
}

int main(){
  my_pthread_t th[10];
  int i;
  for ( i = 0; i < 10; i++) {
      //int* x = (int*) malloc(sizeof(int));
      //*x = i;
      my_pthread_create(&th[i], NULL,test,NULL);
      printf("thread #%d made\n",i );
  }

  while (1) {
    i++;
  }
  return 0;
}
