// Per-CPU state
struct cpu {
  uchar apicid;                // Local APIC ID
  struct context *scheduler;   // swtch() here to enter scheduler
  struct taskstate ts;         // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];   // x86 global descriptor table
  volatile uint started;       // Has the CPU started?
  int ncli;                    // Depth of pushcli nesting.
  int intena;                  // Were interrupts enabled before pushcli?
  struct proc *proc;           // The process running on this cpu or null
};

extern struct cpu cpus[NCPU];
extern int ncpu;

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state
struct proc {
  uint sz;                    // 프로세스가 할당받은 메모리 크기 (바이트 단위)
  pde_t* pgdir;               // 페이지 디렉토리 (프로세스의 가상 메모리 공간을 관리)
  char *kstack;               // 이 프로세스의 커널 스택의 하단부 주소
  enum procstate state;       // 프로세스의 현재 상태 (UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE)
  int pid;                    // 프로세스 ID값 (프로세스 식별자)
  struct proc *parent;        // 부모 프로세스의 proc 구조체 포인터 (프로세스 트리 구성에 사용)
  struct trapframe *tf;       // 유저 모드에서 커널 모드로 전환시 레지스터 값들을 저장하는 트랩 프레임
  struct context *context;    // 컨텍스트 스위칭 시 레지스터 값들을 저장/복원하기 위한 구조체
  void *chan;                 // sleep 상태일 때 대기하는 채널 (wake up 시 이 값으로 프로세스를 식별)
  int killed;                 // 프로세스 종료 플래그 (1이면 종료 예정)
  struct file *ofile[NOFILE]; // 프로세스가 열어둔 파일들의 포인터 배열
  struct inode *cwd;          // 현재 작업 디렉토리의 inode
  char name[16];              // 프로세스 이름 (디버깅용)

  // MLFQ state
  int q_level;                // 큐 레벨
  int cpu_burst;              // time quantum 내에서 사용한 cpu tick 값
  int cpu_wait;               // ready state에서 queue에서 기다린 시간 (cpu tick)
  int io_wait_time;           // sleep state에서 queue에서 있던 시간 (cpu tick)
  int end_time;               // 본인이 사용할 수 있는 총 시간
  int cpu_accumulate_time;    // 얼마나 사용했는지 추적하기 위해 추가한 변수
};

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap