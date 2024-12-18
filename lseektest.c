#include "hw1/types.h"
#include "stat.h"
#include "hw1/user.h"
#include "fcntl.h"

// 파일의 내용을 읽어 출력하는 함수
void print_content(int fd, const char* message) {
  char buffer[1000]; // 읽은 내용 임시로 저장하기 위한 변수
  int n; // read() 함수가 반환하는 바이트 수 저장 변수

  // 파일 시작점으로 이동하여 내용 출력
  lseek(fd, 0, SEEK_SET);
  printf(1, "%s: ", message);

  while ((n = read(fd, buffer, sizeof(buffer) - 1)) > 0) { // 읽은 바이트 수가 0보다 크면 루프를 계속 실행
    buffer[n] = '\0';  // buffer size - 1만큼 읽은 내용이 저장되고 , 맨 마지막에 널문자 추가해줌 (%s가 \0까지 읽으니까)
    printf(1, "%s", buffer);  // 현재 읽은 내용을 출력
  }
}

// 파일을 열고 오프셋을 변경한 후 문자열을 쓰는 함수
void write_content(int fd, int offset, const char* new_string) {
  // 파일 오프셋 변경
  if (lseek(fd, offset, SEEK_SET) < 0) {
    printf(1, "lseek() system call inner error occurs!!\n");
    close(fd);
    exit();
  }

  // 입력받은 문자열을 현재 위치에 쓰기
  if (write(fd, new_string, strlen(new_string)) != strlen(new_string)) {
    printf(1, "Failed to write string at this offset!!\n");
    close(fd);
    exit();
  }
}

int main(int argc, char **argv) {
  if (argc != 4) { // 인자 총 4개(명령어 포함) 못받은 경우
    printf(1, "usage: lseektest <filename> <offset> <string>\n");
    exit();
  }

  const char* filename = argv[1];
  int offset = atoi(argv[2]); // offset이 음수인 경우 atoi 함수의 while을 돌지 못해서 0으로 설정된다.
  const char* new_string = argv[3];

  // 파일 열기
  int fd = open(filename, O_RDWR);
  if (fd < 0) {
    printf(1, "Fail to open the file!!\n");
    exit();
  }

  // 1. 기존 내용 출력
  print_content(fd, "Before");
  printf(1, "\n");

  // 2. 파일 오프셋을 변경하고 문자열 쓰기
  // 만약 offset이 현재 파일보다 크다면 write() 시스템콜을 수정해서 파일을 늘려서 쓰도록 변경해야한다.
  // 지금 write() 시스템콜은 파일의 맨 끝에 그냥 붙여버린다. (실행 사진 확인)
  write_content(fd, offset, new_string);

  // 3. 변경 후 내용 출력
  print_content(fd, "After");
  printf(1, "\n");

  // 파일 닫기
  close(fd);
  exit();
}
