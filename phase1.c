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



/* -------------------------- Globals ------------------------------------- */

// Patrick's debugging global variable...
int debugflag = 1;

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
   		ProcTable[i].numberOfChildren = 0;
   		
	}
    
    // Initialize the Ready list, etc.
    if (DEBUG && debugflag)
        USLOSS_Console("startup(): initializing the Ready list\n");
    ReadyList = NULL;

    // Initialize the clock interrupt handler

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
	
	if(name == NULL || startFunc == NULL ){
		USLOSS_Console("fork1(): name is NULL\n");
		return -1; 
	} 
	if(startFunc == NULL){
		USLOSS_Console("fork1(): startFunc is NULL\n");
		return -1; 
	}
	if (priority < 1 || priority > 6){
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
        if (ProcTable[i % 50].pid == -1) {
            procSlot = i;
            ProcTable[i].pid = nextPid++;
            break;
        }
    }
    if (i == MAXPROC + 1) {
        if (DEBUG && debugflag) {
            USLOSS_Console("fork1(): no room ot ProcTable\n");
        }
        return -1;
    }

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
    			USLOSS_Console("fork1(): Creating ChildList with: %s pid: %d\n",ProcTable[procSlot].name,
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
    	dispatcher();
    }

    return procSlot;
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
	//disable interupts
    //disableInterrupts();
	
	if (DEBUG && debugflag)
        USLOSS_Console("join(): started\n");
	
	// test if in kernel mode; halt if in user mode
	checkKernelMode("join");

    //join() function: There are three cases
    
    int unjoinedPid;
    //(1)The process has no children. (What happens?)
    if (Current->childProcPtr == NULL){
    	if (DEBUG && debugflag) {
    		USLOSS_Console("Process %d has no children\n", Current->pid);
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
    
	//remove child from Current->childProcPtr list
    if (Current->childProcPtr->pid == unjoinedPid){
    	if (DEBUG && debugflag) {
    		USLOSS_Console("join(): removing %s HEAD of childProcPtr list\n", Current->childProcPtr->name);
    	}
    	Current->childProcPtr = Current->childProcPtr->nextSiblingPtr;
    }
    else {
    	procPtr temp = Current->childProcPtr;
    	while(temp->nextSiblingPtr != NULL){
    		if (temp->nextSiblingPtr->pid == unjoinedPid){
    			if (DEBUG && debugflag) {
    				USLOSS_Console("join(): removing %s from childProcPtr list\n", Current->childProcPtr->name);
    			}
    			temp->nextSiblingPtr = temp->nextSiblingPtr->nextSiblingPtr;
    			break;
    		}
    		temp = temp->nextSiblingPtr;
    	}
    	
    }
	
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
	//Current->unjoinedChildrenProcPtr = NULL;
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
	//disable interrupts
	disableInterrupts();
	
	if (DEBUG && debugflag){
        USLOSS_Console("quit(): starting\n");
    }
	// test if in kernel mode; halt if in user mode
	checkKernelMode("quit");
	
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
    		USLOSS_Console("quit(): Creating unjoinedChildren List with %s.\n", Current->name);
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
    		USLOSS_Console("quit(): Adding %s to END of unjoinedChildren List.\n", Current->name);
    	}
    	temp = Current;
    }
    
    if (Current->parent && Current->parent->status == JOIN){
    	//set parent to ready from JOIN
    	Current->parent->status = READY;
	}
    
    if (DEBUG && debugflag) {
    		USLOSS_Console("quit():Cleaning up process table.\n", Current->name);
    }
    //Cleanup the process table entry (but not entirely, see join()
    //Current->nextProcPtr = NULL;       /*MOVED TO AFTER ReadyList remove*/
    //ProcTable[i].childProcPtr = NULL;
	//ProcTable[i].nextSiblingPtr = NULL;
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
   	Current->numberOfChildren = 0;
    
    //Unblock processes that zap’d this process
    
    
    //May have children who have quit(), but for whom a join() was not 
    //(and now will not) be done (This isnt error)
    //TODO clear unjoinedChildrenProcPtr
    
    //Current->unjoinedChildrenProcPtr = NULL;
	//Current->unjoinedSiblingProcPtr = NULL;
    
    
    //Remove quit() process from Readylist in-order
    if (Current->pid == ReadyList->pid ){
    	if (DEBUG && debugflag) {
    		USLOSS_Console("quit(): Removing HEAD of ReadyList: %s\n", Current->name);
    	}
    	ReadyList = Current->nextProcPtr;
    }else {
    	procPtr temp = ReadyList;
    	while(temp->nextProcPtr != NULL){
			if (Current->pid == temp->nextProcPtr->pid){
				if (DEBUG && debugflag) {
					USLOSS_Console("quit(): Removing %s from ReadyList in front of %s\n", 
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

    //enable interrupts
    enableInterrupts();
     
    if (DEBUG && debugflag) {
    		USLOSS_Console("quit(): calling dispatcher().\n");
    }
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
    disableInterrupts();
    if (DEBUG && debugflag) {
    		USLOSS_Console("dispatcher(): starting\n");
    }
    
    checkKernelMode("dispatcher");
    
    //initial call of USLOSS_ContextSwitch
    if(!Current){
    	Current = ReadyList;
    	
    	p1_switch(0, Current->pid);
    	//enable interrupts
    	
    	Current->status = RUNNING;
    	if (DEBUG && debugflag) {
    		USLOSS_Console("dispatcher(): USLOSS_ContextSwitch(NULL, %s)\n",Current->name);
    	}
    	USLOSS_ContextSwitch(NULL, &Current->state);
    }
    
   
    procPtr nextProcess = NULL;
    procPtr temp = ReadyList;
    
    //determine next Process to run
    if ((temp->priority <= Current->priority && temp->status != JOIN)|| 
    	 temp->status != JOIN ){
    	nextProcess = temp;

    }
    else{
    	while (temp->nextProcPtr != NULL){
    		if((temp->nextProcPtr->priority <= Current->priority && temp->status != JOIN)||
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
    
    if (DEBUG && debugflag) {
    		USLOSS_Console("dispatcher(): USLOSS_ContextSwitch(%s, %s)\n", 
    		Current->name,
    		nextProcess->name);
    }
    
    if (Current->status == RUNNING)
    	Current->status = READY;
    if (nextProcess->status == READY)
    	nextProcess->status = RUNNING;
    	
    temp = Current;
    Current = nextProcess;	
    
    p1_switch(Current->pid, nextProcess->pid);
    
    //enable interrupts
    enableInterrupts();

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
} /* sentinel */


/* check to determine if deadlock has occurred... */
static void checkDeadlock()
{
	if (ReadyList->priority == 6){
		if (DEBUG && debugflag){
        	USLOSS_Console("sentinel(): called checkDeadlock().\n");
        	USLOSS_Console("sentinel(): calling halt(0).\n");
        }
        //Cleanup the process table entry (but not entirely, see join()
    	Current->nextProcPtr = NULL;       /*MOVED TO AFTER ReadyList remove*/
    	Current->childProcPtr = NULL;
		Current->nextSiblingPtr = NULL;
    	Current->name[0] = '\0';
		Current->startArg[0] = '\0';
    	//state
    	Current->pid = -1; 
    	Current->priority = -1;
   		Current->startFunc = NULL;
   		free(Current->stack); //JOIN
   		Current->stackSize = 0;	
   		Current->stackSize = 0;
    	Current->status = QUIT;
		/* other fields as needed... */
   		Current->quitStatus = 0;
   		//Current->parent == NULL;
   		Current->unjoinedChildrenProcPtr = NULL;
		Current->unjoinedSiblingProcPtr = NULL;
   		Current->numberOfChildren = 0;
        
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
        USLOSS_Console("%s(): current mode not kernel\n", nameOfFunc);
        USLOSS_Console("halting...\n");
        USLOSS_Halt(1);
    }
}



//OLD WORKING CODE
//    (1)Parent has already done a join(), OR
//     if (Current->parent && Current->parent->status == JOIN){
//     	if (DEBUG && debugflag) {
//     		USLOSS_Console("quit(): parent: %s stat: %d.\n", Current->parent->name, Current->parent->status);
//     	}
//     	Current->parent->unjoinedChildrenProcPtr = Current;
//     	
//     	set parent to ready from JOIN
//     	Current->parent->status = READY;
//     	
//     (2)Parent has not (yet) done a join()
//     }else { 
//     	add Current to Current->parent.unjoinedChildProcPtr list
//     	if (Current->parent && Current->parent->unjoinedChildrenProcPtr == NULL){
//     		if (DEBUG && debugflag) {
//     			USLOSS_Console("quit(): Creating unjoinedChildren List with %s.\n", Current->name);
//     		}
//     		Current->parent->unjoinedChildrenProcPtr = Current;
//     	}
//     	else if (Current->parent){
//     		procPtr temp = Current->parent->unjoinedChildrenProcPtr;
//     		while (temp->unjoinedSiblingProcPtr != NULL){
//     			temp = temp->unjoinedSiblingProcPtr;
//     		}
//     		if (DEBUG && debugflag) {
//     			USLOSS_Console("quit(): Adding %s to END of unjoinedChildren List.\n", Current->name);
//     		}
//     		temp = Current;
//     	}
//     }

// if (Current->unjoinedChildrenProcPtr != NULL ){
//     	//(2)Child(ren) quit() before the join() occurred
//     	//	(a)Return the pid and quit status of one quit child 
//     	//	   and finish the clean up of that child’s process table entry.
//     	//	(b)Report on quit children in the order the children quit().
//     	//if (Current->unjoinedQuitChildren != NULL && Current->status != JOIN){
//     	//*status = Current->unjoinedChildrenProcPtr->quitStatus;
//     	//int unjoinedPid = Current->unjoinedChildrenProcPtr->pid;
//     	
//     	//cleanup ProcStruct
//     	//procPtr cleanup = Current->unjoinedQuitChildren;
//     	//Current->unjoinedChildrenProcPtr = Current->unjoinedChildrenProcPtr->unjoinedSiblingProcPtr;
//     	
//     	//return unjoinedPid;
//     	}
//     	//(3)No (unjoined) child has quit() ... must wait.
//    		//	(a)how? After wait is over: return the pid and 
//    		//			quit status of the child that quit.
//    		//	(b)Where does the parent find these?
//     	else { /*(Current->unjoinedChildrenProcPtr == NULL)*/
//     		//WAIT: set status to JOIN and call dispatcher()
//     		if (DEBUG && debugflag) {
//     			USLOSS_Console("join(): %s wil JOIN. stat: %d.\n", Current->name,
//     															   Current->status);
//     		}
//     		
//     		Current->status = JOIN;
//     		
//     		if (DEBUG && debugflag) {
//     			USLOSS_Console("join(): calling dispatcher()\n");
//     		}
//     		dispatcher();
//     		
//     		if (DEBUG && debugflag) {
//     			USLOSS_Console("join(): continuing after JOIN\n");
//     		}
//     	}//end (3)No (unjoined) child has quit() ... must wait.
//     }//end of else Current->childProcPtr != NULL



