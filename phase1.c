/* ------------------------------------------------------------------------
   phase1.c

   University of Arizona
   Computer Science 452
   Fall 2015

   ------------------------------------------------------------------------ */

#include "phase1.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "kernel.h"

/* ------------------------- Prototypes ----------------------------------- */
int sentinel (char *);
extern int start1 (char *);
void dispatcher(void);
void launch();
static void checkDeadlock();
void checkKernelMode();
int enableInterrupts();
int disableInterrupts();
void clockHandler(int dev, void *arg); //See usloss.h line 64
void instructionHandler(int dev, void *arg);
int readtime(void);
int readCurStartTime(void);
void timeSlice(void);
void dumpProcesses();
int getpid();
int zap(int pid);
int isZapped();
int blockMe(int newStatus);

/* -------------------------- Globals ------------------------------------- */

// Patrick's debugging global variable...
int debugflag = 0;

// the process table
procStruct ProcTable[MAXPROC];

// Process lists
static procPtr ReadyList;

// current process ID
procPtr Current;

// the next pid to be assigned
unsigned int nextPid = SENTINELPID;





/* -------------------------- Functions ----------------------------------- */
/* ------------------------------------------------------------------------
   Name - startup
   Purpose - Initializes process lists and clock interrupt vector.
             Start up sentinel process and the test process.
   Parameters - argc and argv passed in by USLOSS
   Returns - nothing
   Side Effects - lots, starts the whole thing
   ----------------------------------------------------------------------- */
void startup(int argc, char *argv[])
{
    int result; /* value returned by call to fork1() */

    /* initialize the process table */
    if (DEBUG && debugflag)
        USLOSS_Console("startup(): initializing process table, ProcTable[]\n");
	
	for (int i = 0; i < MAXPROC; i++){
		ProcTable[i].nextProcPtr = NULL; 			/*procPtr*/
        ProcTable[i].childProcPtr = NULL;			/*procPtr*/
        ProcTable[i].nextSiblingPtr = NULL; 		/*procPtr*/
        ProcTable[i].name[0] = '\0';     				/* char *: process's name */
        ProcTable[i].startArg[0] = '\0';  				/* char: args passed to process */
     	//ProcTable[i].state;          		/* USLOSS_Context: current context for process */
        ProcTable[i].pid = -1;              		/* short: process id */
        ProcTable[i].priority = -1;  				/* int */
   		ProcTable[i].startFunc = NULL;   			/* int (* startFunc) (char *): function where process begins -- launch */
   		ProcTable[i].stack = NULL; 					/* Char* */
   		ProcTable[i].stackSize = 0;					/* unsigned int    */
        ProcTable[i].status = -1;        			/* int: READY, BLOCKED, QUIT, etc. */
   		/* other fields as needed... */
   		ProcTable[i].quitStatus = 0;				/*process quit(quitStatus); */
   		ProcTable[i].parent = NULL;					/*a process' parent ptr */
   		ProcTable[i].unjoinedChildrenProcPtr = NULL; 	/*procPtr of quit children pre-join */
   		ProcTable[i].unjoinedSiblingProcPtr = NULL;
   		ProcTable[i].zapStatus = 0;
   		ProcTable[i].numberOfChildren = 0;
   		ProcTable[i].startTime = 0;
   		
	}
    
    // Initialize the Ready list, etc.
    if (DEBUG && debugflag)
        USLOSS_Console("startup(): initializing the Ready list\n");
    ReadyList = NULL;

    // Initialize the clock interrupt handler
    USLOSS_IntVec[USLOSS_CLOCK_INT] = clockHandler;
    USLOSS_IntVec[USLOSS_ILLEGAL_INT] = instructionHandler;

    // startup a sentinel process
    if (DEBUG && debugflag)
        USLOSS_Console("startup(): calling fork1() for sentinel\n");
    result = fork1("sentinel", sentinel, NULL, USLOSS_MIN_STACK,
                    SENTINELPRIORITY);
    if (result < 0) {
        if (DEBUG && debugflag) {
            USLOSS_Console("startup(): fork1 of sentinel returned error, ");
            USLOSS_Console("halting...\n");
        }
        USLOSS_Halt(1);
    }
    
    
    // start the test process
    if (DEBUG && debugflag)
        USLOSS_Console("startup(): calling fork1() for start1\n");
    result = fork1("start1", start1, NULL, 2 * USLOSS_MIN_STACK, 1);
    if (result < 0) {
        USLOSS_Console("startup(): fork1 for start1 returned an error, ");
        USLOSS_Console("halting...\n");
        USLOSS_Halt(1);
    }

    USLOSS_Console("startup(): Should not see this message! ");
    USLOSS_Console("Returned from fork1 call that created start1\n");

    return;
} /* startup */

/* ------------------------------------------------------------------------
   Name - finish
   Purpose - Required by USLOSS
   Parameters - none
   Returns - nothing
   Side Effects - none
   ----------------------------------------------------------------------- */
void finish(int argc, char *argv[])
{
    if (DEBUG && debugflag)
        USLOSS_Console("in finish...\n");
} /* finish */

/* ------------------------------------------------------------------------
   Name - fork1
   Purpose - Gets a new process from the process table and initializes
             information of the process.  Updates information in the
             parent process to reflect this child process creation.
   Parameters - the process procedure address, the size of the stack and
                the priority to be assigned to the child process.
   Returns - the process id of the created child or -1 if no child could
             be created or if priority is not between max and min priority.
   Side Effects - ReadyList is changed, ProcTable is changed, Current
                  process information changed
   ------------------------------------------------------------------------ */
int fork1(char *name, int (*startFunc)(char *), char *arg,
          int stacksize, int priority)
{
	if (DEBUG && debugflag)
        USLOSS_Console("fork1(): starting\n");
        
    // test if in kernel mode; halt if in user mode
	checkKernelMode("fork1");
	
	//enable interrupts
	//enableInterrupts();
	
	if(name == NULL || startFunc == NULL ){
		if (DEBUG && debugflag)
			USLOSS_Console("fork1(): name is NULL\n");
		return -1; 
	} 
	if(startFunc == NULL){
		if (DEBUG && debugflag)
			USLOSS_Console("fork1(): startFunc is NULL\n");
		return -1; 
	}
	if (priority < 1 || priority > 6){
		if (DEBUG && debugflag)
			USLOSS_Console("fork1(): priority %d is out-of-range\n", priority);
		return -1;
	}
	
	int procSlot = -1;

    if (DEBUG && debugflag)
        USLOSS_Console("fork1(): creating process %s\n", name);

    
    // Return if stack size is too small
    if (stacksize < USLOSS_MIN_STACK) {
        return -2;
    }

    // Is there room in the process table? What is the next PID?
    int i;
    for (i = 1; i <= MAXPROC; i++) {
        if (ProcTable[nextPid % 50].pid == -1) {
            procSlot = nextPid % 50;
            ProcTable[procSlot].pid = nextPid++;
            break;
        }
        nextPid++;
    }
    if (i == MAXPROC + 1) {
        if (DEBUG && debugflag) {
            USLOSS_Console("fork1(): no room ot ProcTable\n");
        }
        return -1;
    }
	if (DEBUG && debugflag) 
            USLOSS_Console("fork1(): new pid: %d for procSlot: %d\n",ProcTable[procSlot].pid,procSlot);
    // fill-in entry in process table */
    if ( strlen(name) >= (MAXNAME - 1) ) {
        USLOSS_Console("fork1(): Process name is too long.  Halting...\n");
        USLOSS_Halt(1);
    }
    strcpy(ProcTable[procSlot].name, name);
    ProcTable[procSlot].startFunc = startFunc;
    if ( arg == NULL )
        ProcTable[procSlot].startArg[0] = '\0';
    else if ( strlen(arg) >= (MAXARG - 1) ) {
        USLOSS_Console("fork1(): argument too long.  Halting...\n");
        USLOSS_Halt(1);
    }
    else
        strcpy(ProcTable[procSlot].startArg, arg);

    if (DEBUG && debugflag) {
        USLOSS_Console("fork1(): setting stackSize\n");
    }
    ProcTable[procSlot].stackSize = stacksize;

	//set priority
	ProcTable[procSlot].priority = priority;
	if (DEBUG && debugflag) {
        USLOSS_Console("fork1(): priority: %d\n",ProcTable[procSlot].priority);
    }
	
    
    if (DEBUG && debugflag) {
        USLOSS_Console("fork1(): malloc stack\n");
    }
    ProcTable[procSlot].stack = malloc(ProcTable[procSlot].stackSize);
    // Initialize context for this process, but use launch function pointer for
    // the initial value of the process's program counter (PC)

    USLOSS_ContextInit(&(ProcTable[procSlot].state),
                       ProcTable[procSlot].stack,
                       ProcTable[procSlot].stackSize,
                       NULL,
                       launch);

    // for future phase(s)
    p1_fork(ProcTable[procSlot].pid);

    // More stuff to do here...
    
    //set status
    ProcTable[procSlot].status = READY;
     
    //set parent
    ProcTable[procSlot].parent = Current;
    
    //set child list
    if(Current){
		if (DEBUG && debugflag) {
    		USLOSS_Console("fork1(): parent: %s\n",Current->name);
    	}
		
		if (Current->childProcPtr == NULL){
			if (DEBUG && debugflag) {
    			USLOSS_Console("fork1(): creating ChildList with: %s pid: %d\n",ProcTable[procSlot].name,
    																			ProcTable[procSlot].pid);
    		}
    		Current->childProcPtr = &ProcTable[procSlot];
    		Current->numberOfChildren +=1;
    	}else{
    		procPtr temp = Current->childProcPtr;
    		while(temp->nextSiblingPtr != NULL){ 
    			temp = temp->nextSiblingPtr;
    		}
    		if (DEBUG && debugflag) {
    			USLOSS_Console("fork1(): adding %s pid: %d to ChildList\n",ProcTable[procSlot].name,
    																	   ProcTable[procSlot].pid);
    		}
    		temp->nextSiblingPtr = &ProcTable[procSlot];
    		Current->numberOfChildren +=1;
   	 	}
   	 	
    }
    
    //New processes should be placed at the end of the list of processes with the same priority.		
    //add to Readylist in-order
    if (ReadyList == NULL){
    	if (DEBUG && debugflag) {
    		USLOSS_Console("fork1(): Creating ReadyList with: %s\n",ProcTable[procSlot].name);
    	}
    	ReadyList = &ProcTable[procSlot];
    }else if (ProcTable[procSlot].priority < ReadyList->priority ){
    	if (DEBUG && debugflag) {
    		USLOSS_Console("fork1(): Adding to HEAD of ReadyList: %s\n", ProcTable[procSlot].name);
    	}
    	ProcTable[procSlot].nextProcPtr = ReadyList;
    	ReadyList = &ProcTable[procSlot];
    }else {
    	procPtr temp = ReadyList;
    	while(temp->nextProcPtr != NULL){
			if (ProcTable[procSlot].priority < temp->nextProcPtr->priority){
				if (DEBUG && debugflag) {
					USLOSS_Console("fork1(): Adding %s p: %d to ReadyList in front of %s p: %d\n", 
								   ProcTable[procSlot].name,
								   ProcTable[procSlot].priority,
								   temp->nextProcPtr->name,
								   temp->nextProcPtr->priority);
				
				}
				ProcTable[procSlot].nextProcPtr = temp->nextProcPtr;
				temp->nextProcPtr = &ProcTable[procSlot];
				break;
			}
			temp = temp->nextProcPtr;
    	}
    	//end of list
//      if (DEBUG && debugflag) {
//     		USLOSS_Console("fork1(): ?Adding to END of ReadyList: %s\n", ProcTable[procSlot].name);
//     	}
//     	temp->nextProcPtr = &ProcTable[procSlot];
    }
    
    
    //DO NOT CALL dispatcher() on sentinel fork1()!!!
    if (ProcTable[procSlot].priority != 6){
    	if (DEBUG && debugflag) {
    		USLOSS_Console("fork1(): Calling dispatcher() on: %s \n", ProcTable[procSlot].name);
    	}
    	//disableInterrupts();
    	dispatcher();
    }

    return ProcTable[procSlot].pid;
} /* fork1 */

/* ------------------------------------------------------------------------
   Name - launch
   Purpose - Dummy function to enable interrupts and launch a given process
             upon startup.
   Parameters - none
   Returns - nothing
   Side Effects - enable interrupts
   ------------------------------------------------------------------------ */
void launch()
{
    int result;

    if (DEBUG && debugflag)
        USLOSS_Console("launch(): started\n");

    // Enable interrupts
	enableInterrupts();
	
    // Call the function passed to fork1, and capture its return value
    result = Current->startFunc(Current->startArg);

    if (DEBUG && debugflag)
        USLOSS_Console("Process %d returned to launch\n", Current->pid);

    quit(result);

} /* launch */


/* ------------------------------------------------------------------------
   Name - join
   Purpose - Wait for a child process (if one has been forked) to quit.  If 
             one has already quit, don't wait.
   Parameters - a pointer to an int where the termination code of the 
                quitting process is to be stored.
   Returns - the process id of the quitting child joined on.
             -1 if the process was zapped in the join
             -2 if the process has no children
   Side Effects - If no child process has quit before join is called, the 
                  parent is removed from the ready list and blocked.
   ------------------------------------------------------------------------ */
int join(int *status)
{
	if (DEBUG && debugflag)
        USLOSS_Console("join(): started\n");
	
	// test if in kernel mode; halt if in user mode
	checkKernelMode("join");
	
	//disable interupts
    //disableInterrupts();

    //join() function: There are three cases
    
    int unjoinedPid;
    //(1)The process has no children. (What happens?)
    if (Current->childProcPtr == NULL && Current->unjoinedChildrenProcPtr == NULL){
    	if (DEBUG && debugflag) {
    		USLOSS_Console("join(): process %s pid: %d has no children\n", Current->name,
    															           Current->pid);
    	}
    	return -2;
    }else {
    	if (Current->unjoinedChildrenProcPtr != NULL ){
    	//(2)Child(ren) quit() before the join() occurred
    	if (DEBUG && debugflag) {
    		USLOSS_Console("join(): %s has unjoined child: %s.\n", Current->name,
    															   Current->unjoinedChildrenProcPtr->name);
    	}
    	//	(a)Return the pid and quit status of one quit child 
    	*status = Current->unjoinedChildrenProcPtr->quitStatus;
    	unjoinedPid = Current->unjoinedChildrenProcPtr->pid;
    	
    	//	   and finish the clean up of that child’s process table entry.(later)
    	//	(b)Report on quit children in the order the children quit().(HEAD)    	
    	//  (see below)
    	}
    	//(3)No (unjoined) child has quit() ... must wait.
   		//	(a)how? After wait is over: return the pid and 
   		//			quit status of the child that quit.
   		//	(b)Where does the parent find these?
    	else { /*(Current->unjoinedChildrenProcPtr == NULL)*/
    		//WAIT: set status to JOIN and call dispatcher()
    		if (DEBUG && debugflag) {
    			USLOSS_Console("join(): %s will JOIN. stat: %d.\n", Current->name,
    															    Current->status);
    		}
    		
    		Current->status = JOIN;
    		
    		if (DEBUG && debugflag) {
    			USLOSS_Console("join(): calling dispatcher()\n");
    		}
    		
    		//reenable interupts
    		//enableInterrupts();
    		
    		dispatcher();
    		
    		//disable interrupts
    		//disableInterrupts();
    		
    		if (DEBUG && debugflag) {
    			USLOSS_Console("join(): continuing after JOIN\n");
    		}
    		
    		*status = Current->unjoinedChildrenProcPtr->quitStatus;
    		unjoinedPid = Current->unjoinedChildrenProcPtr->pid;
    	}//end (3)No (unjoined) child has quit() ... must wait.
    }//end of else Current->childProcPtr != NULL
    
	//update no of children after removal
	Current->numberOfChildren -=1;
		
	//cleanup ProcStruct
	Current->unjoinedChildrenProcPtr->quitStatus = 0;
	Current->unjoinedChildrenProcPtr->pid = -1;
	Current->unjoinedChildrenProcPtr->status = INIT;
	Current->unjoinedChildrenProcPtr->name[0] = '\0';
	
	//free stack memory
	if (DEBUG && debugflag) {
    	USLOSS_Console("join(): free() \n");
    }
	free(Current->unjoinedChildrenProcPtr->stack);
	
    //clear ptr to unjoinedQuitChildren list
	Current->unjoinedChildrenProcPtr = Current->unjoinedChildrenProcPtr->unjoinedSiblingProcPtr;
	
	//reenable interupts
    //enableInterrupts();
    
    //the process was zapped while waiting for a child to quit RETURN -1
    if(0 /*isZapped()*/) return -1;  // -1 is not correct! Here to prevent warning.
	
	return unjoinedPid;
} /* join */


/* ------------------------------------------------------------------------
   Name - quit
   Purpose - Stops the child process and notifies the parent of the death by
             putting child quit info on the parents child completion code
             list.
   Parameters - the code to return to the grieving parent
   Returns - nothing
   Side Effects - changes the parent of pid child completion status list.
   ------------------------------------------------------------------------ */
void quit(int status)
{	
	if (DEBUG && debugflag){
        USLOSS_Console("quit(): starting\n");
    }
	// test if in kernel mode; halt if in user mode
	checkKernelMode("quit");
	
	//disable interrupts
	disableInterrupts();
	
	//Error if a process with active children calls quit(). Halt USLOSS with 
	//appropriate error message.
	if (Current->childProcPtr != NULL){
    	if (DEBUG && debugflag){
    		USLOSS_Console("Process %d has children! Halting.\n", Current->pid);
    	}
    	USLOSS_Halt(1);
    }
    
    //set quitStatus
    Current->quitStatus = status;
    
	//(1)Parent has already done a join(), OR		
    //(2)Parent has not (yet) done a join()

    //add child to unjoinedChildrenProcPtr
    if (Current->parent && Current->parent->unjoinedChildrenProcPtr == NULL){
    	//(1)Parent has already done a join(), OR		
    	if (DEBUG && debugflag) {
    		USLOSS_Console("quit(): creating %s as   HEAD of %s's unjoinedChildren list.\n", 
    						Current->name,
    						Current->parent->name);
    	}
    	Current->parent->unjoinedChildrenProcPtr = Current;
    }
    else if (Current->parent){
    	//(2)Parent has not (yet) done a join()
    	procPtr temp = Current->parent->unjoinedChildrenProcPtr;
        while (temp->unjoinedSiblingProcPtr != NULL){
    		temp = temp->unjoinedSiblingProcPtr;
    	}
    	if (DEBUG && debugflag) {
    		USLOSS_Console("quit(): adding %s to     END of %s's unjoinedChildren List.\n", 
    					    Current->name,
    					    Current->parent->name);
    	}
    	temp->unjoinedSiblingProcPtr = Current;
    }
    
    if (Current->parent && Current->parent->status == JOIN){
    	//set parent to ready from JOIN
    	Current->parent->status = READY;
	}
    
    
    //remove yourself from Current->parent->childProcPtr list
    if (Current->parent && Current->parent->childProcPtr->pid == Current->pid){
    	if (DEBUG && debugflag) {
    		USLOSS_Console("quit(): removing %s from HEAD of %s's childProcPtr list\n", 
    						Current->name,
    						Current->parent->name);
    	}
    	Current->parent->childProcPtr = Current->parent->childProcPtr->nextSiblingPtr;
    }
    else if (Current->parent){
    	procPtr temp = Current->parent->childProcPtr;
    	while(temp->nextSiblingPtr != NULL){
    		if (temp->nextSiblingPtr->pid == Current->pid){
    			if (DEBUG && debugflag) {
    				USLOSS_Console("quit(): removing %s from %s's childProcPtr list\n", 
    								Current->name,
    								Current->parent->name);
    			}
    			temp->nextSiblingPtr = temp->nextSiblingPtr->nextSiblingPtr;
    			break;
    		}
    		temp = temp->nextSiblingPtr;
    	}
    	
    }
    
    if (DEBUG && debugflag) {
    		USLOSS_Console("quit(): cleaning up process table.\n", Current->name);
    }
    //Cleanup the process table entry (but not entirely, see join()
    //Current->nextProcPtr = NULL;       /*MOVED TO AFTER ReadyList remove*/
    //ProcTable[i].childProcPtr = NULL;
	Current->nextSiblingPtr = NULL;
    //Current->name[0] = '\0'; JOIN
	Current->startArg[0] = '\0';
    //state
    //ProcTable[i].pid = -1; JOIN needs
    Current->priority = -1;
   	Current->startFunc = NULL;
   	//free(Current->stack); JOIN
   	//ProcTable[i].stackSize = 0;	
   	Current->stackSize = 0;
    Current->status = QUIT;
	/* other fields as needed... */
   	//ProcTable[i].quitStatus = 0; JOIN needs
   	//Current->parent == NULL;
   	//Current->unjoinedChildrenProcPtr = NULL;
	//Current->unjoinedSiblingProcPtr = NULL;
   	//Current->numberOfChildren = 0;
    
    //Unblock processes that zap’d this process
    
    
    //May have children who have quit(), but for whom a join() was not 
    //(and now will not) be done (This isnt error)
    //TODO clear unjoinedChildrenProcPtr
    
    //Current->unjoinedChildrenProcPtr = NULL;
	//Current->unjoinedSiblingProcPtr = NULL;
    
    
    //Remove quit() process from Readylist in-order
    if (Current->pid == ReadyList->pid ){
    	if (DEBUG && debugflag) {
    		USLOSS_Console("quit(): removing %s as HEAD of ReadyList.\n", Current->name);
    	}
    	ReadyList = Current->nextProcPtr;
    }else {
    	procPtr temp = ReadyList;
    	while(temp->nextProcPtr != NULL){
			if (Current->pid == temp->nextProcPtr->pid){
				if (DEBUG && debugflag) {
					USLOSS_Console("quit(): removing %s from ReadyList in front of %s\n", 
								   temp->nextProcPtr->name,
								   temp->nextProcPtr->nextProcPtr->name);
				
				}
				temp->nextProcPtr = temp->nextProcPtr->nextProcPtr;
				break;
			}
			temp = temp->nextProcPtr;
    	}
    	//end of list
    }
    
    //clean up
    Current->nextProcPtr = NULL;
    
    p1_quit(Current->pid);

	if (DEBUG && debugflag) {
    		USLOSS_Console("quit(): calling dispatcher().\n");
    }
    
    //enable interrupts
    enableInterrupts();
     
    dispatcher();
    
} /* quit */


/* ------------------------------------------------------------------------
   Name - dispatcher
   Purpose - dispatches ready processes.  The process with the highest
             priority (the first on the ready list) is scheduled to
             run.  The old process is swapped out and the new process
             swapped in.
   Parameters - none
   Returns - nothing
   Side Effects - the context of the machine is changed
   ----------------------------------------------------------------------- */
void dispatcher(void)
{
    if (DEBUG && debugflag) {
    		USLOSS_Console("dispatcher(): starting\n");
    }
    
    checkKernelMode("dispatcher");
    
    disableInterrupts();
    
    //initial call of USLOSS_ContextSwitch
    if(!Current){
    	Current = ReadyList;
    	
    	p1_switch(0, Current->pid);
    	
    	//TODO make Current ptr be "running" status
    	Current->status = RUNNING;
    	
    	if ( USLOSS_DeviceInput(USLOSS_CLOCK_DEV, 0, &Current->startTime) == USLOSS_DEV_OK ){
    		if (DEBUG && debugflag) 
    			USLOSS_Console("dispatcher(): USLDevInp() initial setting of %s's startTime: %d μs\n",
    							Current->name,
    							Current->startTime);
    	}
    	
    	if (DEBUG && debugflag) {
    		USLOSS_Console("dispatcher(): USLOSS_ContextSwitch(NULL, %s)\n",Current->name);
    	}
    	
    	//enable interrupts or does launch() do this?
    	//enableInterrupts();
    	
    	USLOSS_ContextSwitch(NULL, &Current->state);
    }
    
    procPtr temp;
    int rt = readtime();
    //if (DEBUG && debugflag) 
	//	USLOSS_Console("dispatcher(): %s's readtime: %d\n", Current->name, rt);
    if (rt > 80000){
    	if (DEBUG && debugflag) 
    			USLOSS_Console("dispatcher(): readtime(): %dμs > 80000 μs. Pulling Current off ReadyList\n", rt);
    	//Current to end of its priority
    	if(Current->pid == ReadyList->pid){
    		ReadyList = Current->nextProcPtr;
    	}else{
    		//walk downlist looking for prior
    		procPtr prior = ReadyList;
    		while (prior->nextProcPtr != NULL){
    			if (prior->nextProcPtr->pid == Current->pid) break;
    			prior = prior->nextProcPtr;
    		}
    		prior->nextProcPtr = Current->nextProcPtr;
    	}
    	
    if (DEBUG && debugflag) 
    	USLOSS_Console("dispatcher(): putting Current at end of its priority\n", rt);
    //add Current to end of its priority list on the ReadyList
    if (ReadyList == NULL){
    	if (DEBUG && debugflag) {
    		USLOSS_Console("dispatcher(): ReadyList is %s\n", Current->name);
    	}
    	ReadyList = Current;
    }else if (Current->priority < ReadyList->priority ){
    	if (DEBUG && debugflag) {
    		USLOSS_Console("dispatcher(): ReadyList HEAD: %s\n", Current->name);
    	}
    	Current->nextProcPtr = ReadyList;
    	ReadyList = Current;
    }else {
    	temp = ReadyList;
    	while(temp->nextProcPtr != NULL){
			if (Current->priority < temp->nextProcPtr->priority){
				if (DEBUG && debugflag) {
					USLOSS_Console("dispatcher(): Adding %s pri: %d to ReadyList ahead of %s pri: %d\n", 
								   Current->name,
								   Current->priority,
								   temp->nextProcPtr->name,
								   temp->nextProcPtr->priority);
				
				}
				Current->nextProcPtr = temp->nextProcPtr;
				temp->nextProcPtr = Current;
				break;
			}
			temp = temp->nextProcPtr;
    	}
    	//end of list
    }
    }//end rt > 80000
    
    
    
    
    procPtr nextProcess = NULL;
    temp = ReadyList;
    
    //determine next Process to run
    if ((temp->priority <= Current->priority && temp->status != JOIN) || 
    	 temp->status != JOIN){
    	nextProcess = temp;

    }
    else{
    	while (temp->nextProcPtr != NULL){
    		if((temp->nextProcPtr->priority <= Current->priority && temp->status != JOIN )||
    		    temp->status != JOIN){
    			nextProcess = temp;
    			break;
    		}
   			temp = temp->nextProcPtr;
    	}
    }
    
    //nothing has higher priority than Current
    if(!nextProcess ){
    	if (DEBUG && debugflag) {
    		USLOSS_Console("dispatcher(): nextProcess NULL. returning to %s.\n", Current->name);
    	} 
    	return;
    }else if (nextProcess->pid == Current->pid){
    	if (DEBUG && debugflag) {
    		USLOSS_Console("dispatcher(): nextProcess == Current. returning to %s.\n", Current->name);
    	}
    	return;
    }
    
    
    
    // TODO let Current denote RUNNING
    if (Current->status == RUNNING)
	Current->status = READY;
	if (nextProcess->status == READY)
	nextProcess->status = RUNNING;
    	
    //update Current to new process
    temp = Current;
    Current = nextProcess;	
    
    p1_switch(temp->pid, Current->pid);
    
    // set current time
    if ( USLOSS_DeviceInput(USLOSS_CLOCK_DEV, 0, &Current->startTime) == USLOSS_DEV_OK ){
    	if (DEBUG && debugflag) 
    		USLOSS_Console("dispatcher(): updating %s's startTime: %d μs\n",
    					    Current->name,
    						Current->startTime);
    }
    
    if (DEBUG && debugflag) {
    		USLOSS_Console("dispatcher(): USLOSS_ContextSwitch(%s, %s)\n", 
    						temp->name,
    						Current->name);
    }
    //enable interrupts or does launch() do this?
    //enableInterrupts();
    
    USLOSS_ContextSwitch(&temp->state, &Current->state);
        
} /* dispatcher */


/* ------------------------------------------------------------------------
   Name - sentinel 
   Purpose - The purpose of the sentinel routine is two-fold.  One
             responsibility is to keep the system going when all other
             processes are blocked.  The other is to detect and report
             simple deadlock states.
   Parameters - none
   Returns - nothing
   Side Effects -  if system is in deadlock, print appropriate error
                   and halt.
   ----------------------------------------------------------------------- */

int sentinel (char *dummy)
{
    if (DEBUG && debugflag)
        USLOSS_Console("sentinel(): called\n");
    while (1)
    {
        checkDeadlock();
        USLOSS_WaitInt();
    }
    return -100;
} /* sentinel */


/* check to determine if deadlock has occurred... */
static void checkDeadlock()
{
	//TODO walk ReadyList and check for blocked process or join/waiting
    checkKernelMode("checkDeadlock");
	
	if (ReadyList->priority == 6){
		if (DEBUG && debugflag){
        	USLOSS_Console("sentinel(): called checkDeadlock().\n");
        	USLOSS_Console("sentinel(): calling halt(0).\n");
        }
        
        USLOSS_Console("All processes completed.\n");
        ReadyList = NULL;
        USLOSS_Halt(0);
	}else{
		if (DEBUG && debugflag){
        	USLOSS_Console("sentinel(): called checkDeadlock().\n");
        	USLOSS_Console("sentinel(): calling halt(1).\n");
        }
        USLOSS_Halt(1);
	}
	
} /* checkDeadlock */


/*
 * Disables the interrupts.
 */
int disableInterrupts()
{
    // turn the interrupts OFF iff we are in kernel mode
    // if not in kernel mode, print an error message and
    // halt USLOSS
    int cur_mode = USLOSS_PsrGet();
    if ((cur_mode & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("Not in kernel mode");
        USLOSS_Console("halting...\n");
        USLOSS_Halt(1);
    }
    
    return USLOSS_PsrSet(cur_mode & ~USLOSS_PSR_CURRENT_INT);
} /* disableInterrupts */

int enableInterrupts()
{
    int cur_mode = USLOSS_PsrGet();
    if ((cur_mode & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("Not in kernel mode");
        USLOSS_Console("halting...\n");
        USLOSS_Halt(1);
    }

    return USLOSS_PsrSet(cur_mode | USLOSS_PSR_CURRENT_INT);
}

void checkKernelMode(char *nameOfFunc)
{
    // test if in kernel mode; halt if in user mode
    int cur_mode = USLOSS_PsrGet();
    if (DEBUG && debugflag)
        USLOSS_Console("%s(): psr is %d\n", nameOfFunc, cur_mode);

    if ((cur_mode & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("%s(): called while in user mode, by process %d. ", nameOfFunc, Current->pid);
        USLOSS_Console("Halting...\n");
        USLOSS_Halt(1);
    }
}


void dumpProcesses()
{ 
    checkKernelMode("dumpProcesses");
    USLOSS_Console("PROC\tPID\tPPID\tPRIOR\tSTATUS\t#CH\tNAME\n");
    for (int i = 1; i <= MAXPROC; i++) {
        int index = i % MAXPROC;
        USLOSS_Console("%d:\t", i);
        USLOSS_Console("%d\t", ProcTable[index].pid);
        USLOSS_Console("%d\t", ProcTable[index].parent ? ProcTable[index].parent->pid : -1);
        USLOSS_Console("%d\t", ProcTable[index].priority);
        USLOSS_Console("%d\t", ProcTable[index].status);
        USLOSS_Console("%d\t", ProcTable[index].numberOfChildren);
        USLOSS_Console("%s", ProcTable[index].name);
        USLOSS_Console("\n");
    }
}

int getpid() {
    checkKernelMode("getpid");
    return Current->pid;
}

int zap(int pid) {
    checkKernelMode("zap");
    disableInterrupts();

    if (DEBUG && debugflag)
        USLOSS_Console("Checking if process tried to zap itself\n");

    if (Current == &ProcTable[pid]) {
        USLOSS_Console("Process tried to zap itself\n");
        USLOSS_Halt(1);
    }

    if (DEBUG && debugflag)
        USLOSS_Console("Checking if trying to zap a nonexistent process\n");

    if (ProcTable[pid].pid == -1) {
        USLOSS_Console("Tried to zap a nonexistent process\n");
        USLOSS_Halt(1);
    }

    ProcTable[pid].zapStatus = 1; // mark process as zapped
    Current->status = ZAPBLOCK;

    // add ProcTable[pid] to end of Current->zappingList
    procPtr ref = Current->zappingList;
    if (ref == NULL) { // no current processes zapped
        Current->zappingList = &ProcTable[pid];
    }
    else {
        while (ref->nextZapping != NULL) { // walk down zapping list to the end
            ref = ref->nextZapping;
        }
        // add ProcTable[pid] to end of list
        ref->nextZapping = &ProcTable[pid];
        ref->nextZapping->nextZapping = NULL; // end list to avoid circular refs
    }

    // Current process was zapped wile in zap
    if (isZapped()) {
        return -1;
    }

    dispatcher();

    enableInterrupts();
    return 0;
}

int isZapped() {
    checkKernelMode("isZapped");
    return Current->zapStatus;
}

int blockMe(int newStatus) {
    checkKernelMode("blockMe");

    if (newStatus <= 10) {
        USLOSS_Console("blockMe(): newStatus not greater than 10\n");
        USLOSS_Console("halting...\n");
        USLOSS_Halt(1);
    }
    
    if (isZapped()) return -1;

    Current->status = newStatus;

    return 0;
}

// int USLOSS_DeviceInput(int dev, int unit, int *status);
// kernel mode Sets *status to the contents of the device status register indicated by dev and unit. 
// If dev and unit are both valid, USLOSS_DEV_OK is returned; otherwise, USLOSS_DEV_INVALID is returned.

// Since there is only one clock device, the unit number is zero.
// USLOSS_CLOCK_DEV "usloss.h" line 75

// Interrupt vector is defined by USLOSS as an array of pointers to void functions with 2 integer arguments:
//   extern void (*USLOSS_IntVec[NUM_INTS]) (int dev, int  unit); /* from usloss.h */
// Checks if the current process has exceeded its time slice. Calls dispatcher() if necessary.
// Time slice is 80 ms (milliseconds).
// The USLOSS_DeviceInput(int dev, int unit, int *status) function returns time in microseconds (= 1,000 ms); 
// thus, time slice is 80,000 μs
void clockHandler(int dev, void *arg){   /*int dev, void *arg (see usloss.h line 64)*/
	int timeUsed = readtime();
	if (timeUsed == -7){
	   USLOSS_Console("readtime(): ERROR\n");
	   return;
	}
	if (DEBUG && debugflag)
    	USLOSS_Console("clockHandler(): readtime(): %d \n",timeUsed);
	if (timeUsed > 80000){
		dispatcher();
	}
}//end clockHandler

// Return the CPU time (in milliseconds) used by the current process. 
// This means that the kernel must record the amount of processor time used by each process. 
// Do not use the clock interrupt to measure CPU time as it is too coarse-grained; 
// use USLOSS_DeviceInput to get the current time from the USLOSS clock (see Section 4.1 of the USLOSS manual).
//
//(4.1)The clock device has a 32-bit status register that is accessed via USLOSS_DeviceInput. 
//	   This register contains the time (in microseconds) since USLOSS started running.
int readtime(){
	int currentTime = -1;
	
	// if (DEBUG && debugflag) 
	// USLOSS_Console("readtime(): called USLOSS_DeviceInput()\n");
    
	if (USLOSS_DeviceInput(USLOSS_CLOCK_DEV, 0, &currentTime) == USLOSS_DEV_OK){
		// if (DEBUG && debugflag)
//     		USLOSS_Console("USLOSS_DeviceInput() currentTime: %d startTime: %d\n",
//     						currentTime, 
//     						Current->startTime);
		
		return currentTime - Current->startTime;
	}
	
	return -7;
}//end readtime

// This operation returns the time (in microseconds) at which the currently
// executing process began its current time slice.
int readCurStartTime(void){
	return Current->startTime;
}//end readCurStartTime

// This operation calls the dispatcher if the currently executing process has exceeded
// its time slice; otherwise, it simply returns.
void timeSlice(void){
	int currentTime;
	
	if (USLOSS_DeviceInput(USLOSS_CLOCK_DEV, 0, &currentTime) != USLOSS_DEV_OK){
		if (DEBUG && debugflag)
    		USLOSS_Console("USLOSS_DeviceInput() != USLOSS_DEV_OK \n");
	}
	if ( readtime() > 80000 ) dispatcher();
}//end timeSlice

void instructionHandler(int dev, void *arg) {

}
