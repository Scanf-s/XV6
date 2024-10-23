#include "types.h"
#include "stat.h"
#include "user.h"

int
main(void)
{
    int pid1, pid2, pid3;

    printf(1, "start scheduler_test\n");

    // 첫 번째 자식 프로세스 생성
    pid1 = fork();
    if(pid1 < 0){
        printf(1, "fork failed\n");
        exit();
    }

    if(pid1 == 0){
        printf(1, "PID: %d created\n", getpid());
        if(set_proc_info(2, 0, 0, 0, 300) < 0){
            printf(1, "set_proc_info failed\n");
            exit();
        }
        printf(1, "Set process %d's info complete\n", getpid());
        while(1);
    } else {
        // 두 번째 자식 프로세스 생성
        pid2 = fork();
        if(pid2 < 0){
            printf(1, "fork failed\n");
            exit();
        }

        if(pid2 == 0){
            printf(1, "PID: %d created\n", getpid());
            if(set_proc_info(2, 0, 0, 0, 300) < 0){
                printf(1, "set_proc_info failed\n");
                exit();
            }
            printf(1, "Set process %d's info complete\n", getpid());
            while(1);
        } else {
            // 세 번째 자식 프로세스 생성
            pid3 = fork();
            if(pid3 < 0){
                printf(1, "fork failed\n");
                exit();
            }

            if(pid3 == 0){
                printf(1, "PID: %d created\n", getpid());
                if(set_proc_info(2, 0, 0, 0, 300) < 0){
                    printf(1, "set_proc_info failed\n");
                    exit();
                }
                printf(1, "Set process %d's info complete\n", getpid());
                while(1);
            } else {
                // 자식 프로세스 3개 모두 대기
                wait();
                wait();
                wait();
                printf(1, "end of scheduler_test\n");
            }
        }
    }
    exit();
}