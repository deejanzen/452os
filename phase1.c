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
void checkKernelMode();
void enableInterrupts();
void disableInterrupts();
static void checkDeadlock();
void newProcList(procList *p);
void enqueue(procList *p, procPtr);
procPtr dequeue(procList *p);
procPtr peek(procList *p);
void removeChild(procList *p, procPtr);

/* -------------------------- Globals ------------------------------------- */

// Patrick's debugging global variable...
int debugflag = 1;

// the process table
procStruct ProcTable[MAXPROC];

// Process lists
static procList ReadyList[SENTINELPRIORITY];

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

    checkKernelMode();

    /* initialize the process table */
    if (DEBUG && debugflag)
        USLOSS_Console("startup(): initializing process table, ProcTable[]\n");
    for (int i = 0; i < MAXPROC; i++) {
        ProcTable[i].nextProcPtr = NULL;
        ProcTable[i].childProcPtr = NULL;
        ProcTable[i].nextSiblingPtr = NULL;
        ProcTable[i].name[0] = '\0';     /* process's name */
        ProcTable[i].startArg[0] = '\0';  /* args passed to process */
        ProcTable[i].state.start = NULL;             /* current context for process */
        ProcTable[i].state.pageTable = NULL;             /* current context for process */
        ProcTable[i].pid = -1;               /* process id */
        ProcTable[i].priority = 0;
        ProcTable[i].startFunc = NULL;   /* function where process begins -- launch */
        ProcTable[i].stack = NULL;
        ProcTable[i].stackSize = 0;
        ProcTable[i].status = 0;        /* READY, BLOCKED, QUIT, etc. */
        ProcTable[i].parentProcPtr = NULL;
        newProcList(&ProcTable[i].children);
        ProcTable[i].quit = 0;
        newProcList(&ProcTable[i].deadChildren);
        ProcTable[i].deadSibling = NULL;
        ProcTable[i].zap = 0;
        newProcList(&ProcTable[i].zapList);
        ProcTable[i].startTime = 0;
        ProcTable[i].procTime = 0;
        ProcTable[i].sliceTime = 0;
    }

    // Initialize the Ready list, etc.
    if (DEBUG && debugflag)
        USLOSS_Console("startup(): initializing the Ready list\n");
    for (int i = 0; i < SENTINELPRIORITY; i++) {
        newProcList(&ReadyList[i]);
    }

    Current = &ProcTable[0];
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
    Current = &ProcTable[result];
  
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
    checkKernelMode();

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
    int procSlot = -1;

    if (DEBUG && debugflag)
        USLOSS_Console("fork1(): creating process %s\n", name);

    checkKernelMode();

    disableInterrupts();

    // Return if stack size is too small
    if (stacksize < USLOSS_MIN_STACK) {
        if (DEBUG && debugflag)
            USLOSS_Console("fork1(): stack size too small");
        return -2;
    }

    // Is there room in the process table? What is the next PID?
    int i;
    for (i = 1; i <= MAXPROC; i++) {
        if (ProcTable[i % MAXPROC].pid == -1) {
            procSlot = i;
            ProcTable[procSlot].pid = nextPid++;
            break;
        }
    }
    // If ProcTable is full return -1
    if (i == MAXPROC + 1) {
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

    // set up stack
    ProcTable[procSlot].stackSize = stacksize;
    if (DEBUG && debugflag)
        USLOSS_Console("fork1(): malloc stackSize\n");
    ProcTable[procSlot].stack = malloc(ProcTable[procSlot].stackSize);

    ProcTable[procSlot].priority = priority;
    // Initialize context for this process, but use launch function pointer for
    // the initial value of the process's program counter (PC)

    if (DEBUG && debugflag)
        USLOSS_Console("fork1(): calling USLOSS_ContextInit\n");
    USLOSS_ContextInit(&(ProcTable[procSlot].state),
                       ProcTable[procSlot].stack,
                       ProcTable[procSlot].stackSize,
                       NULL,
                       launch);

    // for future phase(s)
    p1_fork(ProcTable[procSlot].pid);

    // More stuff to do here...
    // add process to parent's children
    if (Current->pid == -1) {
        enqueue(&Current->children, &ProcTable[procSlot]);
        ProcTable[procSlot].parentProcPtr = Current;
    }

    enqueue(&ReadyList[ProcTable[procSlot].priority-1], &ProcTable[procSlot]);
    ProcTable[procSlot].status = READY;

    if (startFunc != sentinel) {
        dispatcher();
    }
    
    // add process to ready list
    // call dispatcher
    // enable interrupts
    enableInterrupts();

    return ProcTable[procSlot].pid;  // -1 is not correct! Here to prevent warning.
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
    checkKernelMode();
    disableInterrupts();

    if (Current->children.size == 0 && Current->deadChildren.size == 0) {
        return -2;
    }

    if (Current->deadChildren.size == 0) {
//        block(JOINBLOCK);
    }

    procPtr child = dequeue(&Current->deadChildren);
    int childPid;
    childPid = child->pid;

    ProcTable[childPid].nextProcPtr = NULL;
    ProcTable[childPid].childProcPtr = NULL;
    ProcTable[childPid].nextSiblingPtr = NULL;
    ProcTable[childPid].name[0] = '\0';     /* process's name */
    ProcTable[childPid].startArg[0] = '\0';  /* args passed to process */
    ProcTable[childPid].state.start = NULL;             /* current context for process */
    ProcTable[childPid].state.pageTable = NULL;             /* current context for process */
    ProcTable[childPid].pid = -1;               /* process id */
    ProcTable[childPid].priority = 0;
    ProcTable[childPid].startFunc = NULL;   /* function where process begins -- launch */
    ProcTable[childPid].stack = NULL;
    ProcTable[childPid].stackSize = 0;
    ProcTable[childPid].status = 0;        /* READY, BLOCKED, QUIT, etc. */
    ProcTable[childPid].parentProcPtr = NULL;
    newProcList(&ProcTable[childPid].children);
    ProcTable[childPid].quit = 0;
    newProcList(&ProcTable[childPid].deadChildren);
    ProcTable[childPid].deadSibling = NULL;
    ProcTable[childPid].zap = 0;
    newProcList(&ProcTable[childPid].zapList);
    ProcTable[childPid].startTime = 0;
    ProcTable[childPid].procTime = 0;
    ProcTable[childPid].sliceTime = 0;

    if (Current->zapList.size != 0) {
        childPid = -1;
    }
    enableInterrupts();
    return childPid;
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
    checkKernelMode();
    disableInterrupts();

    procPtr child = peek(&Current->children);
    while (child != NULL) { 
        if (child->status != QUIT) {
            USLOSS_Console("quit(): process has live children");
            USLOSS_Console("halting...\n");
            USLOSS_Halt(1);
        }
        child = child->nextSiblingPtr;
    }

    Current->status = QUIT;
    Current->quit = status;
    dequeue(&ReadyList[Current->priority-1]);
    if (Current->parentProcPtr != NULL) {
        removeChild(&Current->parentProcPtr->children, Current);
        enqueue(&Current->parentProcPtr->deadChildren, Current);

        if (Current->parentProcPtr->status == JOINBLOCK) {
            Current->parentProcPtr->status = READY;
            enqueue(&ReadyList[Current->parentProcPtr->priority-1], Current->parentProcPtr);
        }
    }

    while (Current->zapList.size > 0) {
        procPtr zapper = dequeue(&Current->zapList);
        zapper->status = READY;
        enqueue(&ReadyList[zapper->priority-1], zapper);
    }

    while (Current->deadChildren.size > 0) {
        procPtr child = dequeue(&Current->deadChildren);

        ProcTable[child->pid].nextProcPtr = NULL;
        ProcTable[child->pid].childProcPtr = NULL;
        ProcTable[child->pid].nextSiblingPtr = NULL;
        ProcTable[child->pid].name[0] = '\0';     /* process's name */
        ProcTable[child->pid].startArg[0] = '\0';  /* args passed to process */
        ProcTable[child->pid].state.start = NULL;             /* current context for process */
        ProcTable[child->pid].state.pageTable = NULL;             /* current context for process */
        ProcTable[child->pid].pid = -1;               /* process id */
        ProcTable[child->pid].priority = 0;
        ProcTable[child->pid].startFunc = NULL;   /* function where process begins -- launch */
        ProcTable[child->pid].stack = NULL;
        ProcTable[child->pid].stackSize = 0;
        ProcTable[child->pid].status = 0;        /* READY, BLOCKED, QUIT, etc. */
        ProcTable[child->pid].parentProcPtr = NULL;
        newProcList(&ProcTable[child->pid].children);
        ProcTable[child->pid].quit = 0;
        newProcList(&ProcTable[child->pid].deadChildren);
        ProcTable[child->pid].deadSibling = NULL;
        ProcTable[child->pid].zap = 0;
        newProcList(&ProcTable[child->pid].zapList);
        ProcTable[child->pid].startTime = 0;
        ProcTable[child->pid].procTime = 0;
        ProcTable[child->pid].sliceTime = 0;
    }
    if (Current->parentProcPtr == NULL) {
        ProcTable[Current->pid].nextProcPtr = NULL;
        ProcTable[Current->pid].childProcPtr = NULL;
        ProcTable[Current->pid].nextSiblingPtr = NULL;
        ProcTable[Current->pid].name[0] = '\0';     /* process's name */
        ProcTable[Current->pid].startArg[0] = '\0';  /* args passed to process */
        ProcTable[Current->pid].state.start = NULL;             /* current context for process */
        ProcTable[Current->pid].state.pageTable = NULL;             /* current context for process */
        ProcTable[Current->pid].pid = -1;               /* process id */
        ProcTable[Current->pid].priority = 0;
        ProcTable[Current->pid].startFunc = NULL;   /* function where process begins -- launch */
        ProcTable[Current->pid].stack = NULL;
        ProcTable[Current->pid].stackSize = 0;
        ProcTable[Current->pid].status = 0;        /* READY, BLOCKED, QUIT, etc. */
        ProcTable[Current->pid].parentProcPtr = NULL;
        newProcList(&ProcTable[Current->pid].children);
        ProcTable[Current->pid].quit = 0;
        newProcList(&ProcTable[Current->pid].deadChildren);
        ProcTable[Current->pid].deadSibling = NULL;
        ProcTable[Current->pid].zap = 0;
        newProcList(&ProcTable[Current->pid].zapList);
        ProcTable[Current->pid].startTime = 0;
        ProcTable[Current->pid].procTime = 0;
        ProcTable[Current->pid].sliceTime = 0;
    }

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
    // TODO nextProcess should point to next process in ReadyList
    checkKernelMode();
    disableInterrupts();

    // Andrea notes
    // remove first proc from ReadyList (Current)
    // Usloss context switch from old to nextProcess
    // can have fields in ProcStruct that are just for readylist
    // 
    if (Current->status == RUNNING) {
        Current->status = READY;
        dequeue(&ReadyList[Current->priority-1]);
        enqueue(&ReadyList[Current->priority-1], Current);
    }

    for (int i = 0; i < SENTINELPRIORITY; i++) {
        if (ReadyList[i].size > 0) {
            nextProcess = peek(&ReadyList[i]);
            break;
        }
    }

    procPtr prev = Current;
    Current = nextProcess;
    Current->status = RUNNING;

    if (prev != Current) {
        if (prev->pid > -1) {
//            prev->procTime += USLOSS_Clock() - prev->startTime;
        }
        Current->sliceTime = 0;
//        Current->startTime = USLOSS_Clock();
    }

    p1_switch(Current->pid, nextProcess->pid);
    enableInterrupts();
    USLOSS_ContextSwitch(&prev->state, &nextProcess->state);
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
    int cur_mode = USLOSS_PsrGet();
    if ((cur_mode & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("Not in kernel mode");
        USLOSS_Console("halting...\n");
        USLOSS_Halt(1);
    }

    USLOSS_PsrSet(cur_mode & ~USLOSS_PSR_CURRENT_INT);
} /* disableInterrupts */

void enableInterrupts() {
    int cur_mode = USLOSS_PsrGet();
    if ((cur_mode & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("Not in kernel mode");
        USLOSS_Console("halting...\n");
        USLOSS_Halt(1);
    }

    USLOSS_PsrSet(cur_mode | USLOSS_PSR_CURRENT_INT);
}

void checkKernelMode() {
    // test if in kernel mode; halt if in user mode
    int cur_mode = USLOSS_PsrGet();
    if (DEBUG && debugflag)
        USLOSS_Console("fork1(): psr is %d\n", cur_mode);

    if ((cur_mode & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("fork1(): current mode not kernel\n");
        USLOSS_Console("halting...\n");
        USLOSS_Halt(1);
    }
}

void newProcList(procList *p) {
    p->head = NULL;
    p->tail = NULL;
    p->size = 0;
    p->type = 0;
}

void enqueue(procList *l, procPtr p) {
    if (l->head == NULL && l->tail == NULL) {
        l->head = l->tail = p;
    }
    else {
        if (l->type == READYLIST) {
            l->tail->nextProcPtr = p;
        } else if (l->type == CHILDREN) {
            l->tail->nextSiblingPtr = p;
        } else {
            l->tail->deadSibling = p;
        }
        l->tail = p;
   }
   l->size++;
}

procPtr dequeue(procList *l) {
    procPtr tmp = l->head;
    if (l->head == NULL) {
        return NULL;
    }
    if (l->head == l->tail) {
        l->head = l->tail = NULL;
    }
    else {
        if (l->type == READYLIST) {
            l->head = l->head->nextProcPtr;
        } else if (l->type == CHILDREN) {
            l->head = l->head->nextSiblingPtr;
        } else {
            l->head = l->head->deadSibling;
        }
    }
    l->size--;
    return tmp;    
}

procPtr peek(procList *l) {
    if (l->head == NULL) {
        return NULL;
    }
    return l->head;
}

void removeChild(procList *l, procPtr child) {
    if (l->head == NULL || l->type != CHILDREN)
        return;

    if (l->head == child) {
        dequeue(l);
        return;
    }

    procPtr prev = l->head;
    procPtr p = l->head->nextSiblingPtr;

    while (p != NULL) {
        if (p == child) {
            if (p == l->tail)
                l->tail = prev;
            else
                prev->nextSiblingPtr = p->nextSiblingPtr->nextSiblingPtr;
            l->size--;
        }
        prev = p;
        p = p->nextSiblingPtr;
    }
}

