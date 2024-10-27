#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct { // 프로세스 테이블
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;
struct priority_queue mlfq[NQUEUE]; 
static struct proc *initproc; // pid 1번 init 프로세스에 대한 전역 구조체

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
init_mlfq(void)
{
    for(int i = 0; i < NQUEUE; i++) { // 큐 초기화
        mlfq[i].front = 0;
        mlfq[i].rear = 0;
        mlfq[i].size = 0;
    }
}

int
enqueue(int level, struct proc *p)
{
    if (!holding(&ptable.lock)) // ptable.lock이 잡혀있지 않은 경우
      panic("Function requires ptable.lock to be held");

    if (level < 0 || level >= NQUEUE) { // 큐 레벨이 범위를 벗어난 경우
        cprintf("Invalid queue level\n");
        return -1;
    }

    if(mlfq[level].size >= NPROC) // 큐가 가득 찬 경우
        return -1;

    if (mlfq[level].size == 0) { // 큐가 비어있는 경우
        mlfq[level].front = 0;
        mlfq[level].rear = 0;
    } else {
        mlfq[level].rear = (mlfq[level].rear + 1) % NPROC; // rear를 다음 위치로 이동
    }

    mlfq[level].queue[mlfq[level].rear] = p; // 큐에 프로세스 추가
    mlfq[level].size++; // 큐 사이즈 증가

    // 우선순위 조건대로 큐를 정렬한다.
    // 만약 io_wait_time이 더 크다면 더 높은 우선순위
    // io_wait_time이 같다면 pid값이 더 큰 프로세스가 더 높은 우선순위
    sort_queue(level);

    return 0;
}

void 
sort_queue(int level) {
    int size = mlfq[level].size;
    struct proc* temp_queue[NPROC];
    int idx = mlfq[level].front;

    // 큐의 현재 요소를 임시 배열로 복사
    for(int i = 0; i < size; i++) {
        temp_queue[i] = mlfq[level].queue[idx];
        idx = (idx + 1) % NPROC;
    }

    // 요구사항대로 버블 정렬.. 나중에 다른 정렬 알고리즘으로 바꿔보도록 하자!
    for(int i = 0; i < size - 1; i++) {
        for(int j = i + 1; j < size; j++) {
            if (temp_queue[i]->io_wait_time > temp_queue[j]->io_wait_time ||
                (temp_queue[i]->io_wait_time == temp_queue[j]->io_wait_time && temp_queue[i]->pid > temp_queue[j]->pid)) {
                struct proc* tmp = temp_queue[i];
                temp_queue[i] = temp_queue[j];
                temp_queue[j] = tmp;
            }
        }
    }

    // 정렬된 배열을 큐에 다시 삽입
    idx = mlfq[level].front;
    for(int i = 0; i < size; i++) {
        mlfq[level].queue[idx] = temp_queue[i];
        idx = (idx + 1) % NPROC;
    }
}

struct proc*
dequeue(int level)
{
    if (!holding(&ptable.lock)) // ptable.lock이 잡혀있지 않은 경우
      panic("Function requires ptable.lock to be held");
    
    if(mlfq[level].size <= 0)
        return 0;  // 큐가 비어있는 경우
        
    struct proc *p = mlfq[level].queue[mlfq[level].front];
    mlfq[level].front = (mlfq[level].front + 1) % NPROC;
    mlfq[level].size--;
    
    if(mlfq[level].size == 0) {
        mlfq[level].front = 0;
        mlfq[level].rear = 0;
    }
    
    return p;
}

void
update_process_queue(struct proc *p, int new_level){
    if (new_level < 0 || new_level >= NQUEUE) {
        cprintf("Invalid queue level\n");
        return;
    }

    int level = p->q_level;
    int idx = mlfq[level].front;
    int found = 0;

    for (int i = 0; i < mlfq[level].size; i++) {
        if (mlfq[level].queue[idx] == p) {
            found = 1;
            break;
        }
        idx = (idx + 1) % NPROC;
    }

    if (found) {
        // 프로세스를 큐에서 제거
        int next_idx = (idx + 1) % NPROC;
        while (next_idx != (mlfq[level].rear + 1) % NPROC) {
            mlfq[level].queue[idx] = mlfq[level].queue[next_idx];
            idx = next_idx;
            next_idx = (next_idx + 1) % NPROC;
        }
        mlfq[level].rear = (mlfq[level].rear - 1 + NPROC) % NPROC;
        mlfq[level].size--;
    }

    // 새로운 큐에 프로세스를 삽입
    enqueue(new_level, p);

    p->q_level = new_level;
    p->cpu_burst = 0;
    p->cpu_wait = 0;
    p->io_wait_time = 0;
}

int
remove_from_queue(int level, struct proc *p)
{
    int idx = mlfq[level].front;
    int found = 0;

    if (!holding(&ptable.lock)) // ptable.lock이 잡혀있지 않은 경우
      panic("Remove_from_queue : Function requires ptable.lock to be held");
  
    for (int i = 0; i < mlfq[level].size; i++) {
        if (mlfq[level].queue[idx] == p) {
            found = 1;
            break;
        }
        idx = (idx + 1) % NPROC;
    }
  
    if (found) {
        // 프로세스를 큐에서 제거
        int next_idx = (idx + 1) % NPROC;
        while (next_idx != (mlfq[level].rear + 1) % NPROC) {
            mlfq[level].queue[idx] = mlfq[level].queue[next_idx];
            idx = next_idx;
            next_idx = (next_idx + 1) % NPROC;
        }
        mlfq[level].rear = (mlfq[level].rear - 1 + NPROC) % NPROC;
        mlfq[level].size--;
        return 0;
    }
    return -1; // 프로세스를 찾지 못한 경우
}


int
get_quantum(int level)
{ // 큐 레벨에 따른 time quantum 값 반환 함수 (param.h에 정의된 값 참고바람)
    switch(level) {
        case 0: return TQ_0;
        case 1: return TQ_1;
        case 2: return TQ_2;
        case 3: return TQ_3;
        default: return TQ_3;
    }
}

void
pinit(void) // 프로세스 테이블 초기화하는 함수
{
  initlock(&ptable.lock, "ptable");

  init_mlfq();  // MLFQ 초기화
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
  p->q_level = 3; // init 프로세스는 무조건 최하위 큐에 위치시킨다.
  p->cpu_burst = 0;
  p->cpu_wait = 0;
  p->io_wait_time = 0;
  p->end_time = -1; // init이 종료되면 안되므로 -1로 설정
  p->cpu_accumulate_time = 0;

  // MLFQ 큐에 추가
  if(enqueue(p->q_level, p) < 0) {
    panic("userinit: enqueue failed");  // 큐 삽입 실패 시 패닉
  }

  release(&ptable.lock);

  #ifdef DEBUG
    cprintf("Init process enqueued to level %d\n", p->q_level);
  #endif
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

  if (np->pid == 2) { // SHELL 프로세스라면 q_level 3으로 고정
    // init.c를 보면, init 프로세스가 sh 프로세스를 생성하는것을 볼 수 있다. init은 1번이므로 당연히 2번은 sh 프로세스
    np->q_level = 3;
  }

  np->state = RUNNABLE; // state 변경

  // MLFQ 큐에 추가
  if(enqueue(np->q_level, np) < 0) {
    release(&ptable.lock);
    panic("fork: enqueue failed");
  }

  release(&ptable.lock);

  #ifdef DEBUG
  if (np->pid > 2)  // init과 shell 프로세스 제외
    cprintf("Fork: pid %d created\n", np->pid);
  #endif

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
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    sti();
    acquire(&ptable.lock);

    // Aging check
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state == RUNNABLE && p->cpu_wait >= AGING_THRESHOLD) { // 만약 해당 프로세스의 CPU 대기시간이 250tick 이상이라면
        if (p->q_level > MIN_LEVEL) {
          p->q_level--;
          update_process_queue(p, p->q_level); // 큐를 업데이트한다.
          p->cpu_burst = 0;
          p->cpu_wait = 0;
          p->io_wait_time = 0;
          cprintf("PID: %d Aging to level %d\n", p->pid, p->q_level);
        }
      }
    }

    // 우선순위 큐에서 프로세스 선택
    int found = 0;
    struct proc *selected = 0;
    int selected_level = -1;

    // 높은 우선순위부터 검사
    for(int level = MIN_LEVEL; level <= MAX_LEVEL && !found; level++) {
      if(mlfq[level].size > 0) {
        selected = dequeue(level); // 이미 우선순위에 따라 정렬된 큐에서 하나 꺼내온다.
        if(selected && selected->state == RUNNABLE) {
          found = 1;
          selected_level = level;
        } else if(selected) {
          // RUNNABLE이 아니면 다시 큐에 넣지 않음
          #ifdef DEBUG
            cprintf("Removed non-RUNNABLE process %d from queue %d\n", 
                  selected->pid, level);
          #endif
        }
      }
    }

    if(found) {
      p = selected;
      #ifdef DEBUG
        cprintf("Selected PID: %d from level %d\n", p->pid, selected_level);
      #endif
      
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      c->proc = 0;

      if(p->state == RUNNABLE) {
        enqueue(p->q_level, p);
        #ifdef DEBUG
          cprintf("Process %d enqueued back to level %d\n", p->pid, p->q_level);
        #endif
      }
    }
    release(&ptable.lock);
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

  sched(); // Sleep state로 변경했으니까 lock 가진 상태에서 sched 호출

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
    if(p->state == SLEEPING && p->chan == chan){
      p->state = RUNNABLE;

      // RUNNABLE 상태가 되면 해당 레벨의 큐에 다시 삽입
      if(enqueue(p->q_level, p) < 0) {
        panic("wakeup1: enqueue failed");
      }
    }
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
      p->killed = 1; // 프로세스를 종료하기 위해 killed flag를 설정
      if(p->state == SLEEPING) {
        p->state = RUNNABLE;
        // SLEEPING에서 RUNNABLE로 상태가 변경되므로 큐에 추가
        if(enqueue(p->q_level, p) < 0) {
          panic("kill: enqueue failed");
        }
      }
      release(&ptable.lock);
      #ifdef DEBUG
        cprintf("Process %d killed\n", pid);
      #endif
      return 0;
    }
  }
  release(&ptable.lock);
  #ifdef DEBUG
    cprintf("Process %d not found\n", pid);
  #endif
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
