/************Group Members**********/
//Daniel Schley (drs218)
//Anthony Castronuovo (ajc320)
//Thanassi Natsis (tmn61)
//ilab machines used: cd.cs.rutgers.edu
#include "my_pthread_t.h"


/**
NOTE:FIXME: time quanta changed to 100ms because threads were timing out when it came to allocations and swapping with a real file.
in addition, rather than opening and closing every time we want to read/write to a file, we made it so that it opens once at the beginning
and closes at exit.  we may want to change this back to opening/closing every time we want to read/write but its not TOO important.

**/

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

/******************globals***********************/
static scheduler* scd = NULL;
static bool timerSet = false;
static int currMutexID = 0;
static mutex_list *mutexList = NULL;
FILE *swpFile;

static const unsigned short BLOCK_SIZE = sizeof(unsigned int);
static const unsigned int MAX_MEMORY = 8388608;
static const unsigned int DISK_MEMORY = 16777216;
static const unsigned short STACK_SIZE = 18192;
static char* memory;
//static char* disk;
static bool firstMalloc = true;
static unsigned int* rootBlock;
static unsigned int* rootBlockD;

static char* userSpace = NULL;
//static char* userSpaceDisk = NULL;
static int mainPageNum = 0;
static unsigned int* pages;//this will be the array of pages in memory block
static unsigned int* diskPages;
/********************functions*******************/


unsigned int setBlockSize(unsigned int block, unsigned int size){
  unsigned int temp = block & 0x000000FF;
  block = (size << 8) | temp;
  return block;
}

unsigned int getBlockSize(unsigned int block){
  return (block >> 8) & 0x00FFFFFF;
}

unsigned int setBlockIsNext(unsigned int block, unsigned int isNext){
  unsigned int temp = block & 0xFFFFFF0F;
  block = (isNext << 4) | temp;
  return block;
}

unsigned int getBlockIsNext(unsigned int block){
  return (block >> 4) & 0x0000000F;
}

unsigned int setBlockIsF(unsigned int block, unsigned int isF){
  unsigned int temp = block & 0xFFFFFFF0;
  block = temp | isF;
  return block;
}

unsigned int getBlockIsF(unsigned int block){
  return block & 0x0000000F;
}

unsigned int* getNextBlock(unsigned int* block){
  unsigned int isNext = getBlockIsNext(*block);
  if(isNext == 0){
    return NULL;
  }
  unsigned int size = getBlockSize(*block);
  return (unsigned int*)((char*)block + size + BLOCK_SIZE);
}

unsigned int setPagePgID(unsigned int pg, unsigned int id){

    unsigned int temp = pg & 0x0000FFFF;
    pg = (id << 16) | temp;
    return pg;

}

unsigned int getPagePgID(unsigned int pg){

    return (pg >> 16) & 0x0000FFFF;

}

unsigned int setPageThID(unsigned int pg, unsigned int id){

    unsigned int temp = pg & 0xFFFF00FF;
    pg = (id << 8) | temp;
    return pg;

}

unsigned int getPageThID(unsigned int pg){

    return (pg >> 8) & 0x000000FF;

}

unsigned int setPageIsF(unsigned int pg, unsigned int free){

    pg = pg & 0xFFFFFF00;
    pg = pg | free;
    return pg;

}

unsigned int getPageIsF(unsigned int pg){

    return pg & 0x000000FF;

}


//deletes the swpfile on program exit
void deleteFile(){
  fclose(swpFile);
	int done = remove("/home/csuser/OS2017/assignment3/example/mountdir/swpfile");
	if(!done){
		//puts("file deleted");
	}
}





//pause the timer for 1use in "blocking calls" so that if a
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
  mydeallocate(arr, __FILE__, __LINE__, LIBRARYREQ);
  arr = NULL;
}

void benchmark(){
  clock_t currTime = clock();
  double runtime = ((double)(currTime - scd->start_time)/CLOCKS_PER_SEC);
  printf("runtime of the program is: %f\n", runtime);
}


// Protects all pages belonging to the incoming threadID
void protectAllPages(int threadID){
	int i;
	for(i = 0; i < (MAX_MEMORY / PAGESIZE) - LIBPAGES; i++){
		if(getPageThID(pages[i]) == threadID){
			mprotect(userSpace + (i * PAGESIZE), PAGESIZE, PROT_NONE);
		}
	}
}


void freePages(){

  int i;
  for(i = 0; i < (MAX_MEMORY / PAGESIZE) - LIBPAGES; i++){
    if(getPageThID(pages[i]) == scd->current->threadID->id){
      mprotect(userSpace + (PAGESIZE * i), PAGESIZE, PROT_READ | PROT_WRITE);
      pages[i] = setPageIsF(pages[i], 1);
    }
  }

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
        freePages();
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
	 protectAllPages(justRun->threadID->id);

        swapcontext(justRun->ut, scd->current->ut);

    }

}


//alarm signal handler that will set to the scheduler context which will change what is running
void timerHandler(int signum){
    scd->timer->it_value.tv_usec = 0;
    schedule();
}

void terminationHandler(){
      pause_timer(scd->timer);
      scd->current->status = EXITING;
      unpause_timer(scd->timer);

      schedule();

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
    mainthread->pageNum = mainPageNum;
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
    newCxt->uc_stack.ss_sp = (char*) myallocate(STACK_SIZE, __FILE__, __LINE__, LIBRARYREQ);

    if(newCxt->uc_stack.ss_sp == NULL){
        unpause_timer(scd->timer);
        return 1;
    }

    newCxt->uc_stack.ss_size = STACK_SIZE;
    newCxt->uc_link = scd->termHandler;

    makecontext(newCxt, (void (*) (void))function, 1, arg);

    my_pthread_t* newthread;//= (my_pthread_t*)myallocate(sizeof(my_pthread_t), __FILE__, __LINE__, LIBRARYREQ);

    newthread = thread;
    newthread->id = scd->threadNum;
    scd->threadNum++;
    newthread->isDead = 0;
    newthread->exitArg = NULL;
    newthread->pageNum = 0;
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
    unpause_timer(scd->timer);
    schedule();
}

//exits the thread that called this function and passes on value_ptr as an argument for whatever might join on this thread
void my_pthread_exit(void * value_ptr){
    if(scd == NULL){
        return;
    }
    pause_timer(scd->timer);
    scd->current->status = EXITING;
    scd->current->threadID->exitArg = value_ptr;
    unpause_timer(scd->timer);

    schedule();
}

//joins calling thread on the thread in argument, and saves previous thread's exit argument to be pointed to by value_ptr
int my_pthread_join(my_pthread_t thread, void ** value_ptr){
    if(scd == NULL){
        return 1;
    }
    pause_timer(scd->timer);

    my_pthread_t* ptr = scd->threads->head;
    while (ptr != NULL) {

        if(ptr->id == thread.id){
            if(ptr->isDead == 1){

                //thread to join on is dead, take its arg and return

                if(value_ptr != NULL){
                  *value_ptr = ptr->exitArg;

                }
                unpause_timer(scd->timer);
                return 0;
            }else{
                //thread hasn't died yet, put this thread on a waitlist until that thread dies
                scd->current->status = JOINING;
                scd->current->joinee = ptr;
                unpause_timer(scd->timer);
                schedule();
                pause_timer(scd->timer);
                if(value_ptr != NULL){
                  *value_ptr = ptr->exitArg;

                }
                unpause_timer(scd->timer);
                return 0;
            }
        }
        ptr = ptr->next;
    }
    unpause_timer(scd->timer);
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

	// swaps two pages in memory and in the page table that corresponds to those pages
	void pageSwap(int initial, int swapTo){
    		if(initial == swapTo){
        		return;
    		}

		char temp[PAGESIZE];

		//un-protect so we can write to the new spots
		mprotect(userSpace + (PAGESIZE * swapTo), PAGESIZE, PROT_READ | PROT_WRITE);
		mprotect(userSpace + (PAGESIZE * initial), PAGESIZE, PROT_READ | PROT_WRITE);

		// swap mem
		memcpy(temp, userSpace + (PAGESIZE * swapTo), PAGESIZE);
		memcpy(userSpace + (PAGESIZE * swapTo), userSpace + (PAGESIZE * initial), PAGESIZE);
		memcpy(userSpace + (PAGESIZE * initial), temp, PAGESIZE);

		// swap page table data
		unsigned int tempPage;

		tempPage = pages[swapTo];
		pages[swapTo] = pages[initial];
		pages[initial] = tempPage;


		//protect the memory pages again
		mprotect(userSpace + (PAGESIZE * swapTo), PAGESIZE, PROT_NONE);
		mprotect(userSpace + (PAGESIZE * initial), PAGESIZE, PROT_NONE);

	}

	void swapToMemFromDisk(int inMem, int fromDisk){

		pause_timer(scd->timer);


		char temp[PAGESIZE];
		//what is coming from the file
		char fromFile[PAGESIZE];
    		/////swpFile = fopen("swpfile", "w+");
		//move to the part of the file where the page is stored
		fseek(swpFile, (PAGESIZE * fromDisk), SEEK_SET);
		//fread(fromFile, PAGESIZE, 1, swpFile);
    fread(fromFile, 1, PAGESIZE, swpFile);

		//un-protect the page in memory that will be swapped
		mprotect(userSpace + (PAGESIZE * inMem), PAGESIZE, PROT_READ | PROT_WRITE);

		// swap mem
		memcpy(temp, fromFile, PAGESIZE);
		memcpy(fromFile, userSpace + (PAGESIZE * inMem), PAGESIZE);
		memcpy(userSpace + (PAGESIZE * inMem), temp, PAGESIZE);

		//Write to the new spot in the file
		fseek(swpFile, (PAGESIZE * fromDisk), SEEK_SET);
		//fwrite(fromFile, PAGESIZE, 1, swpFile);
    fwrite(fromFile, 1, PAGESIZE, swpFile);
		// swap page table data
		unsigned int tempPage;

		tempPage = diskPages[fromDisk];
		diskPages[fromDisk] = pages[inMem];
		pages[inMem] = tempPage;


		//protect the pages
		mprotect(userSpace + (PAGESIZE * inMem), PAGESIZE, PROT_NONE);

		unpause_timer(scd->timer);
    		/////fclose(swpFile);
		/////swpFile = -1;
	}


	static void alignPages(){
	    int thread = (scd == NULL) ? 1 : scd->current->threadID->id;
    	    int realIndex;
	    int i;
	    for (i = 0; i < (MAX_MEMORY / PAGESIZE) - LIBPAGES; i++) {
    		  if(getPageThID(pages[i]) == thread && i == getPagePgID(pages[i])){
         	     realIndex = getPagePgID(pages[i]);
         	     pageSwap(i, realIndex);
         	     mprotect(userSpace + (PAGESIZE * realIndex), PAGESIZE, PROT_READ | PROT_WRITE);
    		  }
      	    }


	    //once disk is in place, now check disk if there are any pages in disk to be aligned into memory
	    for(i = 0; i < (DISK_MEMORY / PAGESIZE); i++){
      		if(getPageIsF(diskPages[i]) == 0){
        		if(getPageThID(diskPages[i]) == thread && i == getPagePgID(diskPages[i])){
                		realIndex = getPagePgID(diskPages[i]);
                		swapToMemFromDisk(i, realIndex);
               			mprotect(userSpace + (PAGESIZE * realIndex), PAGESIZE, PROT_READ | PROT_WRITE);
        		}
      		}

	    }
	}

	//returns the number of pages needed if there is enough room for them, 0 if there are not enough free pages
		static int isEnoughPages(int sizeRequest){
		    int pagesNeeded = (sizeRequest / PAGESIZE) + 1;
		    int freeCount = 0;
		    int i;
		    for ( i = 0; i < (MAX_MEMORY / PAGESIZE) - LIBPAGES; i++) {
			if(getPageIsF(pages[i]) == 1){
			    freeCount++;
			}
		    }

		    for(i = 0; i < (DISK_MEMORY / PAGESIZE); i++){
				if(getPageIsF(diskPages[i]) == 1){
					freeCount++;
				}
		    }


		    return (freeCount >= pagesNeeded) ? pagesNeeded : 0;
		}



		/**
		 * Initialize the root block. This is only called the first time that mymalloc is used.
		 */
		static void initializeRoot() {
		    rootBlock = (unsigned int*) memory;
        *rootBlock = setBlockIsF(*rootBlock, 1);
        *rootBlock = setBlockSize(*rootBlock, (unsigned int) ((LIBPAGES * PAGESIZE) - BLOCK_SIZE));
        *rootBlock = setBlockIsNext(*rootBlock, 0);

		    firstMalloc = false;
		}


		void initializePage(int index){
		    //index is the index in the pages array which will be converted to address in memory here

		    unsigned int pg = pages[index];

        if(scd == NULL){
          pg = setPagePgID(pg, mainPageNum++);
          pg = setPageThID(pg, 1);

        }
        else{
          pg = setPagePgID(pg, scd->current->threadID->pageNum++);
          pg = setPageThID(pg, scd->current->threadID->id);
        }

        pg = setPageIsF(pg, 0);

		    if(getPagePgID(pg) == 0){
          mprotect(userSpace, PAGESIZE, PROT_READ | PROT_WRITE);

          unsigned int* pageRoot = (unsigned int*) userSpace;

          *pageRoot = setBlockIsF(*pageRoot, 1);
          *pageRoot = setBlockSize(*pageRoot, (unsigned int)(PAGESIZE - BLOCK_SIZE));
          *pageRoot = setBlockIsNext(*pageRoot, 0);
		    }

        pages[index] = pg;
		}


		/**
		 * Performs a basic check to ensure that the pointer address is contained
		 * inside memory. It does not verify that all of the promised space is in memory,
		 * nor does it verify that the address is actually correct (e.g. a Block pointer).
		 *
		 * @param ptr Check if this pointer points to an address in memory.
		 * @return true if ptr is in memory, false otherwise.
		 */

     /*
		static bool inLibrarySpace(Block* ptr) {
		    return (char*) ptr >= &memory[0] && (char*) ptr < &memory[LIBPAGES * PAGESIZE];
		}

		static bool inThreadSpace(Block* ptr){
		    return (char*) ptr >= userSpace && (char*) ptr < &memory[MAX_MEMORY];
		}
    */
		static bool inMemSpace(unsigned int* ptr){
		    return (char*) ptr >= &memory[0] && (char*) ptr < &memory[MAX_MEMORY];
		}

		unsigned int findPage(int id){
		  int i;

		  for(i = 0; i < (MAX_MEMORY / PAGESIZE) - LIBPAGES; i++){
		    if(getPageThID(pages[i]) == id){

		      return pages[i];

		    }
		  }

		  return 0;
		}




		static bool moveToFreeSpace(int index) {
			int i;
			for(i = 0; i < (MAX_MEMORY / PAGESIZE) - LIBPAGES; i++){
				if(getPageIsF(pages[i]) == 1){

  				if(i == index){
  					return true;
  				}

					mprotect(userSpace + (PAGESIZE * index), PAGESIZE, PROT_READ | PROT_WRITE);

          pages[i] = pages[index];

					memcpy(userSpace + (PAGESIZE * i), userSpace + (PAGESIZE * index), PAGESIZE);
					pages[index] = setPageIsF(pages[index], 1);

					// protect the page where it was moved, unprotect the newly freed spot
					mprotect(userSpace + (PAGESIZE * i), PAGESIZE, PROT_NONE);

					return true;
				}
			}
			return false;
		}


  static bool moveToDiskSpace(int index) {

		pause_timer(scd->timer);

		int i;
		/////swpFile = fopen("swpfile", "w+");
		for(i = 0; i < (DISK_MEMORY / PAGESIZE); i++){
			if(getPageIsF(diskPages[i]) == 1){
       				mprotect(userSpace + (PAGESIZE * index), PAGESIZE, PROT_READ | PROT_WRITE);

        			diskPages[i] = pages[index];

				fseek(swpFile, (PAGESIZE * i), SEEK_SET);
				char toFile[PAGESIZE];

				memcpy(toFile, userSpace + (PAGESIZE * index), PAGESIZE);

				//fwrite(toFile, PAGESIZE, 1, swpFile);
        fwrite(toFile, 1, PAGESIZE, swpFile);
				pages[index] = setPageIsF(pages[index], 1);
       				/////fclose(swpFile);
				/////swpFile = -1;

				unpause_timer(scd->timer);

				return true;
			}
		}

		unpause_timer(scd->timer);

    		/////fclose(swpFile);
		/////swpFile = -1;
		return false;
	}

	static void gatherPages(int pagesNeeded, unsigned int* lastBlock){
	    unsigned int nextPage = 0;

	    while(pagesNeeded > 0){
          nextPage = (scd == NULL) ? mainPageNum : scd->current->threadID->pageNum;

      		if(moveToFreeSpace(nextPage) != true){
        			if(moveToDiskSpace(nextPage) != true){
          				puts("Out of disk and memory space.");
          				return;
        			}
      		}
      		initializePage(nextPage);
          *lastBlock = setBlockSize(*lastBlock, (unsigned int) (getBlockSize(*lastBlock) + PAGESIZE));
      		pagesNeeded--;
	    }

	}

	// this handler is called when a thread attempts to access data that does not belong to it. The handler will find the correct data and swap it
	static void sigHandler(int sig, siginfo_t *si, void *unused){

		if(timerSet){
		    pause_timer(scd->timer);
		}

		//printf("Got SIGSEGV at address: 0x%lx\n", (long) si->si_addr);
		bool pageSwapped = false;
		char *addr = si->si_addr;
		if((addr > memory + MAX_MEMORY || addr < memory)){
			puts("trying to access out of bounds stuff.\nexiting\n");
			exit(1);
		}
		else if((addr < userSpace && addr >= memory)){
			puts("trying to access library stuff.\nexiting\n");
			exit(1);
		}
		else{
			unsigned long long int diff = (char *)si->si_addr - userSpace;
			int index = (int)diff/PAGESIZE;
			int i;
			for(i = 0; i < (MAX_MEMORY / PAGESIZE) - LIBPAGES; i++){
			        if(getPageThID(pages[i]) == scd->current->threadID->id && getPagePgID(pages[i]) == index){

					if(i != index){
						pageSwap(index, i);
					}

					// Un-protect the page we just swapped in
					mprotect(userSpace + (index * PAGESIZE), PAGESIZE, PROT_READ | PROT_WRITE);
					pageSwapped = true;
					break;
				}
			}

			if(pageSwapped == true){
				if(timerSet){
					unpause_timer(scd->timer);
				}
				return;
			}
			for(i = 0; i < (DISK_MEMORY / PAGESIZE); i++){
				if(getPageIsF(diskPages[i]) == 0){
					if(getPageThID(diskPages[i]) == scd->current->threadID->id && getPagePgID(diskPages[i]) == index){

						swapToMemFromDisk(index, i);

						// Un-protect the page we just swapped in
						mprotect(userSpace + (index * PAGESIZE), PAGESIZE, PROT_READ | PROT_WRITE);
						pageSwapped = true;
						break;
					}
				}
			}

		}

		if(pageSwapped == false){
			printf("SEGMENTATION FAULT\n");
			exit(1);
		}

		if(timerSet){
			unpause_timer(scd->timer);
		}
	}

	void setUpSignal(){
		struct sigaction sa;
		sa.sa_flags = SA_SIGINFO;
		sigemptyset(&sa.sa_mask);
		sa.sa_sigaction = sigHandler;

		if(sigaction(SIGSEGV, &sa, NULL) == -1){
			printf("Fatal error setting up signal handler\n");
			exit(-1);
		}
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
	static bool freeAndMerge(unsigned int* toFree, unsigned int page, int caller) {

	    unsigned int* previous = NULL;
	    unsigned int* current = NULL;
      unsigned int* next = NULL;

	    if(caller == LIBRARYREQ){
	      current = rootBlock;
	    }
	    else if(caller == THREADREQ){
	      int index;
	      for(index = 0; index < (MAX_MEMORY/PAGESIZE) - LIBPAGES; index++){
        		if(getPageThID(pages[index]) == getPageThID(page)){
        		  current = (unsigned int*) userSpace + (index * PAGESIZE);
        		  break;
        		}
	      }
	    }

	    if(current == NULL){
	      return false;
	    }


	    // Iterate through the list until toFree is found.
	    do {

		if (current == toFree) {
        *current = setBlockIsF(*current, 1);
		    next = getNextBlock(current);

		    // Merge current with next if next is free.
		    if (next != NULL && getBlockIsF(*next) == 1) {
          *current = setBlockSize(*current, (unsigned int)(getBlockSize(*current) + getBlockSize(*next) + BLOCK_SIZE));
          *current = setBlockIsNext(*current, getBlockIsNext(*next));
		    }

		    // Merge previous with current if previous is free.
		    if (previous != NULL && getBlockIsF(*previous) == 1) {
          *previous = setBlockSize(*previous, (unsigned int)(getBlockSize(*current) + getBlockSize(*previous) + BLOCK_SIZE));
          *previous = setBlockIsNext(*previous, getBlockIsNext(*current));
		    }

		    return true;
		}

		else {
		    previous = current;
		}

  } while ((current = getNextBlock(current)) != NULL);

	    // Return false if toFree was not found.
	    return false;
	}

	//TODO decrement size left and such
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
      		posix_memalign((void **)&memory, PAGESIZE, MAX_MEMORY);

      		userSpace = &memory[LIBPAGES * PAGESIZE];

	        //init swap file

          //swpFile = fopen("swpfile", "w+");
          swpFile = fopen("/home/csuser/OS2017/assignment3/example/mountdir/swpfile", "w+");
          //puts("init'ed the swp");

          //set the swap size 16MB
          ftruncate(fileno(swpFile), DISK_MEMORY);
          /////fclose(swpFile);
		      /////swpFile = -1;
	        //puts("truncated done");


    	    // deletes the swpfile at the end of program execution
    	    atexit(deleteFile);


      		setUpSignal();
      		initializeRoot();

          diskPages = (unsigned int*)myallocate(sizeof(unsigned int) * ((DISK_MEMORY / PAGESIZE)), __FILE__, __LINE__, LIBRARYREQ);
          puts("diskPages mallocd");
          // malloc for the disk table
          int i;
          for ( i = 0; i < (DISK_MEMORY / PAGESIZE); i++) {
                diskPages[i] = 1;
          }

      		//allocate memory for the page table which will be addressed as an array
      		//each index in this array will translate to the memory block as &(where user space starts) + (i * PAGESIZE)
      		pages = (unsigned int*) myallocate(sizeof(unsigned int) * ((MAX_MEMORY / PAGESIZE) - LIBPAGES), __FILE__, __LINE__, LIBRARYREQ);

      		for ( i = 0; i < (MAX_MEMORY / PAGESIZE) - LIBPAGES; i++) {
      			  pages[i] = 1;
      		}
          printf("firstmalloc = %d", firstMalloc);
	    }

	    unsigned int* current;
	    const unsigned int sizeWithBlock = size + BLOCK_SIZE; // Include extra space for metadata.


	    if(caller == DISKREQ){
		//do normal malloc stuff

		    current = rootBlockD;

		    // First fit placement strategy.
		    do {

			// Look for free block with enough space.
			if (getBlockIsF(*current) == 0 || getBlockSize(*current) < size) {
			    continue;
			}

			else if (getBlockIsF(*current) == 1) {

			    if (getBlockSize(*current) == size) {
				// Mark current block as taken and return it.
        *current = setBlockIsF(*current, 0);

				if(timerSet){
				    unpause_timer(scd->timer);
				}

				return ((char*) current) + BLOCK_SIZE;
			    }

			    // If a free block has more than enough space, create new free block to take up the rest of the space.
			    else if (getBlockSize(*current) >= sizeWithBlock) {

				unsigned int* newBlock = (unsigned int*) ((char*) current + sizeWithBlock);

        *newBlock = setBlockIsF(*newBlock, 1);
        *newBlock = setBlockSize(*newBlock, (unsigned int) getBlockSize(*current) - sizeWithBlock);
        *newBlock = setBlockIsNext(*newBlock, getBlockIsNext(*current));

				*current = setBlockSize(*current, size);
        *current = setBlockIsNext(*current, 1);
        *current = setBlockIsF(*current, 0);

				if(timerSet){
				    unpause_timer(scd->timer);
				}

				return ((char*) current) + BLOCK_SIZE;
			    }

			    /* NOTE: If current->size is greater than size, but less than sizeWithBlock,
			     * then there is not enough room to accommodate both the space and a new Block,
			     * so we continue the search. */
			}

    } while ((current = getNextBlock(current)) != NULL);

		    // If no suitable free block is found, print an error message and return NULL pointer.
		    printf("Error at line %d of %s: not enough space is available to allocate.\n", line, file);

		    if(timerSet){
			      unpause_timer(scd->timer);
		    }

		    return NULL;
		}

	    else if(caller == LIBRARYREQ){
    		//do normal malloc stuff

    		    current = rootBlock;

    		    // First fit placement strategy.
    		    do {

    			// Look for free block with enough space.
    			if (getBlockIsF(*current) == 0 || getBlockSize(*current) < size) {
    			    continue;
    			}

    			else if (getBlockIsF(*current) == 1) {

    			    if (getBlockSize(*current) == size) {
    				// Mark current block as taken and return it.
            *current = setBlockIsF(*current, 0);

    				if(timerSet){
    				    unpause_timer(scd->timer);
    				}

    				return ((char*) current) + BLOCK_SIZE;
    			    }

    			    // If a free block has more than enough space, create new free block to take up the rest of the space.
    			    else if (getBlockSize(*current) >= sizeWithBlock) {

    				unsigned int* newBlock = (unsigned int*) ((char*) current + sizeWithBlock);

            *newBlock = setBlockIsF(*newBlock, 1);
            *newBlock = setBlockSize(*newBlock, (unsigned int) getBlockSize(*current) - sizeWithBlock);
            *newBlock = setBlockIsNext(*newBlock, getBlockIsNext(*current));

    				*current = setBlockSize(*current, size);
            *current = setBlockIsNext(*current, 1);
            *current = setBlockIsF(*current, 0);

    				if(timerSet){
    				    unpause_timer(scd->timer);
    				}

    				return ((char*) current) + BLOCK_SIZE;
    			    }

    			    /* NOTE: If current->size is greater than size, but less than sizeWithBlock,
    			     * then there is not enough room to accommodate both the space and a new Block,
    			     * so we continue the search. */
    			}

        } while ((current = getNextBlock(current)) != NULL);

    		    // If no suitable free block is found, print an error message and return NULL pointer.
    		    printf("Error at line %d of %s: not enough space is available to allocate.\n", line, file);

    		    if(timerSet){
    			      unpause_timer(scd->timer);
    		    }

    		    return NULL;
    	}else{
        		//do page stuff for threads

        		if(scd == NULL){
        		    return myallocate(size, __FILE__, __LINE__, LIBRARYREQ );
        		}

        		int thread = (scd == NULL) ? 1 : scd->current->threadID->id;

        		alignPages();


        		unsigned int pg = pages[0];
        		if(getPageThID(pg) != thread){

                //this is the first allocation for this thread so make room for it and start writing
                if(moveToFreeSpace(0) != true){
        			       //do move to disk stuff and if thats full then return NULL
                			if(moveToDiskSpace(0) != true){
                  				puts("could not allocate.");
                  				return NULL;
                			}

        		     }
        		  initializePage(0);
        		}

            pg = pages[0];
        		current = (unsigned int*) userSpace;
        		unsigned int* prev = NULL;

        		do {
        		    prev = current;
        		    // Look for free block with enough space.
        		    if (getBlockIsF(*current) == 0 || getBlockSize(*current) < size) {
        			       continue;
        		    }

        		    else if (getBlockIsF(*current) == 1) {

            			if (getBlockSize(*current) == size) {
            			    // Mark current block as taken and return it.
                      *current = setBlockIsF(*current, 0);

            			    if(timerSet){
            				      unpause_timer(scd->timer);
            			    }

            				  return ((char*) current) + BLOCK_SIZE;
            			}

            			// If a free block has more than enough space, create new free block to take up the rest of the space.
            			else if (getBlockSize(*current) >= sizeWithBlock) {
                    unsigned int* newBlock = (unsigned int*) ((char*) current + sizeWithBlock);

                    *newBlock = setBlockIsF(*newBlock, 1);
                    *newBlock = setBlockSize(*newBlock, (unsigned int) getBlockSize(*current) - sizeWithBlock);
                    *newBlock = setBlockIsNext(*newBlock, getBlockIsNext(*current));

                    *current = setBlockSize(*current, size);
                    *current = setBlockIsNext(*current, 1);
                    *current = setBlockIsF(*current, 0);


            			    if(timerSet){
            				      unpause_timer(scd->timer);
            			    }

            				return ((char*) current) + BLOCK_SIZE;
            			}

        			/* NOTE: If current->size is greater than size, but less than sizeWithBlock,
        			 * then there is not enough room to accommodate both the space and a new Block,
        			 * so we continue the search. */
        		    }

        		} while ((current = getNextBlock(current)) != NULL);
        		//there is no more room left in the current amount of pages so make a new one if possible

        		//if theres no room left in memory to make the allocation happen then return NULL
        		int bytesLeft = (int)((char*) memory + MAX_MEMORY - ((char*)prev + BLOCK_SIZE));

        		if(sizeWithBlock > bytesLeft){
        		    printf("Error at line %d of %s: not enough space is available to allocate.\n", line, file);

        		    if(timerSet){
        			      unpause_timer(scd->timer);
        		    }

                return NULL;
            }

          //this is if the last block isn't the free block pointing to the rest of the free space
          int extraBlock = (getBlockIsF(*prev) == 0) ? BLOCK_SIZE : 0;

        	int pagesNeeded = isEnoughPages(extraBlock + sizeWithBlock - getBlockSize(*prev));

          if(pagesNeeded > 0){
                if(extraBlock > 0){
                    int nextPage = (scd == NULL) ? mainPageNum : scd->current->threadID->pageNum;

                		if(moveToFreeSpace(nextPage) != true){
                  			if(moveToDiskSpace(nextPage) != true){
                    				puts("Disk and memory full. Cannot allocate anymore.");
                    				return NULL;
                  			}
                		}

                    initializePage(nextPage);
                    unsigned int* bl = (unsigned int*)((char*) userSpace + (nextPage * PAGESIZE));
                    *bl = setBlockIsF(*bl, 1);
                    *bl = setBlockSize(*bl, (unsigned int) PAGESIZE - BLOCK_SIZE);
                    *bl = setBlockIsNext(*bl, 0);

                    *prev = setBlockIsNext(*prev, 1);
                    prev = bl;

                    gatherPages(pagesNeeded - 1, prev);
                }else{
                    gatherPages(pagesNeeded, prev);
                }

            current = prev;

            unsigned int* newBlock = (unsigned int*) ((char*) current + sizeWithBlock);

            *newBlock = setBlockIsF(*newBlock, 1);
            *newBlock = setBlockSize(*newBlock, (unsigned int) getBlockSize(*current) - sizeWithBlock);
            *newBlock = setBlockIsNext(*newBlock, getBlockIsNext(*current));

            *current = setBlockSize(*current, size);
            *current = setBlockIsNext(*current, 1);
            *current = setBlockIsF(*current, 0);

            if(timerSet){
                unpause_timer(scd->timer);
            }

            return ((char*) current) + BLOCK_SIZE;


        }

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

    if(timerSet){
        pause_timer(scd->timer);
    }

    if(caller == LIBRARYREQ){

        //called from library
            if (!inMemSpace(ptr)/*inLibrarySpace(ptr)*/) {
                printf("Error at line %d of %s: pointer was not created using malloc.\n", line, file);

                if(timerSet){
                    unpause_timer(scd->timer);
                }

                return;
            }

        unsigned int* block = (unsigned int*) ((char*) ptr - BLOCK_SIZE);

        if (getBlockIsF(*block) == 1) {
            printf("Error at line %d of %s: pointer has already been freed.\n", line, file);

            if(timerSet){
                unpause_timer(scd->timer);
            }

            return;
        }

        if(!freeAndMerge(block, 0, LIBRARYREQ)) {
            printf("Error at line %d of %s: pointer was not created using malloc.\n", line, file);
        }

        if(timerSet){
            unpause_timer(scd->timer);
        }

        return;

    }else if (caller == THREADREQ){

        //called from thread
        if(!inMemSpace(ptr)/*inThreadSpace(ptr)*/){
            printf("Error at line %d of %s: pointer was not created using malloc.\n", line, file);

            if(timerSet){
                unpause_timer(scd->timer);
            }

            return;
        }


        unsigned int* block = (unsigned int*) ((char*) ptr - BLOCK_SIZE);
        if (getBlockIsF(*block) == 1) {
            printf("Error at line %d of %s: pointer has already been freed.\n", line, file);

            if(timerSet){
                unpause_timer(scd->timer);
            }

            return;
        }


        unsigned int page;

        if(scd == NULL){

            page = findPage(1);

        }else{

            page = findPage(scd->current->threadID->id);

        }

        if(page == 0){

            if(timerSet){
                unpause_timer(scd->timer);
            }

            mydeallocate(ptr, __FILE__, __LINE__, LIBRARYREQ);

            return;
        }


        if (!freeAndMerge(block, page, THREADREQ)) {
            printf("Error at line %d of %s: pointer was not created using malloc.\n", line, file);

            if(timerSet){
                unpause_timer(scd->timer);
            }

            return;
        }


        if(timerSet){
            unpause_timer(scd->timer);
        }

        return;


    } else {
      printf("invalid caller\n");
    }

    if(timerSet){
        unpause_timer(scd->timer);
    }

    return;
}

void* test(void* arg) {
	printf("got here\n");
	printf("v will be holding %p\n", arg);
	int* v = (int*)arg;
	puts("didnt break yet");
	int x = *v;
	puts("still workin");
	printf("in thread x is %d\n", x);
	int* y = (int*)malloc(sizeof(int) * 400000);

	if (y == NULL) {
		printf("oopsie, not enouch spage\n");
		exit(1);

	}

		puts("mallocd ints");
	 //   long long int* h;
		  //  h = (long long int *)malloc(sizeof(long long int) * 5000);

		//	if( h == NULL){
		//		printf("the longs were too long");
		//		exit(1);
		//	}


		  //  puts("mallocd longs");
		*y = x;
	printf("I am thread %d and my number is %d\n", *v, *y);
	    //printPages();
		my_pthread_yield();
	malloc(1);
	printf("I am thread %d and my number is %d\n", *v, *y);
	    //free(y);
		    //while (1) {

		    //}
		return NULL;

}

int main() {

	my_pthread_t th[10];
	my_pthread_t* th1 = malloc(sizeof(my_pthread_t));

	int* x = (int*)malloc(sizeof(int) * 150);

	//int gg = 69;
	printf("th1's address is %p\n",&th1 );
	int i;
	  //int* x;
		  //printPages();
		  //my_pthread_create(th1, NULL,test,(void*)&gg);
		  //printPages();

	int* intarr[10];
	for (i = 0; i < 10; i++) {
		intarr[i] = (int*)malloc(sizeof(int));
		*intarr[i] = i;

	}

	for (i = 0; i < 10; i++) {


		printf("x is %d\n", *x);
		my_pthread_create(&th[i], NULL, test, (void*)intarr[i]);
		printf("thread #%d made\n", i);
		//printPages();

	}

	long long int j = 0;
	while (j < 100000000) {
		if (j % 1000 == 0) {
		        //puts("didnt swap yet");

		}
		j++;

	}
	//printPages();
	*x = 123;
	//printPages();

		printf("gonna exit\n");
	return 0;

}
