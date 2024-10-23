#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

// 프로세스 테이블 구조체
struct {
  struct spinlock lock;  // 프로세스 테이블 접근을 위한 락
  struct proc proc[NPROC];  // 프로세스 배열
} ptable;

static struct proc *initproc; // pid 1번 init 프로세스에 대한 전역 구조체

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p; // 새로 할당할 프로세스를 가리킬 포인터
  char *sp; // 스택 포인터

  acquire(&ptable.lock); // 락 획득

  // 프로세스 테이블에서 UNUSED 상태의 슬롯을 찾는다.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock); // UNUSED 프로세스를 찾지 못했다면 락 해제하고 0 반환
  return 0;

found:
  p->state = EMBRYO; // 프로세스 상태를 EMBRYO로 변경 (초기화 진행 중인 상태라고 이해하면 된다.)
  p->pid = nextpid++; // 새로운 프로세스 ID 할당

  release(&ptable.lock); // 프로세스에 대한 기본 초기화가 끝났으므로 락 해제해서 넘겨준다.

  // 프로세스의 커널 스택 할당
  if((p->kstack = kalloc()) == 0){ // 커널 스택 할당 실패 시
    p->state = UNUSED; // 다시 상태 변환해주고
    return 0; // 실패했으니까 0 반환
  }
  sp = p->kstack + KSTACKSIZE; // 커널 스택의 최상단 주소 계산

  // 트랩 프레임을 위한 공간 확보
  sp -= sizeof *p->tf; // 스택에서 트랩 프레임 크기만큼 공간 확보
  p->tf = (struct trapframe*)sp; // 트랩 프레임 포인터 설정

  // forkret 함수에서 시작하도록 컨텍스트 설정
  // trapret은 사용자 모드로 전환할 때 사용된다. (return from trap)
  sp -= 4; // 리턴 주소를 위한 공간 확보
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context; // 컨텍스트를 위한 공간 확보
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context); // 컨텍스트 초기화
  p->context->eip = (uint)forkret;

  //MLFQ init
  p->q_level = 0; // 최상위 큐에서 시작해야하니까 (물론 idle, init, shell은 3번째에서 시작해야함)
  p->cpu_burst = 0; // time slice 내에서 cpu 사용한 시간 (tick)
  p->cpu_wait = 0; // 대기 시간 초기화
  p->io_wait_time = 0; // I/O 대기 시간 초기화
  p->end_time = -1; // 총 할당량 설정 -> 일단 계속해서 실행하도록 설정한다
  p->cpu_accumulate_time = 0;  // 누적 CPU 사용 시간 초기화

  return p; // userinit()함수와 fork()에서 allocproc을 호출하는데, 다시 여기로 프로세스를 반환한다.
}

//PAGEBREAK: 32
// Set up first user process.
// PID 1번 INIT 프로세스를 초기화하는 함수.
// init 프로세스는 모든 프로세스의 공통조상이 되므로, 반드시 필요하다.
void
userinit(void)
{
  struct proc *p; // 프로세스 구조체 선언
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc(); // allocproc()에서 기초적인 프로세스 메타데이터 생성해서 프로세스 정보를 가져온다.
  
  initproc = p; // 프로세스를 INIT 프로세스로 변경(이름 바꾼거임)

  // 커널 가상 메모리 설정
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  // 코드와 데이터 세그먼트 설정
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  // 프로세스 이름을 "initcode"로 설정
  safestrcpy(p->name, "initcode", sizeof(p->name));
  // 루트 디렉토리(/)를 현재 작업 디렉토리로 설정
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  // 항상 process의 상태값을 변경하려면 lock을 획득해야한다. (일관성을 위해)
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np; // new process >> 새로 생성될 자식 프로세스
  struct proc *curproc = myproc(); // 현재 프로세스에서 복사해야하니까 현재 프로세스 정보가 담긴 구조체

  // allocproc() 호출해서 프로세스 초기 상태를 가져온다.
  if((np = allocproc()) == 0){
    return -1;
  }

  // 부모 프로세스의 메모리 공간을 자식 프로세스에게 복사
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc; // 부모 자식 관계 설정하는 부분
  *np->tf = *curproc->tf;

  // 자식 프로세스의 fork() 반환값을 0으로 설정하는 곳이다.
  // 부모는 자식의 pid를, 자식은 0을 반환받게 된다.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock); // fork된 프로세스의 상태를 변경하기 위해 lock 획득

  np->state = RUNNABLE; // state 변경

  release(&ptable.lock);

  // 부모 프로세스에게는 자식의 PID를 반환하고,
  // 자식 프로세스는 np->tf->eax = 0으로 설정했으므로 0을 반환받게 된다.
  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
// 현재 프로세스를 종료하기 위해 전처리 과정을 수행하고, 스케줄러를 호출하는 함수이다.
void
exit(void)
{
  struct proc *curproc = myproc(); // 종료할 현재 프로세스
  struct proc *p;
  int fd;

  // init 프로세스는 절대 종료되면 안됨!!
  if(curproc == initproc)
    panic("init exiting");

  // 프로세스가 열어둔 모든 파일을 닫음
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // 부모 프로세스가 wait()에서 대기 중일 수 있으므로 깨워야한다.
  // 즉, 자식 프로세스가 종료되기를 기다리는 부모를 깨워서 자식의 종료 상태를 처리할 수 있게 해야 한다.
  wakeup1(curproc->parent);

  // 현재 프로세스의 자식들을 init 프로세스에게 넘겨서 고아 프로세스를 처리하도록 부탁하는 로직이다.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE; // 좀비 상태로 변경한다.

  // 프로세스가 종료되었으므로 스케줄러를 호출해야 한다. 아래에 sched 설명이 있지만, 간단하게 설명하자면
  // Context switch 하기 전에, 준비 작업을 수행하는 함수이다.
  sched();

  //panic까지 진행된거면 뭔가 잘못된 일이 일어난것임
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc(); // 현재 프로세스 정보
  
  acquire(&ptable.lock);

  // 무한루프로 자식 프로세스의 상태를 계속 확인
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1; // 자식 프로세스 발견
      if(p->state == ZOMBIE){ // 좀비 상태의 자식을 발견한 경우 -> 프로세스 초기화
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // 자식 프로세스가 없다면, lock을 해제하고 -1을 반환한다.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // 자식은 있는데, 아직 종료가 안된 상태라면 Sleep state로 변경한다. -> exit()에서 wakeup1을 호출해서 깨어나게 됨
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;  // 프로세스 구조체 포인터
  struct cpu *c = mycpu();  // 현재 CPU 코어의 정보를 가져옴
  c->proc = 0;  // 현재 CPU 코어가 실행 중인 프로세스 정보를 초기화

  for(;;){  // 스케줄러는 무한 루프로 실행됨
    // 현재 프로세서에서 인터럽트 활성화
    sti();

    // 프로세스 테이블을 순회하며 실행할 프로세스를 찾음
    acquire(&ptable.lock);  // 프로세스 테이블 접근을 위한 락 획득
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){  // 프로세스 테이블의 모든 프로세스를 확인
      if(p->state != RUNNABLE)  // RUNNABLE 상태가 아닌 프로세스는 스킵
        continue;

      // 선택된 프로세스로 전환
      c->proc = p;  // 현재 CPU코어가 실행할 프로세스 설정
      switchuvm(p);  // 프로세스의 페이지 테이블로 전환 (메모리 컨텍스트 전환)
      p->state = RUNNING;  // 프로세스 상태를 RUNNING으로 변경

      swtch(&(c->scheduler), p->context);  // CPU 레지스터 컨텍스트를 저장하고 프로세스의 컨텍스트로 전환
      switchkvm();  // 커널의 페이지 테이블로 다시 전환

      // 프로세스 실행이 완료됨
      // 돌아오기 전에 프로세스는 자신의 상태를 변경했어야 함 (RUNNABLE, SLEEPING, ZOMBIE 등)
      // 이 경우는 다른 함수에서 소개할것이다
      c->proc = 0;  // CPU의 현재 프로세스 정보를 초기화
    }
    release(&ptable.lock);  // 프로세스 테이블 락 해제
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena; // 인터럽트 활성화 상태 저장 변수
  struct proc *p = myproc(); // 현재 실행 중인 프로세스

  if(!holding(&ptable.lock))
    panic("sched ptable.lock"); // ptable 락을 획득하지 않은 상태면 에러
  if(mycpu()->ncli != 1)
    panic("sched locks"); // 다른 락이 걸려있으면 에러
  if(p->state == RUNNING)
    panic("sched running"); // 여전히 RUNNING 상태면 에러 -> sched 호출 전에 상태를 변경하고 와야함
  if(readeflags()&FL_IF)
    panic("sched interruptible"); // 인터럽트가 활성화되어 있으면 에러
  intena = mycpu()->intena; // 현재 CPU의 인터럽트 활성화 상태 저장
  swtch(&p->context, mycpu()->scheduler); // 현재 프로세스에서 scheduler process로 context switch
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE; // 다시 Ready 상태로 변경
  sched(); // sched 호출해서 전처리
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
/*
* 1. allocproc()에서 프로세스의 시작점을 p->context->eip = (uint)forkret; 으로 설정함
* 2. scheduler() -> context switch -> forkret() -> trapret -> 사용자 모드 순서로 진행된다.
*/
  static int first = 1;

  // Still holding ptable.lock from scheduler.
  // scheduler() 함수에서 context switch 수행 시 lock이 획득된 상태로 넘어오므로
  // 사용자 모드로 전환하기 전에 반드시 release로 lock을 해제한다.
  release(&ptable.lock);

  // 2. 시스템에서 첫 프로세스인 경우에만
  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV); // 파일시스템 초기화
    initlog(ROOTDEV); // 로그 시스템 초기화
  }

  // Return to "caller", actually trapret (see allocproc).
  // trapret으로 이동하고, 사용자 모드로 전환하게 된다.
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0) // 현재 가져온 프로세스가 없다면 panic
    panic("sleep");

  if(lk == 0) // 프로세스 정보 수정해야 하는데 Lock이 없다면 panic
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  // 만약 ptable.lock이 아니라 다른 lock을 가져온 경우, ptable.lock을 획득하는 로직
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }

  // Sleep state로 전환하는 부분
  p->chan = chan;
  p->state = SLEEPING;

  sched(); // Sleep state로 변경했으니까 lock 가진 상태에서 sched 호출해저 context switch 전처리

  // 다시 깨어났다면, chan 초기화
  p->chan = 0;

  // 다시 본인이 가지고있던 lock을 되찾는 로직
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) // SLEEP STATE를 READY STATE로 변경
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan); // 락 획득 후 wakeup1() 호출
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
