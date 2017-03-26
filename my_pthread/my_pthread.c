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
    bool isFree;
} Page;

/******************globals***********************/
static scheduler* scd = NULL;
static bool timerSet = false;
static int currMutexID = 0;
static mutex_list *mutexList = NULL;

static const unsigned long long int BLOCK_SIZE = sizeof(Block);
static const long long int MAX_MEMORY = 8388608;
static const long long int DISK_MEMORY = 16777216;
static const long long int STACK_SIZE = 18192;
static char* memory;
static char* disk;
static bool firstMalloc = true;
static Block* rootBlock;
static Block* rootBlockD;

static char* userSpace = NULL;
static char* userSpaceDisk = NULL;
static int mainPageNum = 0;
static Page** pages;//this will be the array of pages in memory block
static Page** diskPages;
/********************functions*******************/

void printhreads(){
    my_pthread_t* ptr;
    for ( ptr = scd->threads->head; ptr != NULL; ptr=ptr->next) {
        printf("thread %d at addr %p\n",ptr->id,ptr );
    }
}




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
		if(pages[i]->threadID == threadID){
			mprotect(userSpace + (i * PAGESIZE), PAGESIZE, PROT_NONE);
		}
	}
}


//scheduler context function
void schedule(){

    pause_timer(scd->timer);

    //printhreads();

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
	 protectAllPages(justRun->threadID->id);

        swapcontext(justRun->ut, scd->current->ut);

    }

}


//alarm signal handler that will set to the scheduler context which will change what is running
void timerHandler(int signum){
    scd->timer->it_value.tv_usec = 0;
    schedule();
}

void freePages(){

  int i;
  for(i = 0; i < (MAX_MEMORY / PAGESIZE) - LIBPAGES; i++){
    if(pages[i]->threadID == scd->current->threadID->id){
      mprotect(userSpace + (PAGESIZE * i), PAGESIZE, PROT_READ | PROT_WRITE);
      pages[i]->isFree = true;
    }
  }

}

void terminationHandler(){

      scd->current->status = EXITING;
      freePages();
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


void printThreads(){
	my_pthread_t* ptr;
	ptr = scd->threads->head;
	while(ptr != NULL){
		ptr = ptr->next;
	}

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

    schedule();
}

//exits the thread that called this function and passes on value_ptr as an argument for whatever might join on this thread
void my_pthread_exit(void * value_ptr){
    if(scd == NULL){
        return;
    }

    scd->current->status = EXITING;
    scd->current->threadID->exitArg = value_ptr;
    freePages();

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

	// swaps two pages in memory and in the page table that corresponds to those pages
	void pageSwap(int initial, int swapTo){
		char temp[PAGESIZE];

		//un-protect so we can write to the new spots
		mprotect(userSpace + (PAGESIZE * swapTo), PAGESIZE, PROT_READ | PROT_WRITE);
		mprotect(userSpace + (PAGESIZE * initial), PAGESIZE, PROT_READ | PROT_WRITE);

		// swap mem
		memcpy(temp, userSpace + (PAGESIZE * swapTo), PAGESIZE);
		memcpy(userSpace + (PAGESIZE * swapTo), userSpace + (PAGESIZE * initial), PAGESIZE);
		memcpy(userSpace + (PAGESIZE * initial), temp, PAGESIZE);

		// swap page table data
		Page* tempPage;

		tempPage = pages[swapTo];
		pages[swapTo] = pages[initial];
		pages[initial] = tempPage;


		//protect the memory pages again
		mprotect(userSpace + (PAGESIZE * swapTo), PAGESIZE, PROT_NONE);
		mprotect(userSpace + (PAGESIZE * initial), PAGESIZE, PROT_NONE);

	}

	void swapToMemFromDisk(int inMem, int fromDisk){

		char temp[PAGESIZE];

		//un-protect the page in memory that will be swapped
		mprotect(userSpace + (PAGESIZE * inMem), PAGESIZE, PROT_READ | PROT_WRITE);

		// swap mem
		memcpy(temp, userSpaceDisk + (PAGESIZE * fromDisk), PAGESIZE);
		memcpy(userSpaceDisk + (PAGESIZE * fromDisk), userSpace + (PAGESIZE * inMem), PAGESIZE);
		memcpy(userSpace + (PAGESIZE * inMem), temp, PAGESIZE);

		// swap page table data
		Page* tempPage;

		tempPage = diskPages[fromDisk];
		diskPages[fromDisk] = pages[inMem];
		pages[inMem] = tempPage;


		//protect the pages
		mprotect(userSpace + (PAGESIZE * inMem), PAGESIZE, PROT_NONE);
	}


	static void alignPages(){

	    int thread = (scd == NULL) ? 1 : scd->current->threadID->id;

	    int i;
	    for (i = 0; i < (MAX_MEMORY / PAGESIZE) - LIBPAGES; i++) {
		if(pages[i]->threadID == thread && i != pages[i]->pageID){
		    pageSwap(i, pages[i]->pageID);
		}
	    }


	    //once disk is in place, now check disk if there are any pages in disk to be aligned into memory
	    for(i = 0; i < (DISK_MEMORY / PAGESIZE) - 100; i++){
		if(diskPages[i] != NULL){
			if(diskPages[i]->threadID == thread && i != diskPages[i]->pageID){
				swapToMemFromDisk(i, diskPages[i]);
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
			if(pages[i]->isFree == true){
			    freeCount++;
			}
		    }

		    for(i = 0; i < (DISK_MEMORY / PAGESIZE) - 100; i++){
			if(diskPages[i] != NULL){
				if(diskPages[i]->isFree == true){
					freeCount++;
				}
			}
		    }


		    return (freeCount >= pagesNeeded) ? pagesNeeded : 0;
		}



		/**
		 * Initialize the root block. This is only called the first time that mymalloc is used.
		 */
		static void initializeRoot() {
		    rootBlock = (Block*) memory;
		    rootBlock->isFree = true;
		    rootBlock->size = (LIBPAGES * PAGESIZE) - BLOCK_SIZE;
		    rootBlock->next = NULL;
		    firstMalloc = false;
		}


		static void initializeRootDisk() {
		    rootBlockD = (Block*) disk;
		    rootBlockD->isFree = true;
		    rootBlockD->size = (4096 * PAGESIZE) - BLOCK_SIZE;
		    rootBlockD->next = NULL;
		    firstMalloc = false;
		}








		void initializePage(int index){
		    //index is the index in the pages array which will be converted to address in memory here

		    Page* pg = pages[index];

		    pg->threadID = (scd == NULL) ? 1 : scd->current->threadID->id;
		    pg->pageID = (scd == NULL) ? mainPageNum++ : scd->current->threadID->pageNum++ ;
		    pg->isFree = false;

		    if(pg->pageID == 0){
			Block* rootBlock = (Block*) userSpace;
			rootBlock->isFree = true;
			rootBlock->size = PAGESIZE - BLOCK_SIZE;
			rootBlock->next = NULL;
		    }

		}


		/**
		 * Performs a basic check to ensure that the pointer address is contained
		 * inside memory. It does not verify that all of the promised space is in memory,
		 * nor does it verify that the address is actually correct (e.g. a Block pointer).
		 *
		 * @param ptr Check if this pointer points to an address in memory.
		 * @return true if ptr is in memory, false otherwise.
		 */
		static bool inLibrarySpace(Block* ptr) {
		    return (char*) ptr >= &memory[0] && (char*) ptr < &memory[LIBPAGES * PAGESIZE];
		}

		static bool inThreadSpace(Block* ptr){
		    return (char*) ptr >= userSpace && (char*) ptr < &memory[MAX_MEMORY];
		}

		static bool inMemSpace(Block* ptr){
		    return (char*) ptr >= &memory[0] && (char*) ptr < &memory[MAX_MEMORY];
		}

		Page* findPage(int id, Block* block){
		  int i;

		  for(i = 0; i < (MAX_MEMORY / PAGESIZE) - LIBPAGES; i++){
		    if(pages[i]->threadID == id){

		      return pages[i];

		    }
		  }

		  return NULL;
		}




		static bool moveToFreeSpace(int index) {
			int i;
			for(i = 0; i < (MAX_MEMORY / PAGESIZE) - LIBPAGES; i++){
				if(pages[i]->isFree){

				if(i == index){
					return true;
				}

					mprotect(userSpace + (PAGESIZE * index), PAGESIZE, PROT_READ | PROT_WRITE);

					memcpy(pages[i], pages[index], sizeof(Page));
					memcpy(userSpace + (PAGESIZE * i), userSpace + (PAGESIZE * index), PAGESIZE);
					pages[index]->isFree = true;

					// protect the page where it was moved, unprotect the newly freed spot
					mprotect(userSpace + (PAGESIZE * i), PAGESIZE, PROT_NONE);

					return true;
				}
			}
			return false;
		}


		static bool moveToDiskSpace(int index) {
			int i;
			for(i = 0; i < (DISK_MEMORY / PAGESIZE) - 100; i++){

			if(diskPages[i] != NULL){
			if(diskPages[i]->isFree){
				if(i == index){
					return true;
				}

				mprotect(userSpace + (PAGESIZE * index), PAGESIZE, PROT_READ | PROT_WRITE);

				memcpy(diskPages[i], pages[index], sizeof(Page));
				memcpy(disk + (PAGESIZE * i), userSpace + (PAGESIZE * index), PAGESIZE);
				pages[index]->isFree = true;

				return true;
			}
			}
		}
		return false;
	}

	static void gatherPages(int pagesNeeded, Block* lastBlock){
	    int nextPage;

	    while(pagesNeeded > 0){
		nextPage = (scd == NULL) ? mainPageNum : scd->current->threadID->pageNum;
		if(moveToFreeSpace(nextPage) != true){
			if(moveToDiskSpace(nextPage) != true){
				puts("Out of disk and memory space.");
				return;
			}
		}
		initializePage(nextPage);
		lastBlock->size += PAGESIZE;

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
		if((addr > memory + MAX_MEMORY || addr < memory) && (addr < userSpaceDisk || addr > disk + DISK_MEMORY)){
			puts("trying to access out of bounds stuff. bad");
			exit(1);
		}
		else if((addr < userSpace && addr >= memory) && (addr < userSpaceDisk && addr >= disk )){
			puts("trynna access library stuff. bad");
			exit(1);
		}
		else{
			unsigned long long int diff = (char *)si->si_addr - userSpace;
			int index = (int)diff/PAGESIZE;
			int i;

			for(i = 0; i < (MAX_MEMORY / PAGESIZE) - LIBPAGES; i++){
				if(pages[i]->threadID == scd->current->threadID->id && pages[i]->pageID == index){

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

			for(i = 0; i < (DISK_MEMORY / PAGESIZE) - 100; i++){
				if(diskPages[i] != NULL){
					if(diskPages[i]->threadID == scd->current->threadID->id && diskPages[i]->pageID == index){

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
	static bool freeAndMerge(Block* toFree, Page* page, int caller) {

	    Block* previous = NULL;
	    Block* current = NULL;

	    if(caller == LIBRARYREQ){
	      current = rootBlock;
	    }
	    else if(caller == THREADREQ){
	      int index;
	      for(index = 0; index < (MAX_MEMORY/PAGESIZE) - LIBPAGES; index++){
        		if(pages[index]->threadID == page->threadID){
        		  current = (Block*) userSpace + (index * PAGESIZE);
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
		posix_memalign((void **)&disk, PAGESIZE, DISK_MEMORY);

		userSpace = &memory[LIBPAGES * PAGESIZE];
		userSpaceDisk = &disk[100 * PAGESIZE];

		initializeRootDisk();
		int i;
		diskPages = (Page**)myallocate(sizeof(Page*) * ((DISK_MEMORY / PAGESIZE) - 100), __FILE__, __LINE__, DISKREQ);

		// malloc for the disk table
		for ( i = 0; i < (DISK_MEMORY / PAGESIZE) - 100; i++) {
			diskPages[i] = (Page*) myallocate(sizeof(Page), __FILE__, __LINE__, DISKREQ);
			 diskPages[i]->isFree = true;
		}


		setUpSignal();
		initializeRoot();

		//printf("memory starts at %p\nuser space starts at %p\nmemory ends at %p",memory, userSpace, memory+MAX_MEMORY );

		//allocate memory for the page table which will be addressed as an array
		//each index in this array will translate to the memory block as &(where user space starts) + (i * PAGESIZE)
		pages = (Page**) myallocate(sizeof(Page*) * ((MAX_MEMORY / PAGESIZE) - LIBPAGES), __FILE__, __LINE__, LIBRARYREQ);

		for ( i = 0; i < (MAX_MEMORY / PAGESIZE) - LIBPAGES; i++) {
		    pages[i] = (Page*) myallocate(sizeof(Page), __FILE__, __LINE__, LIBRARYREQ);
			  pages[i]->isFree = true;
		}
	    }

	    Block* current;
	    const unsigned long long int sizeWithBlock = size + BLOCK_SIZE; // Include extra space for metadata.


	    if(caller == DISKREQ){
		//do normal malloc stuff

		    current = rootBlockD;

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
		}

	    else if(caller == LIBRARYREQ){
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

		if(scd == NULL){
		    return myallocate(size, __FILE__, __LINE__, LIBRARYREQ );
		}

		int thread = (scd == NULL) ? 1 : scd->current->threadID->id;

		alignPages();


		Page* pg = pages[0];

		if(pg->threadID != thread){
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

		current = (Block*) userSpace;
		Block* prev = NULL;

		do {
		    prev = current;

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
        int extraBlock = (prev->isFree == false) ? BLOCK_SIZE : 0;

	int pagesNeeded = isEnoughPages(extraBlock + sizeWithBlock - prev->size);

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
                Block* bl = (Block*)((char*) userSpace + (nextPage * PAGESIZE));
                bl->isFree = true;
                bl->size = PAGESIZE - BLOCK_SIZE;
                bl->next = NULL;
                prev->next = bl;
                prev = bl;

                gatherPages(pagesNeeded - 1, prev);
            }else{
                gatherPages(pagesNeeded, prev);
            }

            current = prev;

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

        Block* block = (Block*) ((char*) ptr - BLOCK_SIZE);

        if (block->isFree) {
            printf("Error at line %d of %s: pointer has already been freed.\n", line, file);

            if(timerSet){
                unpause_timer(scd->timer);
            }

            return;
        }

        if(!freeAndMerge(block, NULL, LIBRARYREQ)) {
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


        Block* block = (Block*) ((char*) ptr - BLOCK_SIZE);
        if (block->isFree) {
            printf("Error at line %d of %s: pointer has already been freed.\n", line, file);

            if(timerSet){
                unpause_timer(scd->timer);
            }

            return;
        }


        Page* page;

        if(scd == NULL){

            page = findPage(1, block);

        }else{

            page = findPage(scd->current->threadID->id, block);

        }

        if(page == NULL){

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


void printPages(){
    int i;
    for ( i = 0; i < (MAX_MEMORY / PAGESIZE) - LIBPAGES; i++) {
        if(pages[i]->isFree == true){
            break;
        }
        printf("index %d holds pageID: %d, threadID: %d\n",i, pages[i]->pageID, pages[i]->threadID );

    }
}

int main(){
  int* x =  malloc(sizeof(int));
  free(x);
  return 0;
}
