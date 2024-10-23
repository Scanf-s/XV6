#define NPROC        64  // maximum number of processes
#define KSTACKSIZE 4096  // size of per-process kernel stack
#define NCPU          8  // maximum number of CPUs
#define NOFILE       16  // open files per process
#define NFILE       100  // open files per system
#define NINODE       50  // maximum number of active i-nodes
#define NDEV         10  // maximum major device number
#define ROOTDEV       1  // device number of file system root disk
#define MAXARG       32  // max exec arguments
#define MAXOPBLOCKS  10  // max # of blocks any FS op writes
#define LOGSIZE      (MAXOPBLOCKS*3)  // max data blocks in on-disk log
#define NBUF         (MAXOPBLOCKS*3)  // size of disk block cache
#define FSSIZE       1000  // size of file system in blocks

// MLFQ constants
#define NQUEUE 4         // 큐 개수
#define MIN_LEVEL 0      // 최소 큐 레벨
#define MAX_LEVEL 3      // 최대 큐 레벨

#define TQ_0 10          // 최상위 큐 TQ
#define TQ_1 20          // 2번째 큐 TQ
#define TQ_2 40          // 3번째 큐 TQ
#define TQ_3 80          // 최하위 큐 TQ

#define AGING_THRESHOLD 250 // Aging 적용 기준 tick