#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
// TRAP 처리하는 함수
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){ // TRAP 번호가 시스템콜에 대한 것이라면
    if(myproc()->killed) // 만약 현재 프로세스가 종료되었다면
      exit(); // exit 시스템 콜 호출
    myproc()->tf = tf; // 현재 프로세스의 tf 포인터를 보고 커널 모드 전환하기 위한 정보 획득
    syscall(); // 시스템콜 호출
    if(myproc()->killed) // 시스템콜 수행 후 종료되었다면
      exit(); // exit 시스템 콜 호출
    return;
  }

  switch(tf->trapno){ // 시스템 콜 TRAP 이외의 번호에 대해 Switch-case로 처리
  case T_IRQ0 + IRQ_TIMER: // Timer Inturrupt -> 즉, CPU 사용 1 tick이 지난 경우
    if(cpuid() == 0){
/*
* CPU 코어 0번에 대해서만 tick을 증가시키는 이유
* 만약 멀티 코어 시스템에서 각각의 CPU마다 독립적으로 틱을 증가시키면 일관성 문제가 발생하게 된다.
* 그리고, 각 코어마다 타이머 인터럽트가 발생하기 때문에 비효율적이다.
* 따라서 CPU 0이 대표해서 Tick값을 관리하는것
*/
      acquire(&tickslock); // Tick 관련 lock 획득
      ticks++; // 틱 증가
      wakeup(&ticks); // ticks를 기다리는 프로세스들 깨우기
      release(&tickslock); // 락 해제
    }
    lapiceoi(); // 인터럽트 처리 완료했다고 알리는 함수
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  // 만약 인터럽트(트랩)이 걸린 현재 프로세스가 존재하고, 종료된 상태이고, 사용자 모드인 경우
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit(); // exit 시스템 콜 함수 호출

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  // 만약 인터럽트(트랩)이 걸랜 현재 프로세스가 존재하고, RUNNING 상태이며,
  // 트랩 사유가 타이머 인터럽트라면 yield() 함수를 호출해서 다른 프로세스를 선택한다.
  // 이를 통해 XV6의 기본적인 스케줄러 방식은 1Tick마다 CPU를 점유하는 프로세스가 달라지는 ROUND ROBIN 방식임을 알 수 있다.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  // CPU 양보 이후 (yield), 종료를 체크하는 부분
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
