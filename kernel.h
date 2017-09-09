/* Patrick's DEBUG printing constant... */
#define DEBUG 1

typedef struct procStruct procStruct;

typedef struct procStruct * procPtr;

typedef struct procList {
    procPtr head;
    procPtr tail;
    int size;
    int type;
} procList;

struct procStruct {
   procPtr         nextProcPtr;
   procPtr         childProcPtr;
   procPtr         nextSiblingPtr;
   char            name[MAXNAME];     /* process's name */
   char            startArg[MAXARG];  /* args passed to process */
   USLOSS_Context  state;             /* current context for process */
   short           pid;               /* process id */
   int             priority;
   int (* startFunc) (char *);   /* function where process begins -- launch */
   char           *stack;
   unsigned int    stackSize;
   int             status;        /* READY, BLOCKED, QUIT, etc. */
   /* other fields as needed... */
   procPtr         parentProcPtr;
   procList        children;
   int             quit;
   procList        deadChildren;
   procPtr         deadSibling;
   int             zap;
   procList        zapList;
   int             startTime;
   int             procTime;
   int             sliceTime;
};

#define READY 1
#define RUNNING 2
#define QUIT 4
#define ZAPBLOCK 8
#define JOINBLOCK 16

#define READYLIST 0
#define CHILDREN 1
#define DEADCHILDREN 2
#define ZAP 4

struct psrBits {
    unsigned int curMode:1;
    unsigned int curIntEnable:1;
    unsigned int prevMode:1;
    unsigned int prevIntEnable:1;
    unsigned int unused:28;
};

union psrValues {
   struct psrBits bits;
   unsigned int integerPart;
};

/* Some useful constants.  Add more as needed... */
#define NO_CURRENT_PROCESS NULL
#define MINPRIORITY 5
#define MAXPRIORITY 1
#define SENTINELPID 1
#define SENTINELPRIORITY (MINPRIORITY + 1)

