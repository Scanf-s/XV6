#include "types.h"
#include "stat.h"
#include "user.h"

int
main(void)
{
    int pid;

    printf(1, "start scheduler_test\n");

    pid = fork();
    if(pid < 0){
        printf(1, "fork failed\n");
        exit();
    }

    if(pid == 0){
        // 자식 프로세스의 경우
        printf(1, "PID: %d created\n", getpid());
        if(set_proc_info(1, 0, 0, 0, 500) < 0){
            printf(1, "set_proc_info failed\n");
            exit();
        }
        printf(1, "Set process %d's info complete\n", getpid());

        while(1) {
            // 원하는 시나리오대로 동작하도록 무한루프를 돌린다.
            // CPU end_time까지 실행되고 종료된다.
        }
    } else {
        // 부모 프로세스는 자식 프로세스가 완료될때까지 기다린다.
        wait();
        printf(1, "end of scheduler_test\n");
    }

    exit();
}