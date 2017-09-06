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
		procTable[i].nextProcPtr = NULL; 			/*procPtr*/
        procTable[i].childProcPtr = NULL;			/*procPtr*/
        procTable[i].nextSiblingPtr = NULL; 		/*procPtr*/
        procTable[i].name = NULL;     				/* char *: process's name */
        procTable[i].startArg = NULL;  				/* char: args passed to process */
     	procTable[i].state = NULL;          		/* USLOSS_Context: current context for process */
        procTable[i].pid = -1;              		/* short: process id */
        procTable[i].priority = -1;  				/* int */
   		procTable[i].startFunc = NULL;   			/* int (* startFunc) (char *): function where process begins -- launch */
   		procTable[i].stack = NULL; 					/* Char* */
   		procTable[i].stackSize = 0;					/* unsigned int    */
        procTable[i].status = -1;        			/* int: READY, BLOCKED, QUIT, etc. */
   		/* other fields as needed... */
   		procTable[i].quitStatus = 0;				/*process quit(quitStatus); */
   		procTable[i].parent = NULL;					/*a process' parent ptr */
   		procTable[i].unjoinedQuitChildren = NULL; 	/*procPtr of quit children pre-join 
   		
	}
    
    // Initialize the Ready list, etc.
    if (DEBUG && debugflag)
        USLOSS_Console("startup(): initializing the Ready list\n");
    ReadyList = procTable[nextPid];

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
	if(name == NULL || startFunc == NULL || {
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

    // test if in kernel mode; halt if in user mode
	int cur_mode = USLOSS_PsrGet();
    if (DEBUG && debugflag)
        USLOSS_Console("fork1(): psr is %d\n", cur_mode);

    if ((cur_mode & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("fork1(): current mode not kernel\n");
        USLOSS_Console("halting...\n");
        USLOSS_Halt(0);
    }
    // Return if stack size is too small
    if (stacksize < USLOSS_MIN_STACK) {
        return -2;
    }

    // Is there room in the process table? What is the next PID?
	int counter = 0;
	while(1){
		if (procTable[nextPid % 50].pid != -1){
			procSlot = nextPid % 50;
			procTable[procSlot].pid = nextPid;
			break;
		} else {
			nextPid++;
		}
		//no empty slots in the process table
		if (counter++ > 48){
			USLOSS_Console("fork1(): No empty slots in the process table.\n");
			return -1;
		}
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

    return nextPid++;
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
	// test if in kernel mode; halt if in user mode
	int cur_mode = USLOSS_PsrGet();
    if (DEBUG && debugflag)
        USLOSS_Console("join(): psr is %d\n", cur_mode);

    if ((cur_mode & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("join(): current mode not kernel\n");
        USLOSS_Console("halting...\n");
        USLOSS_Halt(0);
    }
    
    
    //join() function: There are three cases
    
    //(1)The process has no children. (What happens?)
    if (Current->childProcPtr == NULL){
    	USLOSS_Console("Process %d has no children\n", Current->pid);
    	return -2;
    }
    
    //(2)Child(ren) quit() before the join() occurred
    //	(a)Return the pid and quit status of one quit child 
    //	   and finish the clean up of that child’s process table entry.
    //	(b)Report on quit children in the order the children quit().
    //if (Current->unjoinedQuitChildren != NULL){
    	//*status = Current->unjoinedQuitChildren->quitStatus;
    	//int unjoinedPid = Current->unjoinedQuitChildren->pid;
    	//cleanup ProcStruct;
    	//
    	//return unjoinedPid;
    //}
    
    //(3)No (unjoined) child has quit() ... must wait.
    //	(a)how? After wait is over: return the pid and 
    //			quit status of the child that quit.
    //	(b)Where does the parent find these?
    
    //blockMe(JOINING) //#define JOINING 2 /*or something */
    
    //the process was zapped while waiting for a child to quit RETURN -1
    return -1;  // -1 is not correct! Here to prevent warning.
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
	// test if in kernel mode; halt if in user mode
	int cur_mode = USLOSS_PsrGet();
    if (DEBUG && debugflag)
        USLOSS_Console("quit(): psr is %d\n", cur_mode);

    if ((cur_mode & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("quit(): current mode not kernel\n");
        USLOSS_Console("halting...\n");
        USLOSS_Halt(0);
    }
    
	//Error if a process with active children calls quit(). Halt USLOSS with 
	//appropriate error message.
	if (Current->childProcPtr != NULL){
    	USLOSS_Console("Process %d has children! Halting.\n", Current->pid);
    	USLOSS_Halt(1);
    }
    
    //Cleanup the process table entry (but not entirely, see join()
    //	(1)Parent has already done a join(), OR
    //	(2)Parent has not (yet) done a join()
    
    //Unblock processes that zap’d this process
    
    
    //May have children who have quit(), but for whom a join() was not 
    //(and now will not) be done (This isnt error)
    p1_quit(Current->pid);
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
    procPtr nextProcess = NULL;

    p1_switch(Current->pid, nextProcess->pid);
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
} /* checkDeadlock */


/*
 * Disables the interrupts.
 */
void disableInterrupts()
{
    // turn the interrupts OFF iff we are in kernel mode
    // if not in kernel mode, print an error message and
    // halt USLOSS

} /* disableInterrupts */
