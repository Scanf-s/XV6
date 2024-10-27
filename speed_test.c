#include "types.h"
#include "stat.h"
#include "user.h"

// CPU-바운드 프로세스: 무거운 계산 작업 수행
void cpu_bound_process() {
    int start = uptime();
    volatile int i;
    for (i = 0; i < 1e8; i++); // CPU 계산 작업
    int end = uptime();
    printf(1, "CPU-bound process PID %d executed in %d ticks\n", getpid(), end - start);
    exit();
}

// I/O-바운드 프로세스: 주기적으로 I/O 대기 상태 모방
void io_bound_process() {
    int start = uptime();
    int i;
    for (i = 0; i < 100; i++) {
        printf(1, "I/O operation %d\n", i);
        sleep(10); // I/O 대기 상태 모방
    }
    int end = uptime();
    printf(1, "I/O-bound process PID %d executed in %d ticks\n", getpid(), end - start);
    exit();
}

// 혼합형 프로세스: CPU 계산과 I/O 대기 번갈아 수행
void mixed_process() {
    int start = uptime();
    int i;
    for (i = 0; i < 50; i++) {
        volatile int j;
        for (j = 0; j < 1e6; j++); // CPU 계산 작업
        sleep(5); // I/O 대기 상태 모방
    }
    int end = uptime();
    printf(1, "Mixed process PID %d executed in %d ticks\n", getpid(), end - start);
    exit();
}

int main() {
    int pid;

    // CPU-바운드 프로세스 생성
    pid = fork();
    if (pid == 0) {
        cpu_bound_process();
    }

    // I/O-바운드 프로세스 생성
    pid = fork();
    if (pid == 0) {
        io_bound_process();
    }

    // 혼합형 프로세스 생성
    pid = fork();
    if (pid == 0) {
        mixed_process();
    }

    // 자식 프로세스들이 종료될 때까지 대기
    while (wait() != -1);

    printf(1, "All child processes have completed.\n");
    exit();
}
